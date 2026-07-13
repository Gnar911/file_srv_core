#include "sqlite/sql_decode_if.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "can_analyzer_log.h"

// Use centralized SQLite ABI declarations and thin C++ wrappers.
#include "sqlite/sqlite_wrapper.h"

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;

namespace {

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

// helper removed: use Statement::bind_zeroblob / bind_blob directly

}  // namespace

DecodedSignalDatabase::DecodedSignalDatabase(const std::string& token_path)
	: db_path_(token_path + ".decoded.sqlite"),
	  db_(db_path_) {
	static const char* kCreateSql =
		"CREATE TABLE IF NOT EXISTS decoded_signal_data ("
		"  can_id INTEGER NOT NULL,"
		"  signal_name TEXT NOT NULL,"
		"  row_index_blob BLOB NOT NULL DEFAULT X'',"
		"  raw_value_blob BLOB NOT NULL DEFAULT X'',"
		"  phys_value_blob BLOB NOT NULL DEFAULT X'',"
		"  PRIMARY KEY (can_id, signal_name)"
		");";

	sqlitew::exec(db_.get(), kCreateSql, nullptr, nullptr, nullptr);
}

std::string DecodedSignalDatabase::db_path() const {
	return db_path_;
}

// Schema initialization moved to constructor for RAII semantics.

int32_t DecodedSignalDatabase::begin_transaction() {
	sqlitew::exec(db_.get(), "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
	return 0;
}

int32_t DecodedSignalDatabase::commit_transaction() {
	sqlitew::exec(db_.get(), "COMMIT;", nullptr, nullptr, nullptr);
	return 0;
}

int32_t DecodedSignalDatabase::write_signals(const std::vector<DecodedSignalChunk>& chunks) {
	static const char* kUpsertSql =
		"INSERT INTO decoded_signal_data ("
		"  can_id, signal_name, row_index_blob, raw_value_blob, phys_value_blob"
		") VALUES (?, ?, ?, ?, ?)"
		"ON CONFLICT(can_id, signal_name) DO UPDATE SET "
		"  row_index_blob = decoded_signal_data.row_index_blob || excluded.row_index_blob,"
		"  raw_value_blob = decoded_signal_data.raw_value_blob || excluded.raw_value_blob,"
		"  phys_value_blob = decoded_signal_data.phys_value_blob || excluded.phys_value_blob;";

	if (!upsert_stmt_.is_prepared()) {
		upsert_stmt_ = db_.prepare(kUpsertSql);
	}

	for (const auto& chunk : chunks) {
		const std::vector<uint8_t> row_blob = to_blob(chunk.row_index);
		const std::vector<uint8_t> raw_blob = to_blob(chunk.raw_value);
		const std::vector<uint8_t> phys_blob = to_blob(chunk.phys_value);
		upsert_stmt_.bind_int64(1, static_cast<sqlite3_int64>(chunk.can_id));
		upsert_stmt_.bind_text(2, chunk.signal_name.c_str(), -1, nullptr);
		if (row_blob.empty()) upsert_stmt_.bind_zeroblob(3, 0); else upsert_stmt_.bind_blob(3, row_blob.data(), static_cast<int>(row_blob.size()), nullptr);
		if (raw_blob.empty()) upsert_stmt_.bind_zeroblob(4, 0); else upsert_stmt_.bind_blob(4, raw_blob.data(), static_cast<int>(raw_blob.size()), nullptr);
		if (phys_blob.empty()) upsert_stmt_.bind_zeroblob(5, 0); else upsert_stmt_.bind_blob(5, phys_blob.data(), static_cast<int>(phys_blob.size()), nullptr);
		const int step_rc = upsert_stmt_.step();
		upsert_stmt_.reset_and_clear();
	}

	return 0;
}

std::vector<std::string> DecodedSignalDatabase::get_signal_names(uint32_t can_id) {
	std::vector<std::string> names;

	static const char* kQuerySql =
		"SELECT signal_name FROM decoded_signal_data WHERE can_id = ? ORDER BY signal_name;";

	Statement stmt = db_.prepare(kQuerySql);

	stmt.bind_int64(1, static_cast<sqlite3_int64>(can_id));
	while (stmt.step() == SQLITE_ROW) {
		const unsigned char* name = stmt.column_text(0);
		if (name != nullptr) {
			names.emplace_back(reinterpret_cast<const char*>(name));
		}
	}
	return names;
}

DecodedSignalChunk DecodedSignalDatabase::get_signal_samples(uint32_t can_id, const std::string& signal_name) {
	DecodedSignalChunk chunk;
	chunk.can_id = can_id;
	chunk.signal_name = signal_name;

	static const char* kQuerySql =
		"SELECT row_index_blob, raw_value_blob, phys_value_blob "
		"FROM decoded_signal_data WHERE can_id = ? AND signal_name = ? LIMIT 1;";

	Statement stmt = db_.prepare(kQuerySql);

	stmt.bind_int64(1, static_cast<sqlite3_int64>(can_id));
	stmt.bind_text(2, signal_name.c_str(), -1, nullptr);

	if (stmt.step() == SQLITE_ROW) {
		chunk.row_index = blob_to_vector<uint32_t>(
			stmt.column_blob(0), stmt.column_bytes(0));
		chunk.raw_value = blob_to_vector<int64_t>(
			stmt.column_blob(1), stmt.column_bytes(1));
		chunk.phys_value = blob_to_vector<double>(
			stmt.column_blob(2), stmt.column_bytes(2));
	}
	return chunk;
}

