#include "oww.hpp"

#include <onnxruntime_cxx_api.h> // API C++ do ONNX Runtime
#include <portaudio.h>            // PortAudio para captura de microfone
#include <algorithm>              // std::copy_n, std::fill
#include <array>                  // std::array
#include <deque>                  // std::deque
#include <iostream>               // std::cout, std::cerr
#include <vector>                 // std::vector
#include <cstdlib>                // std::getenv
#include <filesystem>             // std::filesystem (C++17)

namespace fs = std::filesystem;

// Constantes de áudio do openWakeWord
constexpr int SR = 16000, CHUNK = 1280, CTX = 480;

// Configuração da sessão ONNX (1 thread intra-op para sistemas embarcados)
static Ort::SessionOptions opts() {
    Ort::SessionOptions o;
    o.SetIntraOpNumThreads(1);
    return o;
}

// Descobre a pasta onde o executável está localizado
static fs::path pasta_do_executavel() {
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
}

// Procura a pasta dos modelos testando locais relativos e de ambiente
static fs::path encontrar_pasta_modelos(const std::string& modelo_ww) {
    if (const char* env_p = std::getenv("OWW_MODELS")) {
        fs::path p(env_p);
        if (fs::exists(p / "melspectrogram.onnx")) return p;
    }

    fs::path exe_dir = pasta_do_executavel();
    std::vector<fs::path> candidatos = {
        "models",
        "wakeword/models",
        exe_dir / "models",
        exe_dir / "wakeword/models",
        exe_dir / "../wakeword/models"
    };

    for (const auto& c : candidatos) {
        if (fs::exists(c / "melspectrogram.onnx") &&
            fs::exists(c / "embedding_model.onnx") &&
            fs::exists(c / modelo_ww)) {
            return c;
        }
    }

    return "models";
}

// Executa a inferência síncrona de 1 modelo ONNX
static std::vector<float> run(Ort::Session& s, std::vector<float>& in,
                              std::vector<int64_t> shape) {
    Ort::AllocatorWithDefaultOptions a;
    auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto t = Ort::Value::CreateTensor<float>(mem, in.data(), in.size(),
                                             shape.data(), shape.size());
    auto inN = s.GetInputNameAllocated(0, a);
    auto outN = s.GetOutputNameAllocated(0, a);
    const char* i[] = {inN.get()};
    const char* o[] = {outN.get()};
    auto out = s.Run(Ort::RunOptions{nullptr}, i, &t, 1, o, 1);
    float* p = out[0].GetTensorMutableData<float>();
    size_t n = out[0].GetTensorTypeAndShapeInfo().GetElementCount();
    return {p, p + n};
}

// ============================================================================
// Definição da Estrutura Interna do Detector
// ============================================================================
struct Detector {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "oww"};
    Ort::Session mel, emb, clf;

    std::vector<float> raw = std::vector<float>(CHUNK + CTX, 0.f);
    std::deque<std::array<float, 32>> melbuf;
    std::deque<std::array<float, 96>> embbuf;

    PaStream* st = nullptr;
    int cooldown = 0;

    Detector(const std::string& d, const std::string& ww)
        : mel(env, (d + "/melspectrogram.onnx").c_str(), opts()),
          emb(env, (d + "/embedding_model.onnx").c_str(), opts()),
          clf(env, (d + "/" + ww).c_str(), opts()) {}

    ~Detector() {
        if (st) {
            Pa_StopStream(st);
            Pa_CloseStream(st);
            st = nullptr;
        }
        Pa_Terminate();
    }


    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;
    Detector(Detector&&) = default;
    Detector& operator=(Detector&&) = default;

    // Limpa o histórico de áudio na memória
    void reset() {
        std::fill(raw.begin(), raw.end(), 0.f);
        melbuf.clear();
        embbuf.clear();
        cooldown = 0;
    }

    // Processa 1 bloco PCM de 1280 amostras
    float feed(const int16_t* pcm) {
        raw.erase(raw.begin(), raw.begin() + CHUNK);
        for (int i = 0; i < CHUNK; i++) raw.push_back((float)pcm[i]);

        auto m = run(mel, raw, {1, (int64_t)raw.size()});
        for (size_t i = 0; i + 32 <= m.size(); i += 32) {
            std::array<float, 32> f;
            for (int k = 0; k < 32; k++) f[k] = m[i + k] / 10.f + 2.f;
            melbuf.push_back(f);
        }
        while (melbuf.size() > 76) melbuf.pop_front();
        if (melbuf.size() < 76) return 0.f;

        std::vector<float> e;
        e.reserve(76 * 32);
        for (auto& f : melbuf) e.insert(e.end(), f.begin(), f.end());
        auto v = run(emb, e, {1, 76, 32, 1});
        std::array<float, 96> a;
        std::copy_n(v.begin(), 96, a.begin());
        embbuf.push_back(a);
        while (embbuf.size() > 16) embbuf.pop_front();
        if (embbuf.size() < 16) return 0.f;

        std::vector<float> c;
        c.reserve(16 * 96);
        for (auto& x : embbuf) c.insert(c.end(), x.begin(), x.end());
        return run(clf, c, {1, 16, 96})[0];
    }
};

// ============================================================================
// Implementação das Funções da Biblioteca
// ============================================================================

void DetectorDeleter::operator()(Detector* p) const {
    delete p;
}

DetectorPtr inicializar_detector(const std::string& modelo_ww,
                                const std::string& pasta_modelos) {
    fs::path dir_modelos = pasta_modelos.empty()
                               ? encontrar_pasta_modelos(modelo_ww)
                               : fs::path(pasta_modelos);

    // Valida arquivos ONNX antes de tentar carregar
    for (const fs::path& f : {dir_modelos / "melspectrogram.onnx",
                              dir_modelos / "embedding_model.onnx",
                              dir_modelos / modelo_ww}) {
        if (!fs::exists(f)) {
            std::cerr << "[OWW ERRO] Arquivo de modelo não encontrado: " << f << "\n";
            return nullptr;
        }
    }

    std::cerr << "[OWW] Carregando modelos de: " << dir_modelos << " (" << modelo_ww << ")\n";
    DetectorPtr det(new Detector(dir_modelos.string(), modelo_ww));

    // Inicializa o PortAudio para escuta do microfone
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "[OWW ERRO] Falha ao inicializar PortAudio: " << Pa_GetErrorText(err) << "\n";
        return nullptr;
    }

    err = Pa_OpenDefaultStream(&det->st, 1, 0, paInt16, SR, CHUNK, nullptr, nullptr);
    if (err != paNoError) {
        std::cerr << "[OWW ERRO] Falha ao abrir stream do microfone: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return nullptr;
    }

    err = Pa_StartStream(det->st);
    if (err != paNoError) {
        std::cerr << "[OWW ERRO] Falha ao iniciar stream do microfone: " << Pa_GetErrorText(err) << "\n";
        Pa_CloseStream(det->st);
        Pa_Terminate();
        return nullptr;
    }

    return det;
}

StatusDeteccao detectar_passo(Detector& det, float limiar) {
    if (!det.st || Pa_IsStreamActive(det.st) != 1) {
        return {false, 0.0f, false};
    }

    int16_t buf[CHUNK];
    // Bloqueia até ler 1280 amostras (~80ms)
    PaError err = Pa_ReadStream(det.st, buf, CHUNK);
    if (err != paNoError && err != paInputOverflowed) {
        std::cerr << "[OWW ERRO] Erro ao ler microfone: " << Pa_GetErrorText(err) << "\n";
        return {false, 0.0f, false};
    }

    float p = det.feed(buf);

    // Se estiver em período de cooldown (resfriamento), não dispara
    if (det.cooldown > 0) {
        det.cooldown--;
        return {false, p, true};
    }

    // Se a confiança ultrapassar o limiar configurado (ex: 0.5)
    if (p > limiar) {
        det.cooldown = 25; // 25 chunks * 80ms ≈ 2 segundos de pausa
        return {true, p, false};
    }

    return {false, p, false};
}

bool detectar_wakeword(Detector& det, float limiar) {
    return detectar_passo(det, limiar).detectado;
}

void pausar_escuta(Detector& det) {
    if (det.st) {
        Pa_StopStream(det.st);
        Pa_CloseStream(det.st);
        det.st = nullptr;
    }
}

void retomar_escuta(Detector& det) {
    det.reset();
    if (!det.st) {
        PaError err = Pa_OpenDefaultStream(&det.st, 1, 0, paInt16, SR, CHUNK, nullptr, nullptr);
        if (err == paNoError) {
            Pa_StartStream(det.st);
        }
    }
}

// ============================================================================
// Modo Standalone (Executável de teste caso compilado diretamente)
// ============================================================================
#ifndef OWW_AS_LIB
int main(int argc, char* argv[]) {
    std::string modelo = (argc >= 2) ? argv[1] : "alexa_v0.1.onnx";
    std::string dir = (argc >= 3) ? argv[2] : "";

    auto det = inicializar_detector(modelo, dir);
    if (!det) {
        std::cerr << "Falha na inicialização do detector!\n";
        return 1;
    }

    std::cerr << "Escutando wake word...\n";
    while (true) {
        StatusDeteccao st = detectar_passo(*det);
        if (st.detectado) {
            std::cout << "WAKE " << st.probabilidade << std::endl;
        }
    }
    return 0;
}
#endif
