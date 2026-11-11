#include "gemini.hpp"

#include <curl/curl.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// Mesmo texto do script Python (regras 1 a 10).
const char* SYSTEM_INSTRUCTION = R"instr(
Você é o cérebro de um assistente residencial autônomo. Sua função é converter a fala do usuário (transcrita por STT) em uma intenção estruturada em JSON.

Regras de Mapeamento:
1. Buscar clima/tempo -> tipo_comando: "CLIMA"
2. Responder qual dia é hoje -> tipo_comando: "DATA_ATUAL"
3. Agendar tarefas/eventos -> tipo_comando: "AGENDAR_TAREFA" (extraia descricao_evento e data_evento)
4. Falar eventos agendados -> tipo_comando: "LISTAR_TAREFAS"
5. Acionar lâmpada/relé -> tipo_comando: "ACIONAR_LAMPADA" (estado_lampada: "ligar" ou "desligar")
6. Anotar lista de compras -> tipo_comando: "ADICIONAR_COMPRAS" (item_compra)
7. Falar lista de compras -> tipo_comando: "LISTAR_COMPRAS"
8. Outros assuntos -> tipo_comando: "CONVERSA_GERAL" (forneça a resposta em conteudo.resposta_texto de forma sucinta para voz).
9. Sempre retorne um JSON válido, mesmo que a entrada seja confusa ou irreconhecível.
10. Nunca retorne texto fora do JSON. Sempre siga o schema definido.
)instr";

std::string trim(const std::string& s) {
    const char* esp = " \t\r\n";
    const size_t a = s.find_first_not_of(esp);
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(esp);
    return s.substr(a, b - a + 1);
}

// Regras 11 e 12 (data/hora atuais e formato ISO), acrescentadas à instrução original.
// Sem a data atual o modelo não consegue resolver "amanhã" ou "sexta".
std::string instrucao_do_sistema(bool injetar_data_hora) {
    std::string s = SYSTEM_INSTRUCTION;
    if (!injetar_data_hora) return s;

    static const char* DIAS[] = {"domingo", "segunda-feira", "terça-feira", "quarta-feira",
                                 "quinta-feira", "sexta-feira", "sábado"};  // tm_wday: 0 = domingo
    const std::time_t agora = std::time(nullptr);
    std::tm lt{};
    localtime_r(&agora, &lt);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &lt);

    s += std::string("11. Data e hora atuais: ") + buf + " (" + DIAS[lt.tm_wday] +
         "). Resolva datas relativas (\"amanhã\", \"sexta\", \"daqui a 3 dias\") a partir disso.\n";
    s += "12. data_evento deve estar em ISO 8601: \"YYYY-MM-DDTHH:MM\" se houver horário, ou "
         "\"YYYY-MM-DD\" se não houver. Se o usuário não informar data, omita o campo.\n";
    return s;
}

// Schema de saída: enumeração de ações + dados (igual ao RESPONSE_SCHEMA do Python).
json criar_schema() {
    return json{
        {"type", "OBJECT"},
        {"required", json::array({"tipo_comando", "conteudo"})},
        {"properties",
         {{"tipo_comando",
           {{"type", "STRING"},
            {"enum", json::array({"CLIMA", "DATA_ATUAL", "AGENDAR_TAREFA", "LISTAR_TAREFAS",
                                  "ACIONAR_LAMPADA", "ADICIONAR_COMPRAS", "LISTAR_COMPRAS",
                                  "CONVERSA_GERAL"})},
            {"description", "A categoria exata da ação que o assistente deve executar."}}},
          {"conteudo",
           {{"type", "OBJECT"},
            {"description", "Parâmetros específicos necessários para executar a ação."},
            {"properties",
             {{"estado_lampada",
               {{"type", "STRING"}, {"enum", json::array({"ligar", "desligar"})}}},
              {"item_compra", {{"type", "STRING"}}},
              {"descricao_evento", {{"type", "STRING"}}},
              {"data_evento", {{"type", "STRING"}}},
              {"resposta_texto",
               {{"type", "STRING"},
                {"description", "Resposta em texto para o Piper TTS sintetizar"}}}}}}}}}};
}

json montar_requisicao(const ConfigGemini& cfg, const std::string& texto) {
    json corpo;
    corpo["systemInstruction"] = {
        {"parts", json::array({json{{"text", instrucao_do_sistema(cfg.injetar_data_hora)}}})}};
    corpo["contents"] = json::array(
        {json{{"role", "user"}, {"parts", json::array({json{{"text", texto}}})}}});
    corpo["generationConfig"] = {
        {"responseMimeType", "application/json"},
        {"responseSchema", criar_schema()}};
    if (!cfg.thinking_level.empty())   // nível vazio: não envia o campo (usa o padrão do modelo)
        corpo["generationConfig"]["thinkingConfig"] = {{"thinkingLevel", cfg.thinking_level}};
    return corpo;
}

// Comando devolvido quando a API falha (mesmo do script Python).
Comando comando_contingencia() {
    return Comando{{"tipo_comando", "CONVERSA_GERAL"},
                   {"conteudo",
                    {{"resposta_texto", "Desculpe, tive um problema ao processar seu comando."}}}};
}

// --- libcurl ---
size_t acumular(char* ptr, size_t size, size_t nmemb, void* userdata) {
    static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}
struct CurlGuarda {
    CURL* c;
    ~CurlGuarda() { curl_easy_cleanup(c); }
};
struct ListaGuarda {
    curl_slist* l = nullptr;
    ~ListaGuarda() { curl_slist_free_all(l); }
};

// POST de JSON. A chave vai no cabeçalho x-goog-api-key (e não na URL), para não
// aparecer em logs, histórico ou mensagens de erro do curl. Devolve o corpo da resposta.
std::string post_json(const std::string& url, const std::string& chave, const std::string& corpo,
                      int timeout_s, long& http) {
    static const CURLcode init = curl_global_init(CURL_GLOBAL_DEFAULT);  // uma vez só
    if (init != CURLE_OK) throw std::runtime_error("curl_global_init falhou");

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init falhou");
    CurlGuarda g_curl{curl};

    ListaGuarda cab;
    cab.l = curl_slist_append(cab.l, "Content-Type: application/json");
    cab.l = curl_slist_append(cab.l, ("x-goog-api-key: " + chave).c_str());

    std::string resposta;
    char erro_curl[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, corpo.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(corpo.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cab.l);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, acumular);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resposta);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, erro_curl);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_s));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string msg = std::string("falha de rede: ") + curl_easy_strerror(res);
        if (erro_curl[0]) msg += std::string(" (") + erro_curl + ")";
        throw std::runtime_error(msg);
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    return resposta;
}

// Extrai a mensagem de um erro no formato {"error":{"message":"..."}}; senão, um trecho do corpo.
std::string mensagem_de_erro(const std::string& corpo) {
    try {
        const json j = json::parse(corpo);
        if (j.contains("error") && j["error"].is_object() && j["error"].contains("message") &&
            j["error"]["message"].is_string())
            return j["error"]["message"].get<std::string>();
    } catch (...) {
    }
    std::string t = trim(corpo);
    if (t.size() > 300) t = t.substr(0, 300) + "...";
    return t;
}

// Do corpo da resposta da API até o comando validado.
Comando extrair_comando(const std::string& corpo) {
    const json r = json::parse(corpo);

    if (r.contains("promptFeedback") && r["promptFeedback"].is_object() &&
        r["promptFeedback"].contains("blockReason"))
        throw std::runtime_error("pedido bloqueado pela API (blockReason)");

    if (!r.contains("candidates") || !r["candidates"].is_array() || r["candidates"].empty())
        throw std::runtime_error("resposta sem candidates");
    const json& c0 = r["candidates"][0];

    // Concatena as partes de texto (ignora partes de "pensamento", se houver), como o response.text do SDK.
    std::string texto;
    if (c0.contains("content") && c0["content"].is_object() && c0["content"].contains("parts") &&
        c0["content"]["parts"].is_array()) {
        for (const auto& p : c0["content"]["parts"]) {
            if (!p.is_object() || !p.contains("text") || !p["text"].is_string()) continue;
            if (p.contains("thought") && p["thought"].is_boolean() && p["thought"].get<bool>())
                continue;
            texto += p["text"].get<std::string>();
        }
    }
    if (texto.empty()) {
        std::string motivo = c0.contains("finishReason") && c0["finishReason"].is_string()
                                 ? c0["finishReason"].get<std::string>()
                                 : "desconhecido";
        throw std::runtime_error("resposta sem texto (finishReason: " + motivo + ")");
    }

    Comando cmd = Comando::parse(texto);
    if (!cmd.is_object() || !cmd.contains("tipo_comando") || !cmd["tipo_comando"].is_string() ||
        !cmd.contains("conteudo") || !cmd["conteudo"].is_object())
        throw std::runtime_error("JSON fora do schema esperado: " + texto.substr(0, 200));
    return cmd;
}

}  // namespace

// ---------------------------------------------------------------------------

std::string carregar_chave(const std::string& arquivo) {
    if (const char* env = std::getenv("GEMINI_API_KEY")) {
        const std::string k = trim(env);
        if (!k.empty()) return k;
    }
    std::ifstream f(arquivo);
    if (!f)
        throw std::runtime_error("GEMINI_API_KEY não definida e arquivo '" + arquivo +
                                 "' não encontrado");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string chave = trim(ss.str());
    if (chave.empty()) throw std::runtime_error("arquivo da chave está vazio: " + arquivo);
    return chave;
}

ResultadoGemini processar_comando_voz(const ConfigGemini& cfg, const std::string& chave,
                                      const std::string& texto_transcrito) {
    ResultadoGemini r;
    r.comando = comando_contingencia();
    try {
        const std::string texto = trim(texto_transcrito);   // ex.: arquivo de texto com \n no final
        if (texto.empty()) throw std::runtime_error("texto transcrito vazio");

        const json corpo = montar_requisicao(cfg, texto);
        const std::string url = cfg.url_base + "/" + cfg.modelo + ":generateContent";

        long http = 0;
        // error_handler replace: texto com UTF-8 inválido não derruba o programa
        const std::string resposta =
            post_json(url, chave, corpo.dump(-1, ' ', false, json::error_handler_t::replace),
                      cfg.timeout_s, http);
        if (http != 200)
            throw std::runtime_error("HTTP " + std::to_string(http) + ": " + mensagem_de_erro(resposta));

        r.comando = extrair_comando(resposta);
        r.ok = true;
    } catch (const std::exception& e) {
        r.erro = e.what();
    }
    return r;
}

std::string salvar_comando(const Comando& comando, const std::string& pasta) {
    std::error_code ec;
    fs::create_directories(pasta, ec);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    fs::path destino;
    do {   // nunca sobrescreve um arquivo existente (dois comandos no mesmo milissegundo)
        destino = fs::path(pasta) / ("comando_" + std::to_string(ms++) + ".json");
    } while (fs::exists(destino));
    const fs::path temporario = fs::path(destino).concat(".tmp");  // extensão .tmp: ignorado por quem lista *.json

    {
        std::ofstream f(temporario);
        if (!f) throw std::runtime_error("não foi possível gravar " + temporario.string());
        f << comando.dump(2) << "\n";   // UTF-8 preservado (equivale a ensure_ascii=False)
        if (!f) throw std::runtime_error("erro de escrita em " + temporario.string());
    }
    fs::rename(temporario, destino, ec);
    if (ec) throw std::runtime_error("não foi possível renomear para " + destino.string() + ": " + ec.message());
    return destino.string();
}
