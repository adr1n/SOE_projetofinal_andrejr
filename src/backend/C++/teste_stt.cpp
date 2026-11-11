// teste_stt.cpp: equivalente ao bloco `if __name__ == "__main__"` do script Python.
//
// Compilar:
//   g++ -O2 -std=c++17 -Wall -Wextra teste_stt.cpp stt.cpp -o teste_stt -lcurl -lportaudio
//
// Uso:
//   ./teste_stt --listar                  lista os microfones
//   ./teste_stt                           grava 5 s, transcreve, imprime e salva em comando_voz.txt
//   ./teste_stt -d 4 -i 2 -r 150          4 s, dispositivo 2, descarta se RMS < 150
//
// stdout: só o texto transcrito. stderr: logs. Assim dá para fazer:  TEXTO=$(./teste_stt)
// Códigos de saída: 0 ok | 1 uso/arquivo | 2 áudio | 3 rede/API | 4 silêncio descartado

#include "stt.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

static void uso() {
    std::cerr <<
        "Uso: teste_stt [opções]\n"
        "  --listar        lista os dispositivos de entrada e sai\n"
        "  -d SEGUNDOS     duração da gravação (padrão 5)\n"
        "  -i INDICE       dispositivo de entrada (padrão: o do sistema)\n"
        "  -k ARQUIVO      arquivo com a chave da Groq (padrão API/groq_whisper_api)\n"
        "  -m MODELO       whisper-large-v3 (padrão) ou whisper-large-v3-turbo\n"
        "  -o ARQUIVO      onde salvar o texto (padrão comando_voz.txt)\n"
        "  -r RMS_MIN      descarta a gravação se o RMS for menor (padrão 0 = desligado)\n";
}

int main(int argc, char** argv) {
    ConfigSTT cfg;
    std::string arq_chave = "API/groq_whisper_api";
    std::string arq_saida = "comando_voz.txt";
    double duracao = 5.0;
    double rms_min = 0.0;
    bool listar = false;

    try {
        for (int i = 1; i < argc; i++) {
            const std::string a = argv[i];
            auto valor = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument(a + " requer um valor");
                return argv[++i];
            };
            if (a == "--listar")      listar = true;
            else if (a == "-d")       duracao = std::stod(valor());
            else if (a == "-i")       cfg.dispositivo = std::stoi(valor());
            else if (a == "-k")       arq_chave = valor();
            else if (a == "-m")       cfg.modelo = valor();
            else if (a == "-o")       arq_saida = valor();
            else if (a == "-r")       rms_min = std::stod(valor());
            else { std::cerr << "[ERRO] opção desconhecida: " << a << "\n"; uso(); return 1; }
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERRO] argumento inválido: " << e.what() << "\n";
        uso();
        return 1;
    }

    try {
        if (listar) { listar_dispositivos(); return 0; }

        // A chave é lida do arquivo e nunca é impressa nem registrada em log.
        const std::string chave = ler_chave(arq_chave);

        std::cerr << "[STT Groq]: Gravando comando por " << duracao << " segundos...\n";
        const std::vector<int16_t> pcm = gravar_audio(cfg, duracao);

        const double nivel = rms(pcm);
        std::cerr << "[STT Groq]: RMS do áudio = " << nivel << "\n";
        if (nivel < rms_min) {
            std::cerr << "[STT Groq]: abaixo do limiar (" << rms_min
                      << "), gravação descartada como silêncio.\n";
            return 4;
        }

        const std::vector<uint8_t> wav = montar_wav(pcm, cfg.taxa);

        std::cerr << "[STT Groq]: Enviando áudio para a nuvem...\n";
        const auto t0 = std::chrono::steady_clock::now();
        const std::string texto = transcrever(cfg, chave, wav);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        std::cerr << "[STT Groq]: Concluído em " << ms << " ms!\n";

        std::cout << texto << "\n";

        if (!arq_saida.empty()) {
            std::ofstream f(arq_saida);
            if (!f) {
                std::cerr << "[ERRO] não foi possível gravar " << arq_saida << "\n";
                return 1;
            }
            f << texto;
        }
        return 0;

    } catch (const ErroAudio& e) {
        std::cerr << "[ERRO áudio] " << e.what() << "\n";
        return 2;
    } catch (const ErroAPI& e) {
        std::cerr << "[ERRO API] " << e.what() << "\n";
        return 3;
    } catch (const std::exception& e) {
        std::cerr << "[ERRO] " << e.what() << "\n";
        return 1;
    }
}
