#include "sqlite/sqlite_wrapper.h"

// Forward-declare the SQLite C API with C linkage so the linker finds the
// correct (unmangled) symbols from libsqlite3. We avoid including sqlite3.h
// to keep this wrapper lightweight.
extern "C" {
const char* sqlite3_errmsg(sqlite3*);

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
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int iCol);
const void* sqlite3_column_blob(sqlite3_stmt*, int iCol);
sqlite3_int64 sqlite3_column_int64(sqlite3_stmt*, int iCol);
int sqlite3_column_bytes(sqlite3_stmt*, int iCol);
double sqlite3_column_double(sqlite3_stmt*, int iCol);
}

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

double column_double(sqlite3_stmt* stmt, int i) { return sqlite3_column_double(stmt, i); }

} // namespace sqlitew

