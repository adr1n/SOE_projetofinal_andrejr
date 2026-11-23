#include "tts_manager.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// Decodificador Base64 robusto (ignora espaços, quebras de linha e padding)
std::vector<unsigned char> base64_decode(const std::string& input) {
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> table(256, -1);
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(b64[i])] = i;

    std::vector<unsigned char> out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (table[c] == -1) continue;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Cria cabeçalho WAV padrão de 44 bytes (PCM 16-bit mono 24kHz) caso a API retorne PCM bruto
std::vector<unsigned char> criarHeaderWav(uint32_t dataSize, uint32_t sampleRate = 24000, uint16_t numChannels = 1, uint16_t bitsPerSample = 16) {
    std::vector<unsigned char> h(44);
    uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    uint32_t chunkSize = 36 + dataSize;

    // RIFF chunk descriptor
    h[0] = 'R'; h[1] = 'I'; h[2] = 'F'; h[3] = 'F';
    h[4] = chunkSize & 0xff;        h[5] = (chunkSize >> 8) & 0xff;
    h[6] = (chunkSize >> 16) & 0xff; h[7] = (chunkSize >> 24) & 0xff;
    h[8] = 'W'; h[9] = 'A'; h[10] = 'V'; h[11] = 'E';

    // "fmt " sub-chunk
    h[12] = 'f'; h[13] = 'm'; h[14] = 't'; h[15] = ' ';
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0; // Subchunk1Size = 16 para PCM
    h[20] = 1; h[21] = 0;                        // AudioFormat = 1 (PCM)
    h[22] = numChannels & 0xff; h[23] = (numChannels >> 8) & 0xff;
    h[24] = sampleRate & 0xff;        h[25] = (sampleRate >> 8) & 0xff;
    h[26] = (sampleRate >> 16) & 0xff; h[27] = (sampleRate >> 24) & 0xff;
    h[28] = byteRate & 0xff;          h[29] = (byteRate >> 8) & 0xff;
    h[30] = (byteRate >> 16) & 0xff;   h[31] = (byteRate >> 24) & 0xff;
    h[32] = blockAlign & 0xff;        h[33] = (blockAlign >> 8) & 0xff;
    h[34] = bitsPerSample & 0xff;     h[35] = (bitsPerSample >> 8) & 0xff;

    // "data" sub-chunk
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    h[40] = dataSize & 0xff;        h[41] = (dataSize >> 8) & 0xff;
    h[42] = (dataSize >> 16) & 0xff; h[43] = (dataSize >> 24) & 0xff;

    return h;
}

// Callback do cURL para concatenar a resposta HTTP
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Síntese de voz usando a API do Google Gemini (Generative Language API)
// Vozes disponíveis: "Puck", "Charon", "Aoede", "Fenrir", "Kore"
bool gerarAudioGeminiTTS(const std::string& texto, const std::string& apiKey, const std::string& outputFile, const std::string& voz) {
    static const CURLcode curl_init = curl_global_init(CURL_GLOBAL_DEFAULT);
    (void)curl_init;

    std::string key = apiKey.empty() ? obterChaveGemini() : apiKey;
    if (key.empty()) {
        std::cerr << "[Erro TTS]: Chave da API do Gemini não encontrada!\n";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Erro]: Falha ao inicializar o cURL." << std::endl;
        return false;
    }

    // Endpoint do Gemini para TTS (usa a mesma chave gratuita do Google AI Studio)
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash-lite-tts:generateContent";

    json payload = {
        {"contents", json::array({
            json{{"parts", json::array({
                json{{"text", texto}}
            })}}
        })},
        {"generationConfig", {
            {"responseModalities", json::array({"AUDIO"})},
            {"speechConfig", {
                {"voiceConfig", {
                    {"prebuiltVoiceConfig", {
                        {"voiceName", voz}
                    }}
                }}
            }}
        }}
    };

    std::string jsonStr = payload.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-goog-api-key: " + key).c_str());

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[Erro cURL]: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    if (http_code != 200) {
        std::cerr << "[Erro API Gemini - HTTP " << http_code << "]: " << readBuffer << std::endl;
        return false;
    }

    try {
        json responseJson = json::parse(readBuffer);
        if (responseJson.contains("candidates") && !responseJson["candidates"].empty()) {
            auto& candidate = responseJson["candidates"][0];
            if (candidate.contains("content") && candidate["content"].contains("parts")) {
                for (auto& part : candidate["content"]["parts"]) {
                    if (part.contains("inlineData") && part["inlineData"].contains("data")) {
                        std::string audioBase64 = part["inlineData"]["data"];
                        std::string mimeType = part["inlineData"].value("mimeType", "");
                        std::vector<unsigned char> audioBytes = base64_decode(audioBase64);

                        // Garante que a pasta pai exista antes de criar o arquivo
                        fs::path outPath(outputFile);
                        if (outPath.has_parent_path()) {
                            fs::create_directories(outPath.parent_path());
                        }

                        std::ofstream outfile(outputFile, std::ios::binary);
                        if (!outfile.is_open()) {
                            std::cerr << "[Erro]: Não foi possível criar o arquivo de áudio em: " << outputFile << std::endl;
                            return false;
                        }

                        // Se a resposta já tiver cabeçalho RIFF/WAVE, salva direto.
                        // Caso contrário (PCM bruto 24kHz), adiciona o cabeçalho WAV de 44 bytes.
                        bool temHeaderWav = (audioBytes.size() >= 4 &&
                                             audioBytes[0] == 'R' && audioBytes[1] == 'I' &&
                                             audioBytes[2] == 'F' && audioBytes[3] == 'F');

                        if (!temHeaderWav) {
                            auto wavHeader = criarHeaderWav(static_cast<uint32_t>(audioBytes.size()), 24000, 1, 16);
                            outfile.write(reinterpret_cast<const char*>(wavHeader.data()), wavHeader.size());
                        }

                        outfile.write(reinterpret_cast<const char*>(audioBytes.data()), audioBytes.size());
                        outfile.close();

                        std::cout << "[TTS]: Áudio gerado com sucesso (" << mimeType 
                                  << ", " << audioBytes.size() << " bytes) e salvo em " << outputFile << "!\n";
                        return true;
                    }
                }
            }
        }
        std::cerr << "[Erro]: Resposta da API não contém áudio válido: " << readBuffer << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[Erro JSON]: " << e.what() << std::endl;
        return false;
    }
}

// Tenta obter a chave do Gemini na seguinte ordem:
// 1. Argumento informado
// 2. Variável de ambiente GEMINI_API
// 3. Variável de ambiente GEMINI_API_KEY
// 4. Arquivo API/gemini_api.txt
std::string obterChaveGemini(const std::string& argChave) {
    if (!argChave.empty()) return argChave;

    if (const char* env = std::getenv("GEMINI_API")) {
        std::string k(env);
        if (!k.empty()) return k;
    }
    if (const char* env2 = std::getenv("GEMINI_API_KEY")) {
        std::string k(env2);
        if (!k.empty()) return k;
    }

    std::ifstream f("API/gemini_api.txt");
    if (f.is_open()) {
        std::stringstream ss;
        ss << f.rdbuf();
        std::string k = ss.str();
        while (!k.empty() && (k.back() == '\n' || k.back() == '\r' || k.back() == ' '))
            k.pop_back();
        if (!k.empty()) return k;
    }

    return "";
}

#ifndef TTS_AS_LIB
static void uso(const char* prog) {
    std::cerr << "Uso: " << prog << " [texto ou arquivo.txt] [chave_opcional] [saida.wav] [voz]\n"
              << "Opções de voz: Puck (padrão), Charon, Aoede, Fenrir, Kore\n"
              << "Exemplos:\n"
              << "  " << prog << " \"Lâmpada ligada\" \"\" Audio/saida.wav\n"
              << "  " << prog << " Texto/comando.txt \"\" Audio/saida.wav Aoede\n";
}

int main(int argc, char* argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        uso(argv[0]);
        return 0;
    }

    std::string entrada = (argc > 1 && std::string(argv[1]) != "") ? argv[1] : "Texto/comando.txt";
    std::string argChave = (argc > 2) ? argv[2] : "";
    std::string outputFile = (argc > 3 && std::string(argv[3]) != "") ? argv[3] : "Audio/saida.wav";
    std::string voz = (argc > 4 && std::string(argv[4]) != "") ? argv[4] : "Puck";

    std::string apiKey = obterChaveGemini(argChave);
    if (apiKey.empty()) {
        std::cerr << "[Erro]: Chave da API do Gemini não encontrada!\n"
                  << "Defina GEMINI_API, GEMINI_API_KEY, informe por argumento ou crie API/gemini_api.txt\n";
        return 1;
    }

    // Se a entrada for um arquivo existente no disco, lê seu conteúdo; caso contrário usa o texto diretamente.
    std::string texto;
    std::ifstream arquivo(entrada);
    if (arquivo.is_open()) {
        std::stringstream buffer;
        buffer << arquivo.rdbuf();
        texto = buffer.str();
    } else {
        texto = entrada;
    }

    // Remove espaços em branco nas bordas
    while (!texto.empty() && (texto.back() == '\n' || texto.back() == '\r' || texto.back() == ' '))
        texto.pop_back();

    if (texto.empty()) {
        std::cout << "[TTS]: Texto vazio. Nada para sintetizar.\n";
        return 0;
    }

    std::cout << "[TTS]: Solicitando áudio para a API do Gemini...\n";
    std::cout << "[TTS]: Texto: \"" << texto << "\"\n";
    std::cout << "[TTS]: Voz: " << voz << "\n";

    bool ok = gerarAudioGeminiTTS(texto, apiKey, outputFile, voz);

    curl_global_cleanup();
    return ok ? 0 : 1;
}
#endif
