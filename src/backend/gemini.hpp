// gemini.hpp: processamento semântico (texto transcrito -> comando JSON) via Gemini, em C++.
// Equivalente em C++ do script Python com google-genai.
//
// Dependências: libcurl (HTTP) e nlohmann/json (somente header).
#pragma once

#include <nlohmann/json.hpp>

#include <string>

struct ConfigGemini {
    std::string url_base       = "https://generativelanguage.googleapis.com/v1beta/models";
    std::string modelo         = "gemini-3.5-flash-lite";
    std::string thinking_level = "MINIMAL";   // MINIMAL = menor latência (como no script Python)
    bool injetar_data_hora     = true;        // acrescenta data/hora atuais ao prompt (necessário para a agenda)
    int timeout_s              = 20;
};

// JSON que preserva a ordem das chaves (tipo_comando antes de conteudo), como um dict do Python.
// O despatcher pode continuar usando nlohmann::json normal para ler os arquivos.
using Comando = nlohmann::ordered_json;

struct ResultadoGemini {
    Comando comando;          // sempre preenchido; em caso de erro, é o comando de contingência
    bool ok = false;          // false = a API falhou e 'comando' é o de contingência
    std::string erro;         // motivo da falha (vazio se ok)
};

// Chave da API: a variável de ambiente GEMINI_API_KEY tem prioridade; senão lê o arquivo.
// Lança std::runtime_error se não encontrar.
//std::string carregar_chave(const std::string& arquivo = "API/gemini_api.txt");

// Envia o texto ao Gemini e devolve o comando estruturado. Nunca lança exceção:
// em qualquer falha, devolve ok=false e o comando de contingência (CONVERSA_GERAL),
// como o script Python faz.
ResultadoGemini processar_comando_voz(const ConfigGemini& cfg, const std::string& chave,
                                      const std::string& texto_transcrito);

// Grava o comando em <pasta>/comando_<milissegundos>.json (UTF-8, indentado) e devolve o caminho.
// A escrita é atômica (arquivo temporário + rename), então quem lê a pasta nunca vê JSON pela metade.
// Lança std::runtime_error em falha de escrita.
std::string salvar_comando(const Comando& comando, const std::string& pasta = "Comandos");
