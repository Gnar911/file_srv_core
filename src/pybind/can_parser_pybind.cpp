#include "can_parser_pybind.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <pybind11/stl.h>

#include "parsed_entry_layout.h"

namespace py = pybind11;

namespace {
std::vector<ParsedEntry> copy_and_free_entries(ParsedEntry* entries, uint32_t count) {
    std::vector<ParsedEntry> result;
    if (entries == nullptr || count == 0) {
        if (entries != nullptr) {
            can_parser_free_entries(entries);
        }
        return result;
    }

    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        result.push_back(entries[i]);
    }
    can_parser_free_entries(entries);
    return result;
}
}  // namespace

void bind_can_parser(py::module_& m) {
    m.def("run_worker_segmented", [](const std::string& file_path, const std::string& token_id, int32_t fmt) {
        return can_parser_run_worker_segmented(
            file_path.c_str(),
            token_id.c_str(),
            static_cast<FormatType>(fmt));
    }, py::arg("file_path"), py::arg("token_id"), py::arg("fmt"));

    m.def("parse_file", [](const std::string& path) {
        ParsedEntry* out_entries = nullptr;
        uint32_t out_count = 0;
        const int32_t rc = can_parser_parse_file(path.c_str(), &out_entries, &out_count);
        if (rc != 0) {
            throw std::runtime_error("can_parser_parse_file failed with rc=" + std::to_string(rc));
        }
        return copy_and_free_entries(out_entries, out_count);
    }, py::arg("path"));

    m.def("parse_file_with_fmt", [](const std::string& path, int32_t fmt) {
        ParsedEntry* out_entries = nullptr;
        uint32_t out_count = 0;
        const int32_t rc = can_parser_parse_file_with_fmt(path.c_str(), fmt, &out_entries, &out_count);
        if (rc != 0) {
            throw std::runtime_error("can_parser_parse_file_with_fmt failed with rc=" + std::to_string(rc));
        }
        return copy_and_free_entries(out_entries, out_count);
    }, py::arg("path"), py::arg("fmt"));

    m.def("parse_line", [](const std::string& line, uint32_t line_num) -> py::object {
        ParsedEntry out{};
        const int32_t ok = can_parser_parse_line(line.c_str(), line_num, &out);
        if (ok == 1) {
            return py::cast(out);
        }
        return py::none();
    }, py::arg("line"), py::arg("line_num") = 0);

    m.attr("PARSER_STATUS_RUNNING") = py::int_(static_cast<uint32_t>(PARSER_STATUS_RUNNING));
    m.attr("PARSER_STATUS_DONE") = py::int_(static_cast<uint32_t>(PARSER_STATUS_DONE));
    m.attr("PARSER_STATUS_ERROR") = py::int_(static_cast<uint32_t>(PARSER_STATUS_ERROR));

    m.attr("FMT_UNKNOWN") = py::int_(static_cast<int>(FMT_UNKNOWN));
    m.attr("FMT_CANOE") = py::int_(static_cast<int>(FMT_CANOE));
    m.attr("FMT_CANOE_FULL") = py::int_(static_cast<int>(FMT_CANOE_FULL));
    m.attr("FMT_CANOE_CMP") = py::int_(static_cast<int>(FMT_CANOE_CMP));
    m.attr("FMT_CANCMD") = py::int_(static_cast<int>(FMT_CANCMD));
    m.attr("FMT_FILTER") = py::int_(static_cast<int>(FMT_FILTER));
    m.attr("FMT_CANSUKE") = py::int_(static_cast<int>(FMT_CANSUKE));
    m.attr("FMT_CANCMD_T2") = py::int_(static_cast<int>(FMT_CANCMD_T2));
    m.attr("FMT_CANCMD_T3") = py::int_(static_cast<int>(FMT_CANCMD_T3));
}
