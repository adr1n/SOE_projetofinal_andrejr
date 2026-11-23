#pragma once

#include <string>

// ============================================================================
// Biblioteca TTS (Text-to-Speech) via Google Gemini API
// ============================================================================
// Vozes disponíveis: "Puck" (padrão), "Charon", "Aoede", "Fenrir", "Kore"

// 1. Obtém a chave da API do Gemini (busca por argumento, variáveis de ambiente
//    GEMINI_API / GEMINI_API_KEY ou arquivo API/gemini_api.txt).
std::string obterChaveGemini(const std::string& argChave = "");

// 2. Sintetiza um texto em áudio WAV e salva no caminho 'outputFile'.
// Parâmetros:
//   - texto: String contendo a frase a ser falada.
//   - apiKey: Chave do Gemini (se vazia "", tenta buscar automaticamente).
//   - outputFile: Caminho do arquivo de saída (padrão "Audio/saida.wav").
//   - voz: Nome da voz desejada (padrão "Puck").
// Retorna:
//   - true se o áudio foi gerado e salvo com sucesso; false caso contrário.
bool gerarAudioGeminiTTS(const std::string& texto,
                         const std::string& apiKey = "",
                         const std::string& outputFile = "Audio/saida.wav",
                         const std::string& voz = "Puck");
