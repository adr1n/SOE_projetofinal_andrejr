#include "armazenamento.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <regex>
#include <stdexcept>

namespace {

const char* SCHEMA = R"sql(
CREATE TABLE IF NOT EXISTS compras (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  item      TEXT NOT NULL,
  criado_em TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE TABLE IF NOT EXISTS tarefas (
  id        INTEGER PRIMARY KEY AUTOINCREMENT,
  descricao TEXT NOT NULL,
  data_hora TEXT,
  criado_em TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE INDEX IF NOT EXISTS idx_tarefas_data ON tarefas(data_hora);
)sql";

// RAII para sqlite3_stmt: garante o finalize mesmo se lançar exceção.
struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(s); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
};

std::string col_txt(sqlite3_stmt* s, int i) {
    const unsigned char* p = sqlite3_column_text(s, i);
    return p ? reinterpret_cast<const char*>(p) : "";
}

void bind_txt(sqlite3_stmt* s, int i, const std::string& v) {
    // SQLITE_TRANSIENT: o SQLite copia a string, então 'v' pode ser destruída depois
    sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT);
}

}  // namespace

Armazenamento::Armazenamento(const std::string& caminho_db) {
    auto pasta = std::filesystem::path(caminho_db).parent_path();
    if (!pasta.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(pasta, ec);
    }

    if (sqlite3_open(caminho_db.c_str(), &db_) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("sqlite3_open: " + msg);
    }
    sqlite3_busy_timeout(db_, 3000);  // espera até 3 s se outro processo estiver escrevendo

    char* err = nullptr;
    if (sqlite3_exec(db_, SCHEMA, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "erro desconhecido";
        sqlite3_free(err);
        sqlite3_close(db_);
        throw std::runtime_error("schema: " + msg);
    }
}

Armazenamento::~Armazenamento() { sqlite3_close(db_); }

// ---------------- Compras ----------------

void Armazenamento::adicionar_compra(const std::string& item) {
    Stmt st(db_, "INSERT INTO compras(item) VALUES(?1)");
    bind_txt(st.s, 1, item);
    if (sqlite3_step(st.s) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
}

std::vector<std::string> Armazenamento::listar_compras() {
    Stmt st(db_, "SELECT item FROM compras ORDER BY id");
    std::vector<std::string> itens;
    while (sqlite3_step(st.s) == SQLITE_ROW) itens.push_back(col_txt(st.s, 0));
    return itens;
}

void Armazenamento::limpar_compras() {
    char* err = nullptr;
    if (sqlite3_exec(db_, "DELETE FROM compras", nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "erro";
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

// ---------------- Agenda ----------------

void Armazenamento::agendar_tarefa(const std::string& descricao, const std::string& data_hora) {
    static const std::regex RE_DATA(R"(^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2})?$)");
    if (!data_hora.empty() && !std::regex_match(data_hora, RE_DATA))
        throw std::invalid_argument("data fora do formato ISO: " + data_hora);

    Stmt st(db_, "INSERT INTO tarefas(descricao, data_hora) VALUES(?1, ?2)");
    bind_txt(st.s, 1, descricao);
    if (data_hora.empty()) sqlite3_bind_null(st.s, 2);
    else                   bind_txt(st.s, 2, data_hora);
    if (sqlite3_step(st.s) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
}

std::vector<Tarefa> Armazenamento::listar_tarefas(int dias_a_frente) {
    // Compara só a parte da data (10 primeiros caracteres): funciona com ou sem hora.
    Stmt st(db_,
            "SELECT id, descricao, data_hora FROM tarefas "
            "WHERE data_hora IS NOT NULL "
            "  AND substr(data_hora,1,10) BETWEEN date('now','localtime') "
            "                                 AND date('now','localtime', ?1) "
            "ORDER BY data_hora");
    bind_txt(st.s, 1, "+" + std::to_string(dias_a_frente) + " days");

    std::vector<Tarefa> r;
    while (sqlite3_step(st.s) == SQLITE_ROW)
        r.push_back({sqlite3_column_int64(st.s, 0), col_txt(st.s, 1), col_txt(st.s, 2)});
    return r;
}
