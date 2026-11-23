#pragma once

#include <string>

// ============================================================================
// Biblioteca Telegram BOT
// ============================================================================

// Função de callback para capturar a resposta HTTP do Telegram
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);

// Função para enviar mensagem (equivalente ao curl -X POST ... /sendMessage)
bool enviarMensagemTelegram(const std::string& token, const std::string& chatId, const std::string& texto);

// Função para checar atualizações (equivalente ao curl ... /getUpdates)
void conferirAtualizacoes(const std::string& token);

