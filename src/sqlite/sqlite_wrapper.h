#pragma once



/// NOTE: 20260708
/*
There are two different kinds of SQLite objects:
Database connection (sqlite3*)

sqlite3* db;
sqlite3_open("log.db", &db);
-> This represents the connection to the database.
sqlite3_close(db);
// or sqlite3_close_v2(db);
-> No statement is involved in closing the database.


Prepared statement (sqlite3_stmt*) to execute a SQL command
sqlite3_stmt* stmt;
sqlite3_prepare_v2(db,
                   "SELECT ...",
                   -1,
                   &stmt,
                   nullptr);

sqlite3_step(stmt);
sqlite3_finalize(stmt);

sqlite3* db
    │
    ├── sqlite3_stmt* stmt1   (INSERT)
    ├── sqlite3_stmt* stmt2   (SELECT)
    ├── sqlite3_stmt* stmt3   (UPDATE)
    └── sqlite3_stmt* stmt4   (DELETE)
must finalize the statements before sqlite3_close() succeeds otherwise it return SQLITE_BUSY
*/

/// NOTE: resource lifecycle management
/// In sqlite3.h, you only get:
/// typedef struct sqlite3 sqlite3;
/// This is called a forward declaration or incomplete type.
/*
The compiler knows:

"There is a struct named sqlite3."

But it does not know:

its size
its members
its alignment

Therefore these are legal:

sqlite3* db;      // OK
sqlite3& ref;     // OK

because pointers and references have known sizes.

But these are illegal:

sqlite3 db;               // ERROR
sizeof(sqlite3);          // ERROR
new sqlite3;              // ERROR
*/

/// NOTE:
/*
1. One pointer, prepare every time
prepare INSERT
step
finalize

prepare INSERT
step
finalize

prepare INSERT
step
finalize

...
> One million prepares.


sqlite3_stmt* stmt = nullptr;

sqlite3_prepare_v2(db, "...SELECT...", -1, &stmt, nullptr);
sqlite3_step(stmt);
sqlite3_finalize(stmt);
stmt = nullptr;

sqlite3_prepare_v2(db, "...UPDATE...", -1, &stmt, nullptr);
sqlite3_step(stmt);
sqlite3_finalize(stmt);
stmt = nullptr;
-> Could use RAII to reduce the syntax

2. Keep one prepared statement
prepare INSERT      <-- once

bind
step
reset

bind
step
reset

bind
step
reset

...

finalize            <-- once

-> Only one prepare.
*/

/// NOTE: RAII 
/*
The RAII wrappers are there not because you want to finalize after every query, 
but because they guarantee that if something goes wrong after some resources have already been acquired, 
the resources that were successfully acquired are still released automatically when their owning objects are destroyed.
*/













// extern "C" {
// struct sqlite3;
// struct sqlite3_stmt;
// using sqlite3_int64 = long long;
// using sqlite3_destructor_type = void (*)(void*);
// int sqlite3_open(const char* filename, sqlite3** ppDb);
// int sqlite3_close(sqlite3*);
// int sqlite3_exec(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
// int sqlite3_prepare_v2(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
// int sqlite3_bind_int64(sqlite3_stmt*, int, sqlite3_int64);
// int sqlite3_bind_double(sqlite3_stmt*, int, double);
// int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, sqlite3_destructor_type);
// int sqlite3_bind_blob(sqlite3_stmt*, int, const void*, int, sqlite3_destructor_type);
// int sqlite3_bind_zeroblob(sqlite3_stmt*, int, int);
// int sqlite3_step(sqlite3_stmt*);
// int sqlite3_finalize(sqlite3_stmt* pStmt);
// int sqlite3_reset(sqlite3_stmt* pStmt);
// int sqlite3_clear_bindings(sqlite3_stmt* pStmt);
// int sqlite3_errcode(sqlite3*);
// const char* sqlite3_errmsg(sqlite3*);
// sqlite3_int64 sqlite3_column_int64(sqlite3_stmt*, int iCol);
// const unsigned char* sqlite3_column_text(sqlite3_stmt*, int iCol);
// const void* sqlite3_column_blob(sqlite3_stmt*, int iCol);
// int sqlite3_column_bytes(sqlite3_stmt*, int iCol);
//}

#include <stdexcept>
#include <string>

struct SqliteError : public std::runtime_error {
	int rc;
	explicit SqliteError(int rc_, const std::string& msg)
		: std::runtime_error(msg), rc(rc_) {}
};

struct sqlite3;
struct sqlite3_stmt;
using sqlite3_int64 = long long;
using sqlite3_destructor_type = void (*)(void*);

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

}
