#pragma once

#include <string>

// ============================================================================
// Biblioteca Telegram BOT
// ============================================================================

// Função para enviar mensagem (equivalente ao curl -X POST ... /sendMessage)
bool enviarMensagemTelegram(const std::string& token, const std::string& chatId, const std::string& texto);

// Função para checar atualizações (equivalente ao curl ... /getUpdates)
void conferirAtualizacoes(const std::string& token);

