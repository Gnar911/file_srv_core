#include "can_parser_pybind.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <pybind11/stl.h>

#include "parsed_entry_layout.h"
#include "can_parser.h"

namespace py = pybind11;

void bind_can_parser(py::module_& m) {
    m.def("run_worker_segmented", [](const std::string& file_path, const std::string& token_id) {
        return run_worker_segmented(
            file_path.c_str(),
            token_id.c_str()
        );
    }, py::arg("file_path"), py::arg("token_id"));

    // Deprecated: parse_file and parse_file_with_fmt are intentionally
    // not exposed to pybind because they are deprecated; use parse_lines instead.

    m.def("parse_line", [](const std::string& line) -> py::object {
        const std::optional<LogRecord> parsed = parse_line(line);
        if (parsed.has_value()) {
            return py::cast(*parsed);
        }
        return py::none();
    }, py::arg("line"));

    m.def("parse_lines", [](const std::string& src) {
        return parse_lines(src);
    }, py::arg("src"));

    // py::enum_<FormatType>(m, "FormatType")
    //     .value("UNKNOWN", FMT_UNKNOWN)
    //     .value("CANOE", FMT_CANOE)
    //     .value("CANOE_FULL", FMT_CANOE_FULL)
    //     .value("CANOE_CMP", FMT_CANOE_CMP)
    //     .value("CANCMD", FMT_CANCMD)
    //     .value("FILTER", FMT_FILTER)
    //     .value("CANSUKE", FMT_CANSUKE)
    //     .value("CANCMD_T2", FMT_CANCMD_T2)
    //     .value("CANCMD_T3", FMT_CANCMD_T3)
    //     .value("ASC", FMT_ASC)
    //     .value("BLF", FMT_BLF);

    // m.attr("PARSER_STATUS_RUNNING") = py::int_(static_cast<uint32_t>(PARSER_STATUS_RUNNING));
    // m.attr("PARSER_STATUS_DONE") = py::int_(static_cast<uint32_t>(PARSER_STATUS_DONE));
    // m.attr("PARSER_STATUS_ERROR") = py::int_(static_cast<uint32_t>(PARSER_STATUS_ERROR));

    // m.attr("FMT_UNKNOWN") = py::int_(static_cast<int>(FMT_UNKNOWN));
    // m.attr("FMT_CANOE") = py::int_(static_cast<int>(FMT_CANOE));
    // m.attr("FMT_CANOE_FULL") = py::int_(static_cast<int>(FMT_CANOE_FULL));
    // m.attr("FMT_CANOE_CMP") = py::int_(static_cast<int>(FMT_CANOE_CMP));
    // m.attr("FMT_CANCMD") = py::int_(static_cast<int>(FMT_CANCMD));
    // m.attr("FMT_FILTER") = py::int_(static_cast<int>(FMT_FILTER));
    // m.attr("FMT_CANSUKE") = py::int_(static_cast<int>(FMT_CANSUKE));
    // m.attr("FMT_CANCMD_T2") = py::int_(static_cast<int>(FMT_CANCMD_T2));
    // m.attr("FMT_CANCMD_T3") = py::int_(static_cast<int>(FMT_CANCMD_T3));
    // m.attr("FMT_ASC") = py::int_(static_cast<int>(FMT_ASC));
    // m.attr("FMT_BLF") = py::int_(static_cast<int>(FMT_BLF));
}
