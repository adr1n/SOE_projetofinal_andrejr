// teste_gemini.cpp: equivalente ao bloco `if __name__ == "__main__"` do script Python.
//
// Compilar:
//   g++ -O2 -std=c++17 -Wall -Wextra teste_gemini.cpp gemini.cpp -o teste_gemini -lcurl
//
// Uso:
//   ./teste_gemini                          roda as 5 frases de teste do script Python
//   ./teste_gemini "Anota leite na lista"   processa o texto informado
//   ./teste_gemini -f comando_voz.txt       lê o texto de um arquivo (saída do teste_stt)
//
// stdout: o JSON de cada comando. stderr: logs.
// Códigos de saída: 0 ok | 1 uso/chave/arquivo | 3 a API falhou em pelo menos um comando
//                   (nesse caso o comando de contingência foi usado, como no Python)

#include "gemini.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

static void uso() {
    std::cerr <<
        "Uso: teste_gemini [opções] [texto ...]\n"
        "  -f ARQUIVO      lê o texto a processar de um arquivo\n"
        "  -k ARQUIVO      arquivo da chave (padrão API/gemini_api.txt; GEMINI_API_KEY tem prioridade)\n"
        "  -m MODELO       modelo (padrão gemini-3.5-flash-lite)\n"
        "  -t NIVEL        thinkingLevel: MINIMAL (padrão), LOW, MEDIUM, HIGH; \"\" desliga o campo\n"
        "  -o PASTA        pasta onde salvar os JSONs (padrão Comandos)\n"
        "  --sem-salvar    não grava arquivos, só imprime\n"
        "  --sem-data      não acrescenta a data/hora atuais ao prompt\n"
        "  -u URL_BASE     URL base da API (para testes com um servidor local)\n"
        "Sem texto nem -f, roda as frases de teste do script original.\n";
}

int main(int argc, char** argv) {
    ConfigGemini cfg;
    //std::string chave = "API/gemini_api.txt";
    std::string chave = std::getenv("GEMINI_API");
    std::string pasta = "Comandos";
    std::string arq_texto;
    bool salvar = true;
    std::vector<std::string> textos;

    try {
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            auto valor = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument(a + " requer um valor");
                return argv[++i];
            };
            if (a == "-f")                arq_texto = valor();
            else if (a == "-k")           chave = valor();
            else if (a == "-m")           cfg.modelo = valor();
            else if (a == "-t")           cfg.thinking_level = valor();
            else if (a == "-o")           pasta = valor();
            else if (a == "-u")           cfg.url_base = valor();
            else if (a == "--sem-salvar") salvar = false;
            else if (a == "--sem-data")   cfg.injetar_data_hora = false;
            else if (a == "-h" || a == "--help") { uso(); return 0; }
            else if (!a.empty() && a[0] == '-') {
                std::cerr << "[ERRO] opção desconhecida: " << a << "\n";
                uso();
                return 1;
            } else textos.push_back(a);
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERRO] argumento inválido: " << e.what() << "\n";
        uso();
        return 1;
    }

    if (!arq_texto.empty()) {
        std::ifstream f(arq_texto);
        if (!f) {
            std::cerr << "[ERRO] não foi possível abrir " << arq_texto << "\n";
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        textos.push_back(ss.str());
    }
    if (textos.empty()) {
        textos = {"Ligue a luz da cozinha por favor",
                  "Anota leite e pão na lista de compras",
                  "O que eu tenho agendado para amanhã?",
                  "..",
                  "Oi"};
    }
    
    /*
    std::string chave;
    try {
        chave = carregar_chave(arq_chave);   // nunca é impressa nem registrada em log
    } catch (const std::exception& e) {
        std::cerr << "[ERRO] " << e.what() << "\n";
        return 1;
    }
    */
    int falhas = 0;
    for (const std::string& texto : textos) {
        std::cerr << "\n[Entrada Voz]: \"" << texto << "\"\n";

        const auto t0 = std::chrono::steady_clock::now();
        const ResultadoGemini r = processar_comando_voz(cfg, chave, texto);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();

        if (r.ok) {
            std::cerr << "[Gemini]: concluído em " << ms << " ms\n";
        } else {
            falhas++;
            std::cerr << "[Erro Gemini API]: " << r.erro << "\n"
                      << "[Gemini]: usando o comando de contingência\n";
        }

        std::cout << r.comando.dump(2) << "\n";

        if (salvar) {
            try {
                std::cerr << "[Info]: salvo em " << salvar_comando(r.comando, pasta) << "\n";
            } catch (const std::exception& e) {
                std::cerr << "[ERRO] " << e.what() << "\n";
                return 1;
            }
        }
    }
    return falhas ? 3 : 0;
}
