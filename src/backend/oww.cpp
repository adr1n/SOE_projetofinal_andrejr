#include <onnxruntime_cxx_api.h>
#include <portaudio.h>
#include <algorithm>
#include <array>
#include <deque>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;
constexpr int SR = 16000, CHUNK = 1280, CTX = 480;

// Pasta onde o executável está (Linux: /proc/self/exe é um link para ele).
static fs::path pasta_do_executavel() {
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
}

static Ort::SessionOptions opts() {
    Ort::SessionOptions o;
    o.SetIntraOpNumThreads(1);
    return o;
}

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

struct Detector {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "oww"};
    Ort::Session mel, emb, clf;
    std::vector<float> raw = std::vector<float>(CHUNK + CTX, 0.f);
    std::deque<std::array<float, 32>> melbuf;
    std::deque<std::array<float, 96>> embbuf;

    Detector(const std::string& d, const std::string& ww)
        : mel(env, (d + "/melspectrogram.onnx").c_str(), opts()),
          emb(env, (d + "/embedding_model.onnx").c_str(), opts()),
          clf(env, (d + "/" + ww).c_str(), opts()) {}

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

int main(int argc, char* argv[]) {

    // 1) OWW_MODELS, se definida; 2) senão, <pasta do executável>/models
    fs::path dir_modelos = std::getenv("OWW_MODELS")
                               ? fs::path(std::getenv("OWW_MODELS"))
                               : pasta_do_executavel() / "models";
    // Nome do modelo da wake word: 1º argumento, ou alexa por padrão
    std::string modelo = (argc >= 2) ? argv[1] : "alexa_v0.1.onnx";

    // Falha cedo, com mensagem clara, se faltar algum arquivo
    for (const fs::path& f : {dir_modelos / "melspectrogram.onnx",
                              dir_modelos / "embedding_model.onnx",
                              dir_modelos / modelo}) {
        if (!fs::exists(f)) {
            std::cerr << "[ERRO] modelo não encontrado: " << f << "\n";
            return 1;
        }
    }
    Detector det(std::string(getenv("HOME")) + "/Testes/testeWakeWord/models",
                 "alexa_v0.1.onnx");

    Pa_Initialize();
    PaStream* st;
    Pa_OpenDefaultStream(&st, 1, 0, paInt16, SR, CHUNK, nullptr, nullptr);
    Pa_StartStream(st);

    int16_t buf[CHUNK];
    int cooldown = 0;
    std::cerr << "Escutando...\n";
    while (true) {
        Pa_ReadStream(st, buf, CHUNK);
        float p = det.feed(buf);
        if (cooldown > 0) { cooldown--; continue; }
        if (p > 0.5f) {
            std::cout << "WAKE " << p << std::endl;   // aqui: grava comando / chama gemini_cli
            cooldown = 25;                           // ~2 s sem redisparar
        }
    }
}
