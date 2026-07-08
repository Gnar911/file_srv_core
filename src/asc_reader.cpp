#include "asc_reader.h"
#include "parsed_entry_layout.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include "can_analyzer_log.h"

namespace {

constexpr int BASE_HEX = 16;
constexpr int BASE_DEC = 10;

const std::regex kAscTriggerRegex(
	R"(^begin\s+triggerblock\s+\w+\s+(.+)$)",
	std::regex::icase);
const std::regex kLogRecordRegex(
	R"(^\d+\.\d+\s+(\d+\s+(\w+\s+(Tx|Rx)|ErrorFrame)|CANFD))",
	std::regex::icase);
const std::regex kDateRegex(
	R"(^date\s+\w+\s+(.+)$)",
	std::regex::icase);
const std::regex kBaseRegex(
	R"(^base\s+(hex|dec)(?:\s+timestamps\s+(absolute|relative))?$)",
	std::regex::icase);
const std::regex kCommentRegex(R"(^//.*$)");
const std::regex kEventsRegex(
	R"(^(no)?\s*internal\s+events\s+logged$)",
	std::regex::icase);
const std::regex kStartOfMeasurementRegex(
	R"(^\d+\.\d+\s+Start of measurement$)");

struct ParsedDateTime {
	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;
	double fractional_seconds;
};

static std::string trim_copy(const std::string& s) {
	std::size_t start = 0;
	while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
		++start;
	}
	std::size_t end = s.size();
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		--end;
	}
	return s.substr(start, end - start);
}

static std::string to_lower_ascii(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
}

static bool is_digits(const std::string& s) {
	if (s.empty()) {
		return false;
	}
	return std::all_of(s.begin(), s.end(), [](unsigned char c) {
		return std::isdigit(c) != 0;
	});
}

static bool iequals(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) !=
			std::tolower(static_cast<unsigned char>(b[i]))) {
			return false;
		}
	}
	return true;
}

static std::vector<std::string> split_whitespace(const std::string& s) {
	std::vector<std::string> out;
	std::istringstream iss(s);
	std::string tok;
	while (iss >> tok) {
		out.push_back(tok);
	}
	return out;
}

static std::vector<std::string> split_whitespace_limit(const std::string& s, std::size_t max_splits) {
	std::vector<std::string> out;
	std::size_t i = 0;
	std::size_t n = s.size();
	while (i < n && out.size() < max_splits) {
		while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) {
			++i;
		}
		if (i >= n) {
			break;
		}
		std::size_t start = i;
		while (i < n && !std::isspace(static_cast<unsigned char>(s[i]))) {
			++i;
		}
		out.push_back(s.substr(start, i - start));
	}
	while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) {
		++i;
	}
	if (i < n) {
		out.push_back(s.substr(i));
	}
	return out;
}

static int dlc2len_compat(int dlc) {
	static constexpr std::array<int, 16> kDlcMap = {
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 12, 16, 20, 24, 32, 48, 64
	};
	if (dlc < 0 || dlc >= static_cast<int>(kDlcMap.size())) {
		return dlc;
	}
	return kDlcMap[dlc];
}

static std::string replace_all_copy(std::string text,
									const std::string& from,
									const std::string& to) {
	if (from.empty()) {
		return text;
	}
	std::size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos) {
		text.replace(pos, from.size(), to);
		pos += to.size();
	}
	return text;
}

static double fractional_to_seconds(const std::string& frac) {
	if (frac.empty()) {
		return 0.0;
	}
	double value = 0.0;
	double scale = 1.0;
	for (char c : frac) {
		value = value * 10.0 + static_cast<double>(c - '0');
		scale *= 10.0;
	}
	return value / scale;
}

static bool parse_pattern_12h_with_frac(const std::string& s, ParsedDateTime& out) {
	static const std::regex re(
		R"(^(\d{1,2})\s+(\d{1,2})\s+(\d{1,2}):(\d{2}):(\d{2})\.(\d{1,6})\s+([AaPp][Mm])\s+(\d{4})$)");
	std::smatch m;
	if (!std::regex_match(s, m, re)) {
		return false;
	}

	int hour = std::stoi(m[3].str());
	std::string ampm = m[7].str();
	if (iequals(ampm, "PM") && hour < 12) {
		hour += 12;
	} else if (iequals(ampm, "AM") && hour == 12) {
		hour = 0;
	}

	out.month = std::stoi(m[1].str());
	out.day = std::stoi(m[2].str());
	out.hour = hour;
	out.minute = std::stoi(m[4].str());
	out.second = std::stoi(m[5].str());
	out.fractional_seconds = fractional_to_seconds(m[6].str());
	out.year = std::stoi(m[8].str());
	return true;
}

static bool parse_pattern_12h_no_frac(const std::string& s, ParsedDateTime& out) {
	static const std::regex re(
		R"(^(\d{1,2})\s+(\d{1,2})\s+(\d{1,2}):(\d{2}):(\d{2})\s+([AaPp][Mm])\s+(\d{4})$)");
	std::smatch m;
	if (!std::regex_match(s, m, re)) {
		return false;
	}

	int hour = std::stoi(m[3].str());
	std::string ampm = m[6].str();
	if (iequals(ampm, "PM") && hour < 12) {
		hour += 12;
	} else if (iequals(ampm, "AM") && hour == 12) {
		hour = 0;
	}

	out.month = std::stoi(m[1].str());
	out.day = std::stoi(m[2].str());
	out.hour = hour;
	out.minute = std::stoi(m[4].str());
	out.second = std::stoi(m[5].str());
	out.fractional_seconds = 0.0;
	out.year = std::stoi(m[7].str());
	return true;
}

static bool parse_pattern_24h_with_frac(const std::string& s, ParsedDateTime& out) {
	static const std::regex re(
		R"(^(\d{1,2})\s+(\d{1,2})\s+(\d{1,2}):(\d{2}):(\d{2})\.(\d{1,6})\s+(\d{4})$)");
	std::smatch m;
	if (!std::regex_match(s, m, re)) {
		return false;
	}

	out.month = std::stoi(m[1].str());
	out.day = std::stoi(m[2].str());
	out.hour = std::stoi(m[3].str());
	out.minute = std::stoi(m[4].str());
	out.second = std::stoi(m[5].str());
	out.fractional_seconds = fractional_to_seconds(m[6].str());
	out.year = std::stoi(m[7].str());
	return true;
}

static bool parse_pattern_24h_no_frac(const std::string& s, ParsedDateTime& out) {
	static const std::regex re(
		R"(^(\d{1,2})\s+(\d{1,2})\s+(\d{1,2}):(\d{2}):(\d{2})\s+(\d{4})$)");
	std::smatch m;
	if (!std::regex_match(s, m, re)) {
		return false;
	}

	out.month = std::stoi(m[1].str());
	out.day = std::stoi(m[2].str());
	out.hour = std::stoi(m[3].str());
	out.minute = std::stoi(m[4].str());
	out.second = std::stoi(m[5].str());
	out.fractional_seconds = 0.0;
	out.year = std::stoi(m[6].str());
	return true;
}

static double parsed_datetime_to_timestamp(const ParsedDateTime& dt) {
	std::tm tm{};
	tm.tm_year = dt.year - 1900;
	tm.tm_mon = dt.month - 1;
	tm.tm_mday = dt.day;
	tm.tm_hour = dt.hour;
	tm.tm_min = dt.minute;
	tm.tm_sec = dt.second;
	tm.tm_isdst = -1;

	const std::time_t t = std::mktime(&tm);
	if (t == static_cast<std::time_t>(-1)) {
		throw std::runtime_error("Incompatible datetime string");
	}
	return static_cast<double>(t) + dt.fractional_seconds;
}

} // namespace
// struct ASCReader::LogRecord {
// 	double timestamp = 0.0;
// 	bool is_fd = false;
// 	bool direction = false;
// 	bool is_extended_id = false;
// 	bool is_error_frame = false;
// 	bool is_remote_frame = false;
// 	bool bitrate_switch = false;
// 	bool error_state_indicator = false;
// 	char channel[16]{};
// 	uint32_t can_id = 0;
// 	uint8_t dlc = 0;
// 	std::vector<uint8_t> data;
// };

ASCReader::ASCReader(std::istream& file,
					 std::string base,
					 bool relative_timestamp)
	: file_(file),
	  base_(std::move(base)),
	  converted_base_(check_base(base_)),
	  relative_timestamp_(relative_timestamp),
	  date_(),
	  start_time_(0.0),
	  timestamps_format_(),
	  internal_events_logged_(false),
	  header_extracted_(false) {
}

int ASCReader::check_base(const std::string& base) {
	if (!iequals(base, "hex") && !iequals(base, "dec")) {
		throw std::invalid_argument("base should be either \"hex\" or \"dec\"");
	}
	return iequals(base, "dec") ? BASE_DEC : BASE_HEX;
}

double ASCReader::datetime_to_timestamp(const std::string& datetime_string) const {
	std::string normalized = datetime_string;
	normalized = replace_all_copy(normalized, "Jan", "01");
	normalized = replace_all_copy(normalized, "Feb", "02");
	normalized = replace_all_copy(normalized, "Mar", "03");
	normalized = replace_all_copy(normalized, "Apr", "04");
	normalized = replace_all_copy(normalized, "May", "05");
	normalized = replace_all_copy(normalized, "Jun", "06");
	normalized = replace_all_copy(normalized, "Jul", "07");
	normalized = replace_all_copy(normalized, "Aug", "08");
	normalized = replace_all_copy(normalized, "Sep", "09");
	normalized = replace_all_copy(normalized, "Oct", "10");
	normalized = replace_all_copy(normalized, "Nov", "11");
	normalized = replace_all_copy(normalized, "Dec", "12");
	normalized = replace_all_copy(normalized, "M\xC3\xA4r", "03");
	normalized = replace_all_copy(normalized, "Mai", "05");
	normalized = replace_all_copy(normalized, "Okt", "10");
	normalized = replace_all_copy(normalized, "Dez", "12");

	ParsedDateTime parsed{};
	if (parse_pattern_12h_with_frac(normalized, parsed) ||
		parse_pattern_12h_no_frac(normalized, parsed) ||
		parse_pattern_24h_with_frac(normalized, parsed) ||
		parse_pattern_24h_no_frac(normalized, parsed)) {
		return parsed_datetime_to_timestamp(parsed);
	}

	throw std::runtime_error("Incompatible datetime string " + datetime_string);
}

void ASCReader::extract_header() {
	std::string raw;
	while (std::getline(file_, raw)) {
		const std::string line = trim_copy(raw);
		std::smatch match;

		if (std::regex_match(line, match, kDateRegex)) {
			date_ = match[1].str();
			start_time_ = relative_timestamp_ ? 0.0 : datetime_to_timestamp(date_);
			continue;
		}

		if (std::regex_match(line, match, kBaseRegex)) {
			base_ = match[1].str();
			converted_base_ = check_base(base_);
			if (match[2].matched) {
				timestamps_format_ = match[2].str();
			} else {
				timestamps_format_ = "absolute";
			}
			continue;
		}

		if (std::regex_match(line, kCommentRegex)) {
			continue;
		}

		if (std::regex_match(line, match, kEventsRegex)) {
			internal_events_logged_ = !match[1].matched;
			break;
		}

		break;
	}
}

void ASCReader::extract_can_id(const std::string& str_can_id, LogRecord& msg) const {
	std::string token = str_can_id;
	if (!token.empty() && (token.back() == 'x' || token.back() == 'X')) {
		msg.is_extended_id = true;
		token.pop_back();
	} else {
		msg.is_extended_id = false;
	}

	std::size_t pos = 0;
	const unsigned long parsed = std::stoul(token, &pos, converted_base_);
	if (pos != token.size()) {
		throw std::runtime_error("Invalid CAN ID token: " + str_can_id);
	}
	msg.can_id = static_cast<uint32_t>(parsed);
}

void ASCReader::process_data_string(const std::string& data_str,
									int data_length,
									LogRecord& msg) const {
	const auto tokens = split_whitespace(data_str);
	const int count = std::min<int>(data_length, static_cast<int>(tokens.size()));
	// zero the backing array
	std::memset(msg.data, 0, sizeof(msg.data));
	for (int i = 0; i < count; ++i) {
		std::size_t pos = 0;
		const unsigned long value = std::stoul(tokens[i], &pos, converted_base_);
		if (pos != tokens[i].size() || value > 0xFFu) {
			throw std::runtime_error("Invalid data byte token: " + tokens[i]);
		}
		msg.data[i] = static_cast<uint8_t>(value);
	}
	// record how many bytes we actually wrote into the fixed array
	msg.data_len = static_cast<uint8_t>(std::min<int>(count, static_cast<int>(sizeof(msg.data))));
}

void ASCReader::process_classic_can_frame(const std::string& line, LogRecord& msg) const {
	const std::string stripped = trim_copy(line);
	const std::string prefix = to_lower_ascii(stripped.substr(0, std::min<std::size_t>(10, stripped.size())));
	if (prefix == "errorframe") {
		msg.is_error_frame = true;
		return;
	}

	const std::vector<std::string> parts = split_whitespace_limit(line, 2);
	if (parts.size() < 3) {
		throw std::runtime_error("Invalid classic CAN frame");
	}

	const std::string& abr_id_str = parts[0];
	const std::string& direction = parts[1];
	const std::string& rest_of_message = parts[2];

	msg.direction = (direction == "Rx");
	extract_can_id(abr_id_str, msg);

	if (!rest_of_message.empty() &&
		std::tolower(static_cast<unsigned char>(rest_of_message[0])) == 'r') {
		msg.is_remote_frame = true;
		const std::vector<std::string> remote_data = split_whitespace(rest_of_message);
		if (remote_data.size() > 1 && is_digits(remote_data[1])) {
			const int dlc_raw = std::stoi(remote_data[1], nullptr, converted_base_);
			msg.data_len = static_cast<uint8_t>(dlc2len_compat(dlc_raw));
		}
	} else {
		const std::vector<std::string> message_parts = split_whitespace_limit(rest_of_message, 2);

		std::string dlc_str;
		std::string data;
		if (message_parts.size() >= 3) {
			dlc_str = message_parts[1];
			data = message_parts[2];
		} else if (message_parts.size() >= 2) {
			dlc_str = message_parts[1];
			data.clear();
		} else {
			throw std::runtime_error("Invalid classic CAN payload");
		}

		const int dlc_raw = std::stoi(dlc_str, nullptr, converted_base_);
		const int dlc = dlc2len_compat(dlc_raw);
		msg.data_len = static_cast<uint8_t>(dlc);
		process_data_string(data, std::min(8, dlc), msg);
	}
}

void ASCReader::process_fd_can_frame(const std::string& line, LogRecord& msg) const {
	const std::vector<std::string> parts = split_whitespace_limit(line, 1);
	if (parts.size() < 2) {
		throw std::runtime_error("Invalid CAN FD frame");
	}

	const std::string& channel = parts[0];
	std::strncpy(msg.channel, channel.c_str(), sizeof(msg.channel) - 1);
	msg.channel[sizeof(msg.channel) - 1] = '\0';

	const std::string stripped = trim_copy(parts[1]);
	const std::string prefix = to_lower_ascii(stripped.substr(0, std::min<std::size_t>(10, stripped.size())));
	if (prefix == "errorframe") {
		msg.is_error_frame = true;
		return;
	}

	const std::vector<std::string> dir_parts = split_whitespace_limit(parts[1], 1);
	if (dir_parts.size() < 2) {
		throw std::runtime_error("Invalid CAN FD frame");
	}
	const std::string& direction = dir_parts[0];
	const std::string& rest_of_message = dir_parts[1];
	msg.direction = (direction == "Rx");

	const std::vector<std::string> frame_parts = split_whitespace_limit(rest_of_message, 2);
	if (frame_parts.size() < 3) {
		throw std::runtime_error("Invalid CAN FD payload");
	}

	const std::string& can_id_str = frame_parts[0];
	const std::string& frame_name_or_brs = frame_parts[1];
	const std::string& frame_rest = frame_parts[2];

	std::string brs;
	std::string esi;
	std::string dlc_str;
	std::string data_length_str;
	std::string data;

    if (is_digits(frame_name_or_brs)) {
        brs = frame_name_or_brs;
        const auto fd_parts = split_whitespace_limit(frame_rest, 3);

        if (fd_parts.size() == 3 &&
            //fd_parts[1] == "0" &&
            fd_parts[2] == "0") {
            // Special case: zero-length CAN FD frame with omitted data field.
            esi = fd_parts[0];
            dlc_str = fd_parts[1];
            data_length_str = fd_parts[2];
            data.clear();
        } else if (fd_parts.size() == 4) {
            esi = fd_parts[0];
            dlc_str = fd_parts[1];
            data_length_str = fd_parts[2];
            data = fd_parts[3];
        } else {
            throw std::runtime_error("Invalid CAN FD payload layout");
        }
    } else {
        const auto fd_parts = split_whitespace_limit(frame_rest, 4);

        if (fd_parts.size() == 4 &&
            //fd_parts[2] == "0" &&
            fd_parts[3] == "0") {
            // Special case: zero-length CAN FD frame with omitted data field.
            brs = fd_parts[0];
            esi = fd_parts[1];
            dlc_str = fd_parts[2];
            data_length_str = fd_parts[3];
            data.clear();
        } else if (fd_parts.size() == 5) {
            brs = fd_parts[0];
            esi = fd_parts[1];
            dlc_str = fd_parts[2];
            data_length_str = fd_parts[3];
            data = fd_parts[4];
        } else {
            throw std::runtime_error("Invalid CAN FD payload layout");
        }
    }

	extract_can_id(can_id_str, msg);
	msg.bitrate_switch = (brs == "1");
	msg.error_state_indicator = (esi == "1");

	const int dlc = std::stoi(dlc_str, nullptr, converted_base_);
	const int data_length = std::stoi(data_length_str);
	if (data_length == 0) {
		msg.is_remote_frame = true;
		msg.data_len = static_cast<uint8_t>(dlc2len_compat(dlc));
	} else {
		if (dlc2len_compat(dlc) != data_length) {
			CBCM_ERROR("DLC vs Data Length mismatch %d[%d] != %d",
						dlc, dlc2len_compat(dlc), data_length);
		}
		msg.data_len = static_cast<uint8_t>(data_length);
	}

	process_data_string(data, data_length, msg);
}

// LogRecord ASCReader::to_log_record(const LogRecord& msg) const {
// 	LogRecord out{};
// 	out.timestamp = msg.timestamp;
// 	out.can_id = msg.can_id;
// 	out.direction = msg.direction ? 0 : 1;
//  // copy data_len and data bytes (msg.data_len already set by parsers)
//  out.data_len = msg.data_len;
//  const std::size_t copy_len = std::min<std::size_t>(static_cast<std::size_t>(msg.data_len), sizeof(out.data));
// 	for (std::size_t i = 0; i < copy_len; ++i) {
// 		out.data[i] = msg.data[i];
// 	}
// 	// zero remaining bytes
// 	if (copy_len < sizeof(out.data)) {
// 		std::memset(out.data + copy_len, 0, sizeof(out.data) - copy_len);
// 	}

// 	// copy channel string if present
// 	if (msg.channel[0] != '\0') {
// 		std::strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
// 		out.channel[sizeof(out.channel) - 1] = '\0';
// 	} else {
// 		out.channel[0] = '\0';
// 	}

// 	return out;
// }

bool ASCReader::next(LogRecord& out) {
	// if (!header_extracted_) {
	// 	extract_header();
	// 	header_extracted_ = true;
	// }

	std::string raw;
	while (std::getline(file_, raw)) {
		const std::string line = trim_copy(raw);
		std::smatch match;

		if (std::regex_match(line, match, kAscTriggerRegex)) {
			const std::string datetime_str = match[1].str();
			try {
				start_time_ = relative_timestamp_ ? 0.0 : datetime_to_timestamp(datetime_str);
			} catch (const std::exception&) {
				// follow Python behaviour: ignore incompatible header datetime
				start_time_ = 0.0;
			}
			continue;
		}

		if (std::regex_match(line, kStartOfMeasurementRegex)) {
			continue;
		}

		if (!std::regex_search(line, kLogRecordRegex)) {
			continue;
		}

		LogRecord msg{};
		std::vector<std::string> parts;

        /*
        produces
        _timestamp = 0.123
        channel    = "CANFD"
        rest       = "1 Tx 123 ..."
        Therefore
        if channel == "CANFD":
            msg_kwargs["is_fd"] = True
        is not checking the CAN channel—it is checking the record type.
        */
		try {
			parts = split_whitespace_limit(line, 2);
			if (parts.size() < 3) {
				throw std::runtime_error("Line does not contain enough fields");
			}
			const double ts = std::stod(parts[0]);
			msg.timestamp = ts + start_time_;
			if (parts[1] == "CANFD") {
				msg.is_fd = true;
			} else if (is_digits(parts[1])) {
				std::strncpy(msg.channel, parts[1].c_str(), sizeof(msg.channel) - 1);
				msg.channel[sizeof(msg.channel) - 1] = '\0';
			} else {
				continue;
			}
		} catch (const std::exception&) {
			continue;
		}

		try {
			if (!msg.is_fd) {
				process_classic_can_frame(parts[2], msg);
			} else {
				process_fd_can_frame(parts[2], msg);
			}
		} catch (const std::exception&) {
			// follow Python behaviour: skip malformed lines silently
			continue;
		}

		out = msg;
		return true;
	}

	return false;
}

// std::vector<LogRecord> ASCReader::read_all() {
// 	std::vector<LogRecord> out;
// 	LogRecord record{};
// 	while (next(record)) {
// 		out.push_back(record);
// 	}
// 	return out;
// }
