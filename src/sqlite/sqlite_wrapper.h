// Centralized minimal SQLite C ABI declarations to avoid duplicating the
// extern "C" block across multiple translation units.
// This header mirrors the subset of the SQLite C API used by this project.

#pragma once

extern "C" {
struct sqlite3;
struct sqlite3_stmt;
using sqlite3_int64 = long long;
using sqlite3_destructor_type = void (*)(void*);

int sqlite3_open(const char* filename, sqlite3** ppDb);
int sqlite3_close(sqlite3*);
int sqlite3_exec(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
int sqlite3_prepare_v2(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
int sqlite3_bind_int64(sqlite3_stmt*, int, sqlite3_int64);
int sqlite3_bind_double(sqlite3_stmt*, int, double);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, sqlite3_destructor_type);
int sqlite3_bind_blob(sqlite3_stmt*, int, const void*, int, sqlite3_destructor_type);
int sqlite3_bind_zeroblob(sqlite3_stmt*, int, int);
int sqlite3_step(sqlite3_stmt*);
int sqlite3_finalize(sqlite3_stmt* pStmt);
int sqlite3_reset(sqlite3_stmt* pStmt);
int sqlite3_clear_bindings(sqlite3_stmt* pStmt);
int sqlite3_errcode(sqlite3*);
const char* sqlite3_errmsg(sqlite3*);
sqlite3_int64 sqlite3_column_int64(sqlite3_stmt*, int iCol);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int iCol);
const void* sqlite3_column_blob(sqlite3_stmt*, int iCol);
int sqlite3_column_bytes(sqlite3_stmt*, int iCol);
}

// C++ thin wrappers to avoid calling the raw C symbols throughout the codebase.
// These forward to the C API and live in the `sqlitew` namespace.
#include <stdexcept>
#include <string>

struct SqliteError : public std::runtime_error {
	int rc;
	explicit SqliteError(int rc_, const std::string& msg)
		: std::runtime_error(msg), rc(rc_) {}
};

namespace sqlitew {
	// These wrappers throw SqliteError on failure. They do not return SQLite rc codes.
	void open(const char* filename, sqlite3** ppDb);
	void close(sqlite3* db);
	void exec(sqlite3* db, const char* sql, int (*cb)(void*, int, char**, char**), void* p, char** e);
	void prepare_v2(sqlite3* db, const char* sql, int n, sqlite3_stmt** stmt, const char** tail);
	void bind_int64(sqlite3_stmt* stmt, int idx, sqlite3_int64 v);
	void bind_double(sqlite3_stmt* stmt, int idx, double v);
	void bind_text(sqlite3_stmt* stmt, int idx, const char* txt, int n, sqlite3_destructor_type d);
	void bind_blob(sqlite3_stmt* stmt, int idx, const void* data, int n, sqlite3_destructor_type d);
	void bind_zeroblob(sqlite3_stmt* stmt, int idx, int n);
	int step(sqlite3_stmt* stmt);
	void finalize(sqlite3_stmt* stmt);
	void reset(sqlite3_stmt* stmt);
	void clear_bindings(sqlite3_stmt* stmt);
	int errcode(sqlite3* db);
	const char* errmsg(sqlite3* db);
	sqlite3_int64 column_int64(sqlite3_stmt* stmt, int i);
	const unsigned char* column_text(sqlite3_stmt* stmt, int i);
	const void* column_blob(sqlite3_stmt* stmt, int i);
	int column_bytes(sqlite3_stmt* stmt, int i);

// RAII helper for sqlite3_stmt* to ensure finalize is always called.
class Stmt {
public:
	Stmt(sqlite3* db, const char* sql, int n = -1, const char** tail = nullptr);
	~Stmt();

	// non-copyable
	Stmt(const Stmt&) = delete;
	Stmt& operator=(const Stmt&) = delete;

	// movable
	Stmt(Stmt&& other) noexcept;
	Stmt& operator=(Stmt&& other) noexcept;

	sqlite3_stmt* get() const { return stmt_; }

	// wrappers that forward to free functions
	void bind_int64(int idx, sqlite3_int64 v);
	void bind_double(int idx, double v);
	void bind_text(int idx, const char* txt, int n, sqlite3_destructor_type d);
	void bind_blob(int idx, const void* data, int n, sqlite3_destructor_type d);
	void bind_zeroblob(int idx, int n);
	int step();
	void reset();
	void clear_bindings();

	sqlite3_int64 column_int64(int i) const;
	const unsigned char* column_text(int i) const;
	const void* column_blob(int i) const;
	int column_bytes(int i) const;

private:
	sqlite3* stmt_db_ = nullptr;
	sqlite3_stmt* stmt_ = nullptr;
};
}
