#include "sqlite/sql_decode_if.h"

#include <algorithm>
#include <cstring>

#include "can_analyzer_log.h"

// Use centralized SQLite ABI declarations and thin C++ wrappers.
#include "sqlite/sqlite_wrapper.h"

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

void bind_blob_or_empty(sqlite3_stmt* stmt, int index, const std::vector<uint8_t>& value) {
	if (value.empty()) {
		sqlitew::bind_zeroblob(stmt, index, 0);
		return;
	}

	sqlitew::bind_blob(stmt, index, value.data(), static_cast<int>(value.size()), nullptr);
}

void log_sqlite_error(sqlite3* db, const char* op, int rc) {
	CBCM_ERROR("%s failed rc=%d sqlite_msg=%s", op, rc, db != nullptr ? sqlitew::errmsg(db) : "<null-db>");
}

std::string format_sqlite_error(sqlite3* db, const char* op, int rc) {
	return std::string(op) + " rc=" + std::to_string(rc) + " sqlite_msg=" +
		(db != nullptr ? sqlitew::errmsg(db) : "<null-db>");
}

}  // namespace

DecodedSignalDatabase::DecodedSignalDatabase(const std::string& token_path)
	: db_path_(token_path + ".decoded.sqlite") {}

int32_t DecodedSignalDatabase::open() {
	if (db_ != nullptr) {
		return 0;
	}

	sqlitew::open(db_path_.c_str(), &db_);

	last_error_message_.clear();
	sqlitew::exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
	sqlitew::exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
	return 0;
}

void DecodedSignalDatabase::close() {
	if (db_ != nullptr) {
		sqlitew::close(db_);
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

	sqlitew::exec(db_, kCreateSql, nullptr, nullptr, nullptr);
	last_error_message_.clear();
	return 0;
}

int32_t DecodedSignalDatabase::begin_transaction() {
	if (db_ == nullptr) {
		last_error_message_ = "begin_transaction failed: db closed";
		return kSqlDecodeRcBeginDbClosed;
	}
	sqlitew::exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
	last_error_message_.clear();
	return 0;
}

int32_t DecodedSignalDatabase::commit_transaction() {
	if (db_ == nullptr) {
		last_error_message_ = "commit_transaction failed: db closed";
		return kSqlDecodeRcCommitDbClosed;
	}
	sqlitew::exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
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
	sqlitew::prepare_v2(db_, kUpsertSql, -1, &stmt, nullptr);

	for (const auto& chunk : chunks) {
		const std::vector<uint8_t> row_blob = to_blob(chunk.row_index);
		const std::vector<uint8_t> raw_blob = to_blob(chunk.raw_value);
		const std::vector<uint8_t> phys_blob = to_blob(chunk.phys_value);
		const std::vector<uint8_t> changed_blob = to_blob(chunk.changed_row_index);

		sqlitew::bind_int64(stmt, 1, static_cast<sqlite3_int64>(chunk.can_id));
		sqlitew::bind_text(stmt, 2, chunk.signal_name.c_str(), -1, nullptr);
		bind_blob_or_empty(stmt, 3, row_blob);
		bind_blob_or_empty(stmt, 4, raw_blob);
		bind_blob_or_empty(stmt, 5, phys_blob);
		bind_blob_or_empty(stmt, 6, changed_blob);
		const int step_rc = sqlitew::step(stmt);
		if (step_rc != SQLITE_DONE) {
			last_error_message_ = format_sqlite_error(db_, "write_signals sqlite3_step", step_rc);
			log_sqlite_error(db_, "write_signals sqlite3_step", step_rc);
			sqlitew::finalize(stmt);
			return kSqlDecodeRcWriteStepFailed;
		}

		sqlitew::reset(stmt);
		sqlitew::clear_bindings(stmt);
	}

	sqlitew::finalize(stmt);
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
	sqlitew::prepare_v2(db_, kQuerySql, -1, &stmt, nullptr);

	sqlitew::bind_int64(stmt, 1, static_cast<sqlite3_int64>(can_id));
	while (sqlitew::step(stmt) == SQLITE_ROW) {
		const unsigned char* name = sqlitew::column_text(stmt, 0);
		if (name != nullptr) {
			names.emplace_back(reinterpret_cast<const char*>(name));
		}
	}

	sqlitew::finalize(stmt);
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
	sqlitew::prepare_v2(db_, kQuerySql, -1, &stmt, nullptr);

	sqlitew::bind_int64(stmt, 1, static_cast<sqlite3_int64>(can_id));
	sqlitew::bind_text(stmt, 2, signal_name.c_str(), -1, nullptr);

	if (sqlitew::step(stmt) == SQLITE_ROW) {
		chunk.row_index = blob_to_vector<uint32_t>(
			sqlitew::column_blob(stmt, 0), sqlitew::column_bytes(stmt, 0));
		chunk.raw_value = blob_to_vector<int64_t>(
			sqlitew::column_blob(stmt, 1), sqlitew::column_bytes(stmt, 1));
		chunk.phys_value = blob_to_vector<double>(
			sqlitew::column_blob(stmt, 2), sqlitew::column_bytes(stmt, 2));
		chunk.changed_row_index = blob_to_vector<uint32_t>(
			sqlitew::column_blob(stmt, 3), sqlitew::column_bytes(stmt, 3));
	}

	sqlitew::finalize(stmt);
	return chunk;
}

const std::string& DecodedSignalDatabase::last_error_message() const {
	return last_error_message_;
}
