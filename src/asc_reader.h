#pragma once

#include <istream>
#include <string>
#include <vector>

#include "parsed_entry_layout.h"

class ASCReader {
public:
	ASCReader(std::istream& file,
			  std::string base = "hex",
			  bool relative_timestamp = true);

	bool next(LogRecord& out);
	//std::vector<LogRecord> read_all();

private:
	// struct LogRecord;

	void extract_header();
	double datetime_to_timestamp(const std::string& datetime_string) const;
	void extract_can_id(const std::string& str_can_id, LogRecord& msg) const;
	static int check_base(const std::string& base);
	void process_data_string(const std::string& data_str,
							 int data_length,
							 LogRecord& msg) const;
	void process_classic_can_frame(const std::string& line, LogRecord& msg) const;
	void process_fd_can_frame(const std::string& line, LogRecord& msg) const;
	//LogRecord to_log_record(const LogRecord& msg) const;

	std::istream& file_;
	std::string base_;
	int converted_base_;
	bool relative_timestamp_;
	std::string date_;
	double start_time_;
	std::string timestamps_format_;
	bool internal_events_logged_;
	bool header_extracted_;
};
