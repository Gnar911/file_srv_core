#include "sqlite/sql_decode_if.h"

#include <algorithm>
#include <cstring>

#include "can_analyzer_log.h"

extern "C" {
struct sqlite3_stmt;
using sqlite3_int64 = long long;
using sqlite3_destructor_type = void (*)(void*);

int sqlite3_open(const char* filename, sqlite3** ppDb);
int sqlite3_close(sqlite3*);
int sqlite3_exec(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
int sqlite3_prepare_v2(sqlite3*, const char*, int, sqlite3_stmt**, const char**);
int sqlite3_bind_int64(sqlite3_stmt*, int, sqlite3_int64);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, sqlite3_destructor_type);
int sqlite3_bind_blob(sqlite3_stmt*, int, const void*, int, sqlite3_destructor_type);
int sqlite3_bind_zeroblob(sqlite3_stmt*, int, int);
int sqlite3_step(sqlite3_stmt*);
int sqlite3_finalize(sqlite3_stmt* pStmt);
int sqlite3_reset(sqlite3_stmt* pStmt);
int sqlite3_clear_bindings(sqlite3_stmt* pStmt);
int sqlite3_errcode(sqlite3*);
const char* sqlite3_errmsg(sqlite3*);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int iCol);
const void* sqlite3_column_blob(sqlite3_stmt*, int iCol);
int sqlite3_column_bytes(sqlite3_stmt*, int iCol);
}

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;

namespace {

constexpr int32_t kSqlDecodeRcOpenFailed = -311;
constexpr int32_t kSqlDecodeRcSchemaDbClosed = -312;
constexpr int32_t kSqlDecodeRcSchemaExecFailed = -313;
constexpr int32_t kSqlDecodeRcBeginDbClosed = -314;
constexpr int32_t kSqlDecodeRcBeginExecFailed = -315;
constexpr int32_t kSqlDecodeRcCommitDbClosed = -316;
constexpr int32_t kSqlDecodeRcCommitExecFailed = -317;
constexpr int32_t kSqlDecodeRcWriteDbClosed = -318;
constexpr int32_t kSqlDecodeRcWritePrepareFailed = -319;
constexpr int32_t kSqlDecodeRcWriteStepFailed = -320;

std::string signal_name_from_id(uint16_t signal_id) {
	return std::string("signal_") + std::to_string(signal_id);
}

template <typename T>
std::vector<uint8_t> to_blob(const std::vector<T>& values) {
	std::vector<uint8_t> out;
	if (values.empty()) {
		return out;
	}

	out.resize(values.size() * sizeof(T));
	std::memcpy(out.data(), values.data(), out.size());
	return out;
}

template <typename T>
std::vector<T> blob_to_vector(const void* blob, int bytes) {
	std::vector<T> out;
	if (blob == nullptr || bytes <= 0) {
		return out;
	}

	const int elem_size = static_cast<int>(sizeof(T));
	const int count = bytes / elem_size;
	out.resize(static_cast<size_t>(count));
	std::memcpy(out.data(), blob, static_cast<size_t>(count) * sizeof(T));
	return out;
}

template <typename T>
std::vector<T> slice_page(const std::vector<T>& values, uint64_t first_row, uint64_t page_size) {
	std::vector<T> out;
	if (values.empty()) {
		return out;
	}

	const size_t begin = std::min<size_t>(static_cast<size_t>(first_row), values.size());
	const size_t end = std::min<size_t>(begin + static_cast<size_t>(page_size), values.size());
	out.assign(values.begin() + begin, values.begin() + end);
	return out;
}

int bind_blob_or_empty(sqlite3_stmt* stmt, int index, const std::vector<uint8_t>& value) {
	if (value.empty()) {
		return sqlite3_bind_zeroblob(stmt, index, 0);
	}

	return sqlite3_bind_blob(stmt, index, value.data(), static_cast<int>(value.size()), nullptr);
}

void log_sqlite_error(sqlite3* db, const char* op, int rc) {
	CBCM_ERROR("%s failed rc=%d sqlite_msg=%s", op, rc, db != nullptr ? sqlite3_errmsg(db) : "<null-db>");
}

std::string format_sqlite_error(sqlite3* db, const char* op, int rc) {
	return std::string(op) + " rc=" + std::to_string(rc) + " sqlite_msg=" +
		(db != nullptr ? sqlite3_errmsg(db) : "<null-db>");
}

}  // namespace

DecodedSignalDatabase::DecodedSignalDatabase(const std::string& token_path)
	: db_path_(token_path + ".decoded.sqlite") {}

int32_t DecodedSignalDatabase::open() {
	if (db_ != nullptr) {
		return 0;
	}

	const int rc = sqlite3_open(db_path_.c_str(), &db_);
	if (rc != SQLITE_OK) {
		last_error_message_ = format_sqlite_error(db_, "sqlite3_open", rc);
		log_sqlite_error(db_, "sqlite3_open", rc);
		close();
		return kSqlDecodeRcOpenFailed;
	}

	last_error_message_.clear();
	sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
	sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
	return 0;
}

void DecodedSignalDatabase::close() {
	if (db_ != nullptr) {
		sqlite3_close(db_);
		db_ = nullptr;
	}
}

std::string DecodedSignalDatabase::db_path() const {
	return db_path_;
}

int32_t DecodedSignalDatabase::initialize_schema() {
	if (db_ == nullptr) {
		last_error_message_ = "initialize_schema failed: db closed";
		return kSqlDecodeRcSchemaDbClosed;
	}

	static const char* kCreateSql =
		"CREATE TABLE IF NOT EXISTS decoded_signal_data ("
		"  can_id INTEGER NOT NULL,"
		"  signal_name TEXT NOT NULL,"
		"  row_index_blob BLOB NOT NULL DEFAULT X'',"
		"  raw_value_blob BLOB NOT NULL DEFAULT X'',"
		"  phys_value_blob BLOB NOT NULL DEFAULT X'',"
		"  changed_row_blob BLOB NOT NULL DEFAULT X'',"
		"  PRIMARY KEY (can_id, signal_name)"
		");";

	const int rc = sqlite3_exec(db_, kCreateSql, nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		last_error_message_ = format_sqlite_error(db_, "initialize_schema sqlite3_exec", rc);
		log_sqlite_error(db_, "initialize_schema sqlite3_exec", rc);
		return kSqlDecodeRcSchemaExecFailed;
	}
	last_error_message_.clear();
	return 0;
}

int32_t DecodedSignalDatabase::begin_transaction() {
	if (db_ == nullptr) {
		last_error_message_ = "begin_transaction failed: db closed";
		return kSqlDecodeRcBeginDbClosed;
	}
	const int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		last_error_message_ = format_sqlite_error(db_, "begin_transaction sqlite3_exec", rc);
		log_sqlite_error(db_, "begin_transaction sqlite3_exec", rc);
		return kSqlDecodeRcBeginExecFailed;
	}
	last_error_message_.clear();
	return 0;
}

int32_t DecodedSignalDatabase::commit_transaction() {
	if (db_ == nullptr) {
		last_error_message_ = "commit_transaction failed: db closed";
		return kSqlDecodeRcCommitDbClosed;
	}
	const int rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		last_error_message_ = format_sqlite_error(db_, "commit_transaction sqlite3_exec", rc);
		log_sqlite_error(db_, "commit_transaction sqlite3_exec", rc);
		return kSqlDecodeRcCommitExecFailed;
	}
	last_error_message_.clear();
	return 0;
}

int32_t DecodedSignalDatabase::write_signals(const std::vector<DecodedSignalChunk>& chunks) {
	if (db_ == nullptr) {
		last_error_message_ = "write_signals failed: db closed";
		return kSqlDecodeRcWriteDbClosed;
	}

	static const char* kUpsertSql =
		"INSERT INTO decoded_signal_data ("
		"  can_id, signal_name, row_index_blob, raw_value_blob, phys_value_blob, changed_row_blob"
		") VALUES (?, ?, ?, ?, ?, ?)"
		"ON CONFLICT(can_id, signal_name) DO UPDATE SET "
		"  row_index_blob = decoded_signal_data.row_index_blob || excluded.row_index_blob,"
		"  raw_value_blob = decoded_signal_data.raw_value_blob || excluded.raw_value_blob,"
		"  phys_value_blob = decoded_signal_data.phys_value_blob || excluded.phys_value_blob,"
		"  changed_row_blob = decoded_signal_data.changed_row_blob || excluded.changed_row_blob;";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_, kUpsertSql, -1, &stmt, nullptr) != SQLITE_OK) {
		last_error_message_ = format_sqlite_error(db_, "write_signals sqlite3_prepare_v2", sqlite3_errcode(db_));
		log_sqlite_error(db_, "write_signals sqlite3_prepare_v2", sqlite3_errcode(db_));
		return kSqlDecodeRcWritePrepareFailed;
	}

	for (const auto& chunk : chunks) {
		const std::vector<uint8_t> row_blob = to_blob(chunk.row_index);
		const std::vector<uint8_t> raw_blob = to_blob(chunk.raw_value);
		const std::vector<uint8_t> phys_blob = to_blob(chunk.phys_value);
		const std::vector<uint8_t> changed_blob = to_blob(chunk.changed_row_index);

		sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(chunk.can_id));
		sqlite3_bind_text(stmt, 2, chunk.signal_name.c_str(), -1, nullptr);
		if (bind_blob_or_empty(stmt, 3, row_blob) != SQLITE_OK ||
			bind_blob_or_empty(stmt, 4, raw_blob) != SQLITE_OK ||
			bind_blob_or_empty(stmt, 5, phys_blob) != SQLITE_OK ||
			bind_blob_or_empty(stmt, 6, changed_blob) != SQLITE_OK) {
			last_error_message_ = format_sqlite_error(db_, "write_signals sqlite3_bind_*", sqlite3_errcode(db_));
			log_sqlite_error(db_, "write_signals sqlite3_bind_*", sqlite3_errcode(db_));
			sqlite3_finalize(stmt);
			return kSqlDecodeRcWriteStepFailed;
		}

		const int step_rc = sqlite3_step(stmt);
		if (step_rc != SQLITE_DONE) {
			last_error_message_ = format_sqlite_error(db_, "write_signals sqlite3_step", step_rc);
			log_sqlite_error(db_, "write_signals sqlite3_step", step_rc);
			sqlite3_finalize(stmt);
			return kSqlDecodeRcWriteStepFailed;
		}

		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
	}

	sqlite3_finalize(stmt);
	last_error_message_.clear();
	return 0;
}

std::vector<std::string> DecodedSignalDatabase::get_signal_names(uint32_t can_id) {
	std::vector<std::string> names;
	if (db_ == nullptr) {
		return names;
	}

	static const char* kQuerySql =
		"SELECT signal_name FROM decoded_signal_data WHERE can_id = ? ORDER BY signal_name;";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_, kQuerySql, -1, &stmt, nullptr) != SQLITE_OK) {
		return names;
	}

	sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(can_id));
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char* name = sqlite3_column_text(stmt, 0);
		if (name != nullptr) {
			names.emplace_back(reinterpret_cast<const char*>(name));
		}
	}

	sqlite3_finalize(stmt);
	return names;
}

DecodedSignalChunk DecodedSignalDatabase::get_signal_samples(uint32_t can_id, const std::string& signal_name) {
	DecodedSignalChunk chunk;
	chunk.can_id = can_id;
	chunk.signal_name = signal_name;

	if (db_ == nullptr) {
		return chunk;
	}

	static const char* kQuerySql =
		"SELECT row_index_blob, raw_value_blob, phys_value_blob, changed_row_blob "
		"FROM decoded_signal_data WHERE can_id = ? AND signal_name = ? LIMIT 1;";

	sqlite3_stmt* stmt = nullptr;
	if (sqlite3_prepare_v2(db_, kQuerySql, -1, &stmt, nullptr) != SQLITE_OK) {
		return chunk;
	}

	sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(can_id));
	sqlite3_bind_text(stmt, 2, signal_name.c_str(), -1, nullptr);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		chunk.row_index = blob_to_vector<uint32_t>(
			sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
		chunk.raw_value = blob_to_vector<int64_t>(
			sqlite3_column_blob(stmt, 1), sqlite3_column_bytes(stmt, 1));
		chunk.phys_value = blob_to_vector<double>(
			sqlite3_column_blob(stmt, 2), sqlite3_column_bytes(stmt, 2));
		chunk.changed_row_index = blob_to_vector<uint32_t>(
			sqlite3_column_blob(stmt, 3), sqlite3_column_bytes(stmt, 3));
	}

	sqlite3_finalize(stmt);
	return chunk;
}

const std::string& DecodedSignalDatabase::last_error_message() const {
	return last_error_message_;
}
