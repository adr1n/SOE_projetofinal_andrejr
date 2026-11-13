#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp> // Certifique-se de ter o header no projeto

using json = nlohmann::json;

// Função de callback para capturar a resposta HTTP do Telegram
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

// Função para enviar mensagem (equivalente ao curl -X POST ... /sendMessage)
bool enviarMensagemTelegram(const std::string& token, const std::string& chatId, const std::string& texto) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(!curl) {
        std::cerr << "[Erro]: Falha ao inicializar o cURL." << std::endl;
        return false;
    }

    // Monta o endpoint do Telegram
    std::string url = "https://api.telegram.org/bot" + token + "/sendMessage";

    // Monta o JSON idêntico ao do seu script bash: {"chat_id": "...", "text": "...", "disable_notification": true}
    json payload = {
        {"chat_id", chatId},
        {"text", texto},
        {"disable_notification", true}
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

    std::cout << "[Telegram Resposta]: " << readBuffer << std::endl;
    return true;
}

// Função para checar atualizações (equivalente ao curl ... /getUpdates)
void conferirAtualizacoes(const std::string& token) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if(curl) {
        std::string url = "https://api.telegram.org/bot" + token + "/getUpdates";

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        std::cout << "\n[Atualizações do Bot]:\n" << readBuffer << std::endl;
    }
}

int main() {
    std::string token = std::getenv("TELEGRAM_TOKEN");
    std::string chatId = std::getenv("TELEGRAM_CHATID");

    std::cout << "Enviando mensagem via API do Telegram em C++...\n";
    enviarMensagemTelegram(token, chatId, "Olá! Esta mensagem foi enviada direto do código C++ do assistente.");

    std::cout << "\nConferindo atualizações...\n";
    conferirAtualizacoes(token);

    return 0;
}
