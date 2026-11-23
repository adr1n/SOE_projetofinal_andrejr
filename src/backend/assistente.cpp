#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <vector>
#include <string>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <portaudio.h>
#include <alsa/asoundlib.h>

// Módulos do projeto
#include "wakeword/oww.hpp"
#include "stt.hpp"
#include "gemini.hpp"
#include "armazenamento.hpp"
#include "tts_manager.hpp"
#include "telegram_bot.hpp"
// ============================================================================
// Estados da FSM (Fluxograma do Assistente)
// ============================================================================
enum class EstadoAssistente {
    MONITORANDO_WAKEWORD, // 1 e 2: Aguarda ativação da palavra-chave
    GRAVANDO_COMANDO,     // 3 e 4: Aciona LED e grava 4s de comando (avalia RMS)
    PROCESSANDO_STT,      // 5: Envia áudio ao Groq Whisper e gera texto
    INTERPRETANDO_GEMINI, // 6: Envia texto ao Gemini e valida intenção
    EXECUTANDO_ACAO,      // 7 e 8: Pisca LED e despacha o comando
    ERRO_NAO_ENTENDEU     // Feedback de erro (apaga LED e toca nao_entendi.wav)
};

// ============================================================================
// Controle de LED (Feedback Visual)
// ============================================================================
static std::atomic<bool> g_piscar_led(false);
static std::thread g_thread_led;

void set_led(bool estado) {
    // No Raspberry Pi: manipular pinos GPIO (ex: via libgpiod ou sysfs).
    // Aqui: log no terminal
    std::cout << "[LED]: " << (estado ? "ACESO (ON)" : "APAGADO (OFF)") << std::endl;
}

void iniciar_piscar_led() {
    g_piscar_led = true;
    g_thread_led = std::thread([]() {
        bool nivel = false;
        while (g_piscar_led) {
            nivel = !nivel;
            set_led(nivel);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        set_led(false);
    });
}

void parar_piscar_led() {
    g_piscar_led = false;
    if (g_thread_led.joinable()) {
        g_thread_led.join();
    }
}

// ============================================================================
// Variáveis Globais de Sinais e Processos
// ============================================================================
static volatile sig_atomic_t g_wake_word_detectada = 0;
static pid_t g_pid_aplay = -1; // Guarda o PID do aplay para permitir interrupção imediata

// Tratador do sinal SIGUSR1 (disparado pelo processo filho de Wake Word)
void tratador_sigusr1(int sig) {
    (void)sig;
    g_wake_word_detectada = 1;

    // Se estiver tocando áudio no alto-falante, cancela o aplay na hora!
    if (g_pid_aplay > 0) {
        kill(g_pid_aplay, SIGTERM);
        g_pid_aplay = -1;
    }
}

// Reproduz arquivo WAV no alto-falante de forma que possa ser interrompido por sinal
void tocar_audio_interrompivel(const std::string& wav_path) {
    g_pid_aplay = fork();
    if (g_pid_aplay == 0) {
        // Processo neto executa o aplay
        execlp("aplay", "aplay", "-q", wav_path.c_str(), nullptr);
        _exit(1);
    } else if (g_pid_aplay > 0) {
        int status = 0;
        waitpid(g_pid_aplay, &status, 0); // Bloqueia até terminar ou ser interrompido por sinal
        g_pid_aplay = -1;
    }
}

// ============================================================================
// Processo Filho: Detector de Wake Word em Background
// ============================================================================
void processo_filho_wakeword(int read_fd) {
    // Inicializa o Detector DENTRO do processo filho (evita conflito de hardware ALSA)
    auto det = inicializar_detector("alexa_v0.1.onnx");
    if (!det) {
        std::cerr << "[FILHO]: Falha ao inicializar modelos ou microfone do Wake Word!\n";
        exit(1);
    }

    std::cout << "[FILHO]: Monitorando áudio em background (PID: " << getpid() << ")...\n";

    while (true) {
        // Processa 80ms de áudio do microfone
        StatusDeteccao status = detectar_passo(*det);

        if (status.detectado) {
            std::cout << "[FILHO]: >>> WAKE WORD DETECTADA (Confiança: "
                      << status.probabilidade << ")! Enviando SIGUSR1 ao Pai...\n";

            // 1. Envia sinal SIGUSR1 para o processo pai (getppid())
            kill(getppid(), SIGUSR1);

            // 2. Pausa a escuta do microfone para não capturar a própria fala ou comando STT
            pausar_escuta(*det);

            // 3. Aguarda o Pai avisar pelo Pipe que o comando acabou ('R' = Retomar)
            char cmd = 0;
            if (read(read_fd, &cmd, sizeof(cmd)) > 0) {
                if (cmd == 'R') {
                    // Limpa buffers acústicos residuais e reativa a escuta
                    retomar_escuta(*det);
                    std::cout << "[FILHO]: Microfone limpo e escuta retomada.\n";
                }
            }
        }
    }
}

// ============================================================================
// Processo Pai: Máquina de Estados Principal do Assistente
// ============================================================================
int main() {
    // 0. Silencia mensagens internas de diagnóstico do ALSA e JACK
    snd_lib_error_set_handler(nullptr);

    // 1. Carrega chaves de API das variáveis de ambiente ou arquivos
    std::string chave_groq = std::getenv("GROQ_API_KEY") ? std::getenv("GROQ_API_KEY") : "";
    if (chave_groq.empty()) {
        if (const char* g = std::getenv("GROQ_API")) chave_groq = g;
        else {
            std::ifstream f("API/groq_whisper_api.txt");
            if (!f.is_open()) f.open("API/groq_whisper_api.txt");
            if (f.is_open()) f >> chave_groq;
        }
    }
    std::cout << "[PAI]: Chave Groq Whisper: " << (chave_groq.empty() ? "<não encontrada>" : "<carregada>") << "\n";

    std::string chave_gemini = std::getenv("GEMINI_API_KEY") ? std::getenv("GEMINI_API_KEY") : "";
    if (chave_gemini.empty()) {
        if (const char* g = std::getenv("GEMINI_API")) chave_gemini = g;
        else {
            std::ifstream f("API/gemini_api.txt");
            if (f.is_open()) f >> chave_gemini;
        }
    }
    std::cout << "[PAI]: Chave Gemini: " << (chave_gemini.empty() ? "<não encontrada>" : "<carregada>") << "\n";

    std::string token = std::getenv("TELEGRAM_TOKEN");
    std::string chatId = std::getenv("TELEGRAM_CHATID");

    // 2. Cria o Pipe(comunicação Pai -> Filho)
    int fd[2];
    if (pipe(fd) < 0) {
        perror("Erro ao criar pipe");
        return 1;
    }

    // 3. Registra o tratador para o sinal SIGUSR1
    signal(SIGUSR1, tratador_sigusr1);

    std::cout << "=== Assistente Inicializado e Pronto ===" << std::endl;

    pid_t pid = fork();
    if (pid < 0) {
        perror("Erro no fork");
        return 1;
    }

    if (pid == 0) {
        // --- CÓDIGO DO FILHO ---
        close(fd[1]); // Fecha a ponta de escrita (o filho só lê)
        processo_filho_wakeword(fd[0]);
        return 0;
    }

    // --- CÓDIGO DO PAI ---
    close(fd[0]); // Fecha a ponta de leitura (o pai só escreve)
    int pipe_escrita = fd[1];

    // Mantém o PortAudio aquecido no Pai para não haver atraso de inicialização na gravação
    Pa_Initialize();

    Armazenamento db("Dados/Assistente.db");
    ConfigSTT cfg_stt;
    ConfigGemini cfg_gemini;

    EstadoAssistente estado = EstadoAssistente::MONITORANDO_WAKEWORD;
    std::vector<int16_t> pcm_comando;
    std::string texto_transcrito;
    ResultadoGemini resultado_gemini;

    // Helper: envia 'R' pelo pipe para o filho voltar a escutar
    auto liberar_wakeword = [&]() {
        char cmd = 'R';
        if (write(pipe_escrita, &cmd, sizeof(cmd)) < 0) {
            perror("[PAI ERRO write pipe]");
        }
    };

    while (true) {
        switch (estado) {
            // ----------------------------------------------------------------
            // 1 & 2: Monitorando Wake Word (Background)
            // ----------------------------------------------------------------
            case EstadoAssistente::MONITORANDO_WAKEWORD: {
                if (g_wake_word_detectada) {
                    g_wake_word_detectada = 0;
                    std::cout << "\n[PAI]: Ativação recebida! Acionando LED...\n";
                    set_led(true); // Aciona o LED
                    estado = EstadoAssistente::GRAVANDO_COMANDO;
                } else {
                    usleep(25000); // 25ms para não ocupar 100% da CPU em espera
                }
                break;
            }

            // ----------------------------------------------------------------
            // 3 & 4: Gravando e Validando Áudio do Comando
            // ----------------------------------------------------------------
            case EstadoAssistente::GRAVANDO_COMANDO: {
                std::cout << "[PAI]: Escutando comando por 4 segundos...\n";
                try {
                    pcm_comando = gravar_audio(cfg_stt, 4.0);
                } catch (const std::exception& e) {
                    std::cerr << "[PAI ERRO Gravação]: " << e.what() << "\n";
                    estado = EstadoAssistente::ERRO_NAO_ENTENDEU;
                    break;
                }

                // Avalia se o comando gravado possui informação sonora (RMS)
                double nivel_rms = rms(pcm_comando);
                std::cout << "[PAI]: Nível de energia RMS = " << nivel_rms << "\n";

                if (nivel_rms < 5.0) { // Silêncio ou ruído irrelevante
                    std::cout << "[PAI]: Nenhum comando detectado (silêncio).\n";
                    estado = EstadoAssistente::ERRO_NAO_ENTENDEU;
                } else {
                    estado = EstadoAssistente::PROCESSANDO_STT;
                }
                break;
            }

            // ----------------------------------------------------------------
            // 5: Transformando Áudio em Texto (STT Groq Whisper)
            // ----------------------------------------------------------------
            case EstadoAssistente::PROCESSANDO_STT: {
                try {
                    auto wav = montar_wav(pcm_comando, cfg_stt.taxa);
                    texto_transcrito = transcrever(cfg_stt, chave_groq, wav);
                    std::cout << "[PAI]: Transcrição: \"" << texto_transcrito << "\"\n";

                    if (texto_transcrito.empty()) {
                        estado = EstadoAssistente::ERRO_NAO_ENTENDEU;
                    } else {
                        estado = EstadoAssistente::INTERPRETANDO_GEMINI;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[PAI ERRO STT]: " << e.what() << "\n";
                    estado = EstadoAssistente::ERRO_NAO_ENTENDEU;
                }
                break;
            }

            // ----------------------------------------------------------------
            // 6: Interpretando com o Gemini (NLU)
            // ----------------------------------------------------------------
            case EstadoAssistente::INTERPRETANDO_GEMINI: {
                resultado_gemini = processar_comando_voz(cfg_gemini, chave_gemini, texto_transcrito);

                // O comando faz sentido?
                if (!resultado_gemini.ok || resultado_gemini.comando.is_null()) {
                    std::cerr << "[PAI]: Comando incompreensível ou erro na API.\n";
                    estado = EstadoAssistente::ERRO_NAO_ENTENDEU;
                } else {
                    estado = EstadoAssistente::EXECUTANDO_ACAO;
                }
                break;
            }

            // ----------------------------------------------------------------
            // 7: Executando Ação enquanto Pisca o LED
            // ----------------------------------------------------------------
            case EstadoAssistente::EXECUTANDO_ACAO: {
                iniciar_piscar_led(); // Pisca o LED durante a execução

                std::string tipo = resultado_gemini.comando["tipo_comando"];
                std::cout << "[PAI]: Executando comando: " << tipo << "\n";

                if (tipo == "ACIONAR_LAMPADA") {
                    std::string estado_lamp = resultado_gemini.comando["conteudo"]["estado_lampada"];
                    if (estado_lamp == "ligar") {
                        std::cout << "[ACAO]: Ligando lâmpada...\n";
                        tocar_audio_interrompivel("Audio/ligando_lampada_sala.wav");
                    } else {
                        std::cout << "[ACAO]: Desligando lâmpada...\n";
                        tocar_audio_interrompivel("Audio/desligando_lampada_sala.wav");
                    }
                } else if (tipo == "ADICIONAR_COMPRAS") {
                    std::string item = resultado_gemini.comando["conteudo"]["item_compra"];
                    db.adicionar_compra(item);
                    std::string fala = item + " adicionado à lista de compras.";
                    std::cout << "[ACAO]: " << fala << "\n";
                    if (gerarAudioGeminiTTS(fala, chave_gemini, "Audio/resposta.wav")) {
                        tocar_audio_interrompivel("Audio/resposta.wav");
                    }
                } else if (tipo == "LISTAR_COMPRAS") {
                    auto itens = db.listar_compras();
                    std::string fala = itens.empty() ? "Sua lista de compras está vazia." : "Na sua lista tem: ";
                    for (size_t i = 0; i < itens.size(); i++) fala += itens[i] + (i + 1 < itens.size() ? ", " : ".");
                    std::cout << "[ACAO]: " << fala << "\n";
                    try{
                        enviarMensagemTelegram(token, chatId, fala);
                    }
                    catch(const std::exception& e){
                        std::cout << "[TELEGRAM]: Erro ao enviar mensagens: " << e.what() << std::endl;
                    }
                    if (gerarAudioGeminiTTS(fala, chave_gemini, "Audio/resposta.wav")) {
                        tocar_audio_interrompivel("Audio/resposta.wav");
                    }
                } else if (tipo == "AGENDAR_TAREFA") {
                    std::string desc = resultado_gemini.comando["conteudo"]["descricao_evento"];
                    std::string data = resultado_gemini.comando["conteudo"].value("data_evento", "");
                    db.agendar_tarefa(desc, data);
                    std::string fala = "Agendado: " + desc;
                    if (!data.empty()) fala += " para " + data;
                    std::cout << "[ACAO]: " << fala << "\n";
                    try{
                        enviarMensagemTelegram(token, chatId, fala);
                    }
                    catch(const std::exception& e){
                        std::cout << "[TELEGRAM]: Erro ao enviar mensagens: " << e.what() << std::endl;
                    }
                    if (gerarAudioGeminiTTS(fala, chave_gemini, "Audio/resposta.wav")) {
                        tocar_audio_interrompivel("Audio/resposta.wav");
                    }
                } else if (tipo == "LISTAR_TAREFAS") {
                    auto tarefas = db.listar_tarefas(7);
                    std::string fala;
                    if (tarefas.empty()) {
                        fala = "Você não tem compromissos nos próximos sete dias.";
                    } else {
                        fala = "Seus compromissos são: ";
                        for (size_t i = 0; i < tarefas.size(); i++) {
                            fala += tarefas[i].descricao;
                            if (!tarefas[i].data_hora.empty()) fala += ", " + tarefas[i].data_hora;
                            fala += (i + 1 < tarefas.size()) ? "; " : ".";
                        }
                    }
                    std::cout << "[ACAO]: " << fala << "\n";
                    try{
                        enviarMensagemTelegram(token, chatId, fala);
                    }
                    catch(const std::exception& e){
                        std::cout << "[TELEGRAM]: Erro ao enviar mensagens: " << e.what() << std::endl;
                    }
                    if (gerarAudioGeminiTTS(fala, chave_gemini, "Audio/resposta.wav")) {
                        tocar_audio_interrompivel("Audio/resposta.wav");
                    }
                } else if (tipo == "CONVERSA_GERAL") {
                    std::string resposta = resultado_gemini.comando["conteudo"]["resposta_texto"];
                    std::cout << "[GEMINI]: " << resposta << "\n";
                    if (gerarAudioGeminiTTS(resposta, chave_gemini, "Audio/resposta.wav")) {
                        tocar_audio_interrompivel("Audio/resposta.wav");
                    }
                }

                // 8 e 9: Fim do comando -> Desliga LED e apaga buffers temporários
                parar_piscar_led();
                set_led(false);
                pcm_comando.clear();

                // Pausa de 100ms para amortecer qualquer eco físico no ambiente
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Avisa o processo filho para voltar a escutar a Wake Word
                liberar_wakeword();
                estado = EstadoAssistente::MONITORANDO_WAKEWORD;
                break;
            }

            // ----------------------------------------------------------------
            // 8: Erro: Desliga LED, Apaga áudio e responde que não entendeu
            // ----------------------------------------------------------------
            case EstadoAssistente::ERRO_NAO_ENTENDEU: {
                parar_piscar_led();
                set_led(false);
                pcm_comando.clear();

                tocar_audio_interrompivel("Audio/nao_entendi.wav");

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                liberar_wakeword();
                estado = EstadoAssistente::MONITORANDO_WAKEWORD;
                break;
            }
        }
    }

    return 0;
}
