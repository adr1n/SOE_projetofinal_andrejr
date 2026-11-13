#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Função auxiliar simples para decodificar Base64 e salvar como binário (WAV)
// (Base64 decode padrão em C++)
static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

std::vector<unsigned char> base64_decode(const std::string& encoded_string) {
    int in_len = encoded_string.size();
    int i = 0, j = 0, in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::vector<unsigned char> ret;

    while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++)
            ret.push_back(char_array_3[j]);
    }

    return ret;
}

// Callback do cURL
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

bool gerarAudioGoogleTTS(const std::string& texto, const std::string& apiKey, const std::string& outputFile) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(!curl) {
        std::cerr << "[Erro]: Falha ao inicializar o cURL." << std::endl;
        return false;
    }

    std::string url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + apiKey;

    json payload = {
        {"input", {{"text", texto}}},
        {"voice", {
            {"languageCode", "pt-BR"},
            {"name", "pt-BR-Neural2-B"},
            {"ssmlGender", "MALE"}
        }},
        {"audioConfig", {
            {"audioEncoding", "LINEAR16"}
        }}
    };

    std::string jsonStr = payload.dump();

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    res = curl_easy_perform(curl);

    if(res != CURLE_OK) {
        std::cerr << "[Erro cURL]: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return false;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    try {
        json responseJson = json::parse(readBuffer);
        
        if (responseJson.contains("audioContent")) {
            std::string audioBase64 = responseJson["audioContent"];
            
            // Decodifica o Base64 recebido para bytes binários do WAV
            std::vector<unsigned char> audioBytes = base64_decode(audioBase64);

            // Salva no arquivo de saída .wav
            std::ofstream outfile(outputFile, std::ios::binary);
            if (!outfile.is_open()) {
                std::cerr << "[Erro]: Não foi possível criar o arquivo de áudio em: " << outputFile << std::endl;
                return false;
            }
            outfile.write((char*)audioBytes.data(), audioBytes.size());
            outfile.close();

            std::cout << "[TTS]: Áudio gerado e salvo com sucesso em " << outputFile << "!\n";
            return true;
        } else {
            std::cerr << "[Erro API Google]: " << readBuffer << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Erro JSON]: " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    // Uso esperado pelo script shell: ./piper/tts_manager <arquivo_texto.txt> <API_KEY> <saida.wav>
    

    std::string arquivoPath;
    std::string apiKey;
    std::string outputFile;
    if (argc < 4) {
        std::cerr << "Uso: " << argv[0] << " <arquivo_texto.txt> <API_KEY> <saida.wav>\n";
        return 1;
    }
    if(argc>1){
	arquivoPath = argv[1];
	apiKey = argv[2];
	outputFile = argv[3];
    }else{
	arquivoPath = "Texto/comando.txt";
        apiKey = std::getenv("GEMINI_API");
	outputFile = "Audio/saida.wav";
    }
    // Lê o texto que será transformado em áudio a partir do arquivo
    std::ifstream arquivo(arquivoPath);
    if (!arquivo.is_open()) {
        std::cerr << "[Erro]: Não foi possível abrir o arquivo de texto: " << arquivoPath << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << arquivo.rdbuf();
    std::string texto = buffer.str();
    arquivo.close();

    if (texto.empty()) {
        std::cout << "[TTS]: O arquivo de texto está vazio. Nada para sintetizar.\n";
        return 0;
    }

    std::cout << "[TTS]: Solicitando áudio para a API do Google Cloud...\n";
    if (!gerarAudioGoogleTTS(texto, apiKey, outputFile)) {
        return 1;
    }

    return 0;
}
