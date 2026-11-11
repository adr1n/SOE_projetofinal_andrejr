// stt.hpp: Speech-to-Text via Groq (Whisper) em C++.
// Equivalente em C++ do script Python com sounddevice + groq.
//
// Dependências: libcurl (HTTP) e PortAudio (microfone).
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Falha ao acessar o microfone (dispositivo ausente, ocupado, formato não suportado...)
struct ErroAudio : std::runtime_error { using std::runtime_error::runtime_error; };
// Falha de rede ou resposta de erro da API
struct ErroAPI : std::runtime_error { using std::runtime_error::runtime_error; };

struct ConfigSTT {
    std::string url    = "https://api.groq.com/openai/v1/audio/transcriptions";
    std::string modelo = "whisper-large-v3";   // alternativa mais rápida: "whisper-large-v3-turbo"
    std::string idioma = "pt";
    int taxa           = 16000;                // Hz (mono, 16 bits)
    int dispositivo    = -1;                   // índice do PortAudio; -1 = dispositivo padrão
    int timeout_s      = 20;                   // tempo máximo da requisição HTTP
};

// Lê a chave de API de um arquivo (descarta espaços e quebras de linha).
// Lança std::runtime_error se o arquivo não abrir ou estiver vazio.
std::string ler_chave(const std::string& caminho);

// Grava 'duracao_s' segundos do microfone (mono, int16, cfg.taxa Hz). Lança ErroAudio.
std::vector<int16_t> gravar_audio(const ConfigSTT& cfg, double duracao_s);

// Valor RMS do sinal, na escala do int16 (silêncio ~ dezenas; voz ~ centenas ou milhares).
double rms(const std::vector<int16_t>& pcm);

// Empacota PCM 16-bit mono em um arquivo WAV, inteiramente em memória.
std::vector<uint8_t> montar_wav(const std::vector<int16_t>& pcm, int taxa);

// Envia o WAV ao endpoint de transcrição e devolve o texto. Lança ErroAPI.
std::string transcrever(const ConfigSTT& cfg, const std::string& chave,
                        const std::vector<uint8_t>& wav);

// Imprime em stdout os dispositivos de entrada vistos pelo PortAudio.
void listar_dispositivos();
