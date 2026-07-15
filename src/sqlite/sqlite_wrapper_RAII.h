#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#include "sqlite/sqlite_wrapper.h"

class Statement;

class Connection {
public:
    Connection() = default;

    explicit Connection(const char* filename) {
        open(filename);
    }

    explicit Connection(const std::string& filename)
        : Connection(filename.c_str()) {}

    ~Connection() noexcept {
        close_noexcept();
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept
        : db_(other.db_) {
        other.db_ = nullptr;
    }

    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            close_noexcept();
            db_ = other.db_;
            other.db_ = nullptr;
        }
        return *this;
    }

    /// 20260714 BUG: Re-open close when init instance
    void open(const char* filename) {
        if (!filename || filename[0] == '\0') {
            throw std::invalid_argument("sqlite filename is empty");
        }
        //close();
        if (db_) {
            return; // already open
        }
        sqlitew::open(filename, &db_);
        sqlitew::exec(get(), "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlitew::exec(get(), "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        sqlitew::exec(get(), "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
    }

    void close() {
        if (db_ != nullptr) {
            sqlite3* to_close = db_;
            db_ = nullptr;
            sqlitew::close(to_close);
        }
    }

    void close_noexcept() noexcept {
        if (db_ != nullptr) {
            sqlite3* to_close = db_;
            db_ = nullptr;
            try {
                sqlitew::close(to_close);
            } catch (...) {
                // Destructors must not throw.
            }
        }
    }

    [[nodiscard]] bool is_open() const noexcept {
        return db_ != nullptr;
    }

    [[nodiscard]] sqlite3* get() const {
        if (db_ == nullptr) {
            throw std::runtime_error("sqlite connection is not open");
        }
        return db_;
    }

    Statement prepare(const char* sql, int n = -1, const char** tail = nullptr) const;

private:
    sqlite3* db_ = nullptr;
};

class Statement {
public:
    Statement() = default;

    Statement(const Connection& conn, const char* sql, int n = -1, const char** tail = nullptr) {
        prepare(conn, sql, n, tail);
    }

    ~Statement() noexcept {
        finalize_noexcept();
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement(Statement&& other) noexcept
        : stmt_(other.stmt_) {
        other.stmt_ = nullptr;
    }

    Statement& operator=(Statement&& other) noexcept {
        if (this != &other) {
            finalize_noexcept();
            stmt_ = other.stmt_;
            other.stmt_ = nullptr;
        }
        return *this;
    }

    void prepare(const Connection& conn, const char* sql, int n = -1, const char** tail = nullptr) {
        finalize();
        sqlitew::prepare_v2(conn.get(), sql, n, &stmt_, tail);
    }

    void finalize() {
        if (stmt_ != nullptr) {
            sqlite3_stmt* to_finalize = stmt_;
            stmt_ = nullptr;
            sqlitew::finalize(to_finalize);
        }
    }

    void finalize_noexcept() noexcept {
        if (stmt_ != nullptr) {
            sqlite3_stmt* to_finalize = stmt_;
            stmt_ = nullptr;
            try {
                sqlitew::finalize(to_finalize);
            } catch (...) {
                // Destructors must not throw.
            }
        }
    }

    [[nodiscard]] bool is_prepared() const noexcept {
        return stmt_ != nullptr;
    }

    [[nodiscard]] sqlite3_stmt* get() const {
        if (stmt_ == nullptr) {
            throw std::runtime_error("sqlite statement is not prepared");
        }
        return stmt_;
    }

    int step() {
        return sqlitew::step(get());
    }

    void reset() {
        sqlitew::reset(get());
    }

    void clear_bindings() {
        sqlitew::clear_bindings(get());
    }

    void reset_and_clear() {
        reset();
        clear_bindings();
    }

    void bind_int64(int idx, sqlite3_int64 v) {
        sqlitew::bind_int64(get(), idx, v);
    }

    void bind_double(int idx, double v) {
        sqlitew::bind_double(get(), idx, v);
    }

    void bind_text(int idx, const char* txt, int n = -1, sqlite3_destructor_type d = nullptr) {
        sqlitew::bind_text(get(), idx, txt, n, d);
    }

    void bind_blob(int idx, const void* data, int n, sqlite3_destructor_type d = nullptr) {
        sqlitew::bind_blob(get(), idx, data, n, d);
    }

    void bind_zeroblob(int idx, int n) {
        sqlitew::bind_zeroblob(get(), idx, n);
    }

    [[nodiscard]] sqlite3_int64 column_int64(int i) const {
        return sqlitew::column_int64(get(), i);
    }

    [[nodiscard]] const unsigned char* column_text(int i) const {
        return sqlitew::column_text(get(), i);
    }

    [[nodiscard]] const void* column_blob(int i) const {
        return sqlitew::column_blob(get(), i);
    }

    [[nodiscard]] int column_bytes(int i) const {
        return sqlitew::column_bytes(get(), i);
    }

    [[nodiscard]] double column_double(int i) const {
        return sqlitew::column_double(get(), i);
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

inline Statement Connection::prepare(const char* sql, int n, const char** tail) const {
    return Statement(*this, sql, n, tail);
}

