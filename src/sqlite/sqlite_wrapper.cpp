#include "sqlite/sqlite_wrapper.h"

namespace {
inline std::string format_errmsg(sqlite3* db, const char* op, int rc) {
	std::string msg = std::string(op) + " rc=" + std::to_string(rc);
	if (db != nullptr) {
		msg += " sqlite_msg=";
		msg += sqlite3_errmsg(db);
	}
	return msg;
}
}

namespace sqlitew {

// Define SQLITE_ROW/DONE locally to avoid including sqlite3.h here.
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;

void open(const char* filename, sqlite3** ppDb) {
	const int rc = sqlite3_open(filename, ppDb);
	if (rc != 0) {
		throw SqliteError(rc, format_errmsg(*ppDb, "sqlite3_open", rc));
	}
}

void close(sqlite3* db) {
	const int rc = sqlite3_close(db);
	if (rc != 0) {
		throw SqliteError(rc, "sqlite3_close rc=" + std::to_string(rc));
	}
}

void exec(sqlite3* db, const char* sql, int (*cb)(void*, int, char**, char**), void* p, char** e) {
	const int rc = sqlite3_exec(db, sql, cb, p, e);
	if (rc != 0) {
		throw SqliteError(rc, format_errmsg(db, "sqlite3_exec", rc));
	}
}

void prepare_v2(sqlite3* db, const char* sql, int n, sqlite3_stmt** stmt, const char** tail) {
	const int rc = sqlite3_prepare_v2(db, sql, n, stmt, tail);
	if (rc != 0) {
		throw SqliteError(rc, format_errmsg(db, "sqlite3_prepare_v2", rc));
	}
}

void bind_int64(sqlite3_stmt* stmt, int idx, sqlite3_int64 v) {
	const int rc = sqlite3_bind_int64(stmt, idx, v);
	if (rc != 0) throw SqliteError(rc, "sqlite3_bind_int64 rc=" + std::to_string(rc));
}

void bind_double(sqlite3_stmt* stmt, int idx, double v) {
	const int rc = sqlite3_bind_double(stmt, idx, v);
	if (rc != 0) throw SqliteError(rc, "sqlite3_bind_double rc=" + std::to_string(rc));
}

void bind_text(sqlite3_stmt* stmt, int idx, const char* txt, int n, sqlite3_destructor_type d) {
	const int rc = sqlite3_bind_text(stmt, idx, txt, n, d);
	if (rc != 0) throw SqliteError(rc, "sqlite3_bind_text rc=" + std::to_string(rc));
}

void bind_blob(sqlite3_stmt* stmt, int idx, const void* data, int n, sqlite3_destructor_type d) {
	const int rc = sqlite3_bind_blob(stmt, idx, data, n, d);
	if (rc != 0) throw SqliteError(rc, "sqlite3_bind_blob rc=" + std::to_string(rc));
}

void bind_zeroblob(sqlite3_stmt* stmt, int idx, int n) {
	const int rc = sqlite3_bind_zeroblob(stmt, idx, n);
	if (rc != 0) throw SqliteError(rc, "sqlite3_bind_zeroblob rc=" + std::to_string(rc));
}

int step(sqlite3_stmt* stmt) {
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW || rc == SQLITE_DONE) return rc;
	throw SqliteError(rc, std::string("sqlite3_step rc=") + std::to_string(rc));
}

// Stmt implementation
Stmt::Stmt(sqlite3* db, const char* sql, int n, const char** tail)
	: stmt_db_(db), stmt_(nullptr) {
	prepare_v2(db, sql, n, &stmt_, tail);
}

Stmt::~Stmt() {
	if (stmt_ != nullptr) {
		try {
			finalize(stmt_);
		} catch (...) {
			// destructors must not throw; swallow errors
		}
		stmt_ = nullptr;
	}
}

Stmt::Stmt(Stmt&& other) noexcept
	: stmt_db_(other.stmt_db_), stmt_(other.stmt_) {
	other.stmt_db_ = nullptr;
	other.stmt_ = nullptr;
}

Stmt& Stmt::operator=(Stmt&& other) noexcept {
	if (this != &other) {
		if (stmt_ != nullptr) {
			try { finalize(stmt_); } catch(...) {}
		}
		stmt_db_ = other.stmt_db_;
		stmt_ = other.stmt_;
		other.stmt_db_ = nullptr;
		other.stmt_ = nullptr;
	}
	return *this;
}

void Stmt::bind_int64(int idx, sqlite3_int64 v) { ::sqlitew::bind_int64(stmt_, idx, v); }
void Stmt::bind_double(int idx, double v) { ::sqlitew::bind_double(stmt_, idx, v); }
void Stmt::bind_text(int idx, const char* txt, int n, sqlite3_destructor_type d) { ::sqlitew::bind_text(stmt_, idx, txt, n, d); }
void Stmt::bind_blob(int idx, const void* data, int n, sqlite3_destructor_type d) { ::sqlitew::bind_blob(stmt_, idx, data, n, d); }
void Stmt::bind_zeroblob(int idx, int n) { ::sqlitew::bind_zeroblob(stmt_, idx, n); }
int Stmt::step() { return ::sqlitew::step(stmt_); }
void Stmt::reset() { ::sqlitew::reset(stmt_); }
void Stmt::clear_bindings() { ::sqlitew::clear_bindings(stmt_); }

sqlite3_int64 Stmt::column_int64(int i) const { return ::sqlitew::column_int64(stmt_, i); }
const unsigned char* Stmt::column_text(int i) const { return ::sqlitew::column_text(stmt_, i); }
const void* Stmt::column_blob(int i) const { return ::sqlitew::column_blob(stmt_, i); }
int Stmt::column_bytes(int i) const { return ::sqlitew::column_bytes(stmt_, i); }

void finalize(sqlite3_stmt* stmt) {
	const int rc = sqlite3_finalize(stmt);
	if (rc != 0) throw SqliteError(rc, "sqlite3_finalize rc=" + std::to_string(rc));
}

void reset(sqlite3_stmt* stmt) {
	const int rc = sqlite3_reset(stmt);
	if (rc != 0) throw SqliteError(rc, "sqlite3_reset rc=" + std::to_string(rc));
}

void clear_bindings(sqlite3_stmt* stmt) {
	const int rc = sqlite3_clear_bindings(stmt);
	if (rc != 0) throw SqliteError(rc, "sqlite3_clear_bindings rc=" + std::to_string(rc));
}

int errcode(sqlite3* db) { return sqlite3_errcode(db); }

const char* errmsg(sqlite3* db) { return sqlite3_errmsg(db); }

sqlite3_int64 column_int64(sqlite3_stmt* stmt, int i) { return sqlite3_column_int64(stmt, i); }

const unsigned char* column_text(sqlite3_stmt* stmt, int i) { return sqlite3_column_text(stmt, i); }

const void* column_blob(sqlite3_stmt* stmt, int i) { return sqlite3_column_blob(stmt, i); }

int column_bytes(sqlite3_stmt* stmt, int i) { return sqlite3_column_bytes(stmt, i); }

} // namespace sqlitew

