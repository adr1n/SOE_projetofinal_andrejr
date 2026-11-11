#include "stt.hpp"

#include <curl/curl.h>
#include <portaudio.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    const char* esp = " \t\r\n";
    const size_t a = s.find_first_not_of(esp);
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(esp);
    return s.substr(a, b - a + 1);
}

// Pa_Initialize / Pa_Terminate pareados (RAII), mesmo se uma exceção for lançada.
struct PaSessao {
    PaSessao() {
        const PaError e = Pa_Initialize();
        if (e != paNoError)
            throw ErroAudio(std::string("Pa_Initialize: ") + Pa_GetErrorText(e));
    }
    ~PaSessao() { Pa_Terminate(); }
};

// Para e fecha a stream ao sair do escopo.
struct PaStreamGuarda {
    PaStream* s = nullptr;
    ~PaStreamGuarda() {
        if (s) {
            Pa_StopStream(s);
            Pa_CloseStream(s);
        }
    }
};

// --- helpers de WAV (little-endian explícito, independente da arquitetura) ---
void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF));
}
void put4cc(std::vector<uint8_t>& v, const char* s) { v.insert(v.end(), s, s + 4); }

// --- callback do libcurl: acumula o corpo da resposta numa string ---
size_t acumular(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

struct CurlGuarda {
    CURL* c;
    ~CurlGuarda() { curl_easy_cleanup(c); }
};
struct MimeGuarda {
    curl_mime* m;
    ~MimeGuarda() { curl_mime_free(m); }
};
struct ListaGuarda {
    curl_slist* l = nullptr;
    ~ListaGuarda() { curl_slist_free_all(l); }
};

}  // namespace

// ---------------------------------------------------------------------------

std::string ler_chave(const std::string& caminho) {
    std::ifstream f(caminho);
    if (!f) throw std::runtime_error("não foi possível abrir o arquivo da chave: " + caminho);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string chave = trim(ss.str());
    if (chave.empty()) throw std::runtime_error("arquivo da chave está vazio: " + caminho);
    return chave;
}

std::vector<int16_t> gravar_audio(const ConfigSTT& cfg, double duracao_s) {
    if (duracao_s <= 0) throw ErroAudio("duração de gravação inválida");

    PaSessao pa;

    PaStreamParameters in{};
    in.device = (cfg.dispositivo >= 0) ? cfg.dispositivo : Pa_GetDefaultInputDevice();
    if (in.device == paNoDevice) throw ErroAudio("nenhum dispositivo de entrada de áudio encontrado");

    const PaDeviceInfo* info = Pa_GetDeviceInfo(in.device);
    if (!info || info->maxInputChannels < 1)
        throw ErroAudio("dispositivo " + std::to_string(in.device) + " inválido ou sem entrada");

    in.channelCount = 1;
    in.sampleFormat = paInt16;
    in.suggestedLatency = info->defaultLowInputLatency;
    in.hostApiSpecificStreamInfo = nullptr;

    PaStreamGuarda st;
    PaError e = Pa_OpenStream(&st.s, &in, nullptr, cfg.taxa, 1024, paClipOff, nullptr, nullptr);
    if (e != paNoError) {
        st.s = nullptr;
        throw ErroAudio(std::string("Pa_OpenStream: ") + Pa_GetErrorText(e));
    }

    e = Pa_StartStream(st.s);
    if (e != paNoError) throw ErroAudio(std::string("Pa_StartStream: ") + Pa_GetErrorText(e));

    const size_t total = static_cast<size_t>(duracao_s * cfg.taxa);
    std::vector<int16_t> pcm(total);
    size_t lido = 0;
    while (lido < total) {
        const size_t n = std::min<size_t>(1024, total - lido);
        e = Pa_ReadStream(st.s, pcm.data() + lido, n);
        // overflow de entrada não é fatal: o áudio continua válido
        if (e != paNoError && e != paInputOverflowed)
            throw ErroAudio(std::string("Pa_ReadStream: ") + Pa_GetErrorText(e));
        lido += n;
    }
    return pcm;
}

double rms(const std::vector<int16_t>& pcm) {
    if (pcm.empty()) return 0.0;
    double acc = 0.0;
    for (int16_t s : pcm) acc += static_cast<double>(s) * s;
    return std::sqrt(acc / static_cast<double>(pcm.size()));
}

std::vector<uint8_t> montar_wav(const std::vector<int16_t>& pcm, int taxa) {
    const uint32_t bytes_dados = static_cast<uint32_t>(pcm.size() * 2);
    std::vector<uint8_t> w;
    w.reserve(44 + bytes_dados);

    put4cc(w, "RIFF");
    put32(w, 36 + bytes_dados);                       // tamanho do resto do arquivo
    put4cc(w, "WAVE");
    put4cc(w, "fmt ");
    put32(w, 16);                                     // tamanho do bloco fmt (PCM)
    put16(w, 1);                                      // formato 1 = PCM
    put16(w, 1);                                      // 1 canal (mono)
    put32(w, static_cast<uint32_t>(taxa));            // taxa de amostragem
    put32(w, static_cast<uint32_t>(taxa) * 2);        // bytes por segundo
    put16(w, 2);                                      // bytes por frame (1 canal x 2 bytes)
    put16(w, 16);                                     // bits por amostra
    put4cc(w, "data");
    put32(w, bytes_dados);
    for (int16_t s : pcm) put16(w, static_cast<uint16_t>(s));
    return w;
}

std::string transcrever(const ConfigSTT& cfg, const std::string& chave,
                        const std::vector<uint8_t>& wav) {
    static const CURLcode init = curl_global_init(CURL_GLOBAL_DEFAULT);  // uma vez só
    if (init != CURLE_OK) throw ErroAPI("curl_global_init falhou");

    CURL* curl = curl_easy_init();
    if (!curl) throw ErroAPI("curl_easy_init falhou");
    CurlGuarda g_curl{curl};

    // Corpo multipart/form-data montado na memória (nada é gravado no cartão SD)
    curl_mime* mime = curl_mime_init(curl);
    MimeGuarda g_mime{mime};

    auto campo = [&](const char* nome, const std::string& valor) {
        curl_mimepart* p = curl_mime_addpart(mime);
        curl_mime_name(p, nome);
        curl_mime_data(p, valor.c_str(), CURL_ZERO_TERMINATED);
    };
    campo("model", cfg.modelo);
    campo("language", cfg.idioma);   // força a transcrição em português
    campo("response_format", "text");

    curl_mimepart* arq = curl_mime_addpart(mime);
    curl_mime_name(arq, "file");
    curl_mime_filename(arq, "comando.wav");   // a API exige um nome de arquivo
    curl_mime_type(arq, "audio/wav");
    curl_mime_data(arq, reinterpret_cast<const char*>(wav.data()), wav.size());

    ListaGuarda cab;
    cab.l = curl_slist_append(cab.l, ("Authorization: Bearer " + chave).c_str());

    std::string corpo;
    char erro_curl[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, cfg.url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cab.l);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, acumular);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &corpo);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, erro_curl);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(cfg.timeout_s));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string msg = std::string("falha de rede: ") + curl_easy_strerror(res);
        if (erro_curl[0]) msg += std::string(" (") + erro_curl + ")";
        throw ErroAPI(msg);
    }

    long codigo = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &codigo);
    if (codigo != 200) {
        std::string trecho = trim(corpo);
        if (trecho.size() > 300) trecho = trecho.substr(0, 300) + "...";
        throw ErroAPI("HTTP " + std::to_string(codigo) + ": " + trecho);
    }
    return trim(corpo);
}

void listar_dispositivos() {
    PaSessao pa;
    int n = Pa_GetDeviceCount();
    if (n < 0) n = 0;
    const int padrao = Pa_GetDefaultInputDevice();

    std::cout << "Dispositivos de entrada:\n";
    bool algum = false;
    for (int i = 0; i < n; i++) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
        if (!d || d->maxInputChannels < 1) continue;
        algum = true;
        std::cout << "  [" << i << "] " << d->name << "  (" << d->maxInputChannels
                  << " canais, " << d->defaultSampleRate << " Hz)"
                  << (i == padrao ? "  <- padrão" : "") << "\n";
    }
    if (!algum) std::cout << "  (nenhum)\n";
}
