#pragma once
#include <string>
#include <vector>

struct sqlite3;  // declaração antecipada: evita incluir sqlite3.h no header

struct Tarefa {
    long long id;
    std::string descricao;
    std::string data_hora;  // "" se não tiver data
};

// Todos os métodos lançam std::runtime_error em falha do banco.
// agendar_tarefa lança std::invalid_argument se a data tiver formato inválido.
class Armazenamento {
public:
    explicit Armazenamento(const std::string& caminho_db);
    ~Armazenamento();
    Armazenamento(const Armazenamento&) = delete;
    Armazenamento& operator=(const Armazenamento&) = delete;

    void adicionar_compra(const std::string& item);
    std::vector<std::string> listar_compras();
    void limpar_compras();

    // data_hora: "" (sem data), "YYYY-MM-DD" ou "YYYY-MM-DDTHH:MM"
    void agendar_tarefa(const std::string& descricao, const std::string& data_hora);
    // Tarefas de hoje até hoje + dias_a_frente, ordenadas por data.
    std::vector<Tarefa> listar_tarefas(int dias_a_frente);

private:
    sqlite3* db_ = nullptr;
};
