// g++ -O2 -std=c++17 -Wall despachante.cpp -o despachante
//
// Uso:
//   ./despachante                       -> processa o .json mais recente de ./Comandos
//   ./despachante Comandos/             -> idem, em outra pasta
//   ./despachante Comandos/comando_X.json
//
// Códigos de saída: 0 ok | 1 arquivo/pasta | 2 JSON inválido | 3 comando inválido
// Logs vão para stderr; a saída das ações vai para stdout.

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ======================================================================
// AÇÕES: implemente o corpo de cada função (GPIO, SQLite, Piper TTS...)
// ======================================================================
void acao_acionar_lampada(bool ligar) {
    std::cout << "[ACAO] lampada " << (ligar ? "LIGADA" : "DESLIGADA") << "\n";
}
void acao_adicionar_compra(const std::string& item) {
    std::cout << "[ACAO] adicionar a lista de compras: " << item << "\n";
}
void acao_listar_compras() {
    std::cout << "[ACAO] falar a lista de compras\n";
}
void acao_agendar_tarefa(const std::string& descricao, const std::string& data) {
    std::cout << "[ACAO] agendar: \"" << descricao << "\" em \""
              << (data.empty() ? "(sem data)" : data) << "\"\n";
}
void acao_listar_tarefas() {
    std::cout << "[ACAO] falar tarefas/eventos agendados\n";
}
void acao_informar_clima() {
    std::cout << "[ACAO] buscar e falar o clima\n";
}
void acao_informar_data_atual() {
    std::cout << "[ACAO] falar a data de hoje\n";
}
void acao_responder(const std::string& texto) {
    std::cout << "[ACAO] falar (TTS): " << texto << "\n";
}

// ======================================================================
// Mapeamento tipo_comando (string do JSON) -> enum
// ======================================================================
enum class Tipo {
    Clima, DataAtual, AgendarTarefa, ListarTarefas,
    AcionarLampada, AdicionarCompras, ListarCompras, ConversaGeral
};

static const std::unordered_map<std::string, Tipo> TIPOS = {
    {"CLIMA",             Tipo::Clima},
    {"DATA_ATUAL",        Tipo::DataAtual},
    {"AGENDAR_TAREFA",    Tipo::AgendarTarefa},
    {"LISTAR_TAREFAS",    Tipo::ListarTarefas},
    {"ACIONAR_LAMPADA",   Tipo::AcionarLampada},
    {"ADICIONAR_COMPRAS", Tipo::AdicionarCompras},
    {"LISTAR_COMPRAS",    Tipo::ListarCompras},
    {"CONVERSA_GERAL",    Tipo::ConversaGeral},
};

// Lê um campo string de um objeto; devolve "" se ausente ou de outro tipo.
static std::string campo_str(const json& obj, const char* chave) {
    auto it = obj.find(chave);
    if (it == obj.end() || !it->is_string()) return "";
    return it->get<std::string>();
}

static int erro_comando(const std::string& msg) {
    std::cerr << "[ERRO] " << msg << "\n";
    return 3;
}

// ======================================================================
// Despachante: valida o JSON e chama a ação correspondente
// ======================================================================
static int despachar(const json& raiz) {
    if (!raiz.is_object())
        return erro_comando("o JSON raiz não é um objeto");

    auto it_tipo = raiz.find("tipo_comando");
    if (it_tipo == raiz.end() || !it_tipo->is_string())
        return erro_comando("campo 'tipo_comando' ausente ou não é string");

    const std::string nome = it_tipo->get<std::string>();
    auto t = TIPOS.find(nome);
    if (t == TIPOS.end())
        return erro_comando("tipo_comando desconhecido: " + nome);

    // O schema do Gemini não obriga os campos de 'conteudo' por tipo,
    // então cada caso valida o que precisa.
    static const json vazio = json::object();
    auto it_c = raiz.find("conteudo");
    const json& c = (it_c != raiz.end() && it_c->is_object()) ? *it_c : vazio;

    std::cerr << "[Info] tipo_comando = " << nome << "\n";

    switch (t->second) {
        case Tipo::AcionarLampada: {
            const std::string estado = campo_str(c, "estado_lampada");
            if (estado == "ligar")         acao_acionar_lampada(true);
            else if (estado == "desligar") acao_acionar_lampada(false);
            else return erro_comando("estado_lampada ausente ou inválido: '" + estado + "'");
            break;
        }
        case Tipo::AdicionarCompras: {
            const std::string item = campo_str(c, "item_compra");
            if (item.empty()) return erro_comando("item_compra ausente");
            acao_adicionar_compra(item);
            break;
        }
        case Tipo::AgendarTarefa: {
            const std::string desc = campo_str(c, "descricao_evento");
            if (desc.empty()) return erro_comando("descricao_evento ausente");
            acao_agendar_tarefa(desc, campo_str(c, "data_evento"));  // data pode vir vazia
            break;
        }
        case Tipo::ConversaGeral: {
            const std::string txt = campo_str(c, "resposta_texto");
            if (txt.empty()) return erro_comando("resposta_texto ausente");
            acao_responder(txt);
            break;
        }
        case Tipo::ListarCompras:  acao_listar_compras();      break;
        case Tipo::ListarTarefas:  acao_listar_tarefas();      break;
        case Tipo::Clima:          acao_informar_clima();      break;
        case Tipo::DataAtual:      acao_informar_data_atual(); break;
    }
    return 0;
}

// ======================================================================
// Localização do arquivo
// ======================================================================
static std::optional<fs::path> arquivo_mais_recente(const fs::path& dir) {
    if (!fs::is_directory(dir)) return std::nullopt;

    std::optional<fs::path> melhor;
    fs::file_time_type t_melhor;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file() || e.path().extension() != ".json") continue;
        auto t = e.last_write_time();
        if (!melhor || t > t_melhor) { melhor = e.path(); t_melhor = t; }
    }
    return melhor;
}

int main(int argc, char** argv) {
    fs::path alvo = (argc >= 2) ? fs::path(argv[1]) : fs::path("Comandos");

    if (fs::is_directory(alvo)) {
        auto r = arquivo_mais_recente(alvo);
        if (!r) {
            std::cerr << "[ERRO] nenhum .json em " << alvo << "\n";
            return 1;
        }
        alvo = *r;
    }

    std::ifstream f(alvo);
    if (!f) {
        std::cerr << "[ERRO] não foi possível abrir " << alvo << "\n";
        return 1;
    }

    // 3º argumento false = não lançar exceção; devolve 'discarded' se inválido.
    json raiz = json::parse(f, nullptr, false);
    if (raiz.is_discarded()) {
        std::cerr << "[ERRO] JSON inválido em " << alvo << "\n";
        return 2;
    }

    std::cerr << "[Info] lendo " << alvo << "\n";
    return despachar(raiz);
}