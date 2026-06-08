#include <cstdint>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "can_parser.h"
#include "parsed_mmap_if.h"

namespace py = pybind11;

PYBIND11_MODULE(parsed_mmap, m) {
    m.doc() = "pybind11 bindings for ParsedMmapInterface";

     m.def("abi_version", []() {
          return fs_core_abi_version();
     });

    py::class_<ParsedEntry>(m, "ParsedEntry")
        .def(py::init<>())
        .def_readwrite("line_number", &ParsedEntry::line_number)
        .def_readwrite("timestamp", &ParsedEntry::timestamp)
        .def_readwrite("last_timestamp", &ParsedEntry::last_timestamp)
        .def_readwrite("can_id", &ParsedEntry::can_id)
        .def_readwrite("direction", &ParsedEntry::direction)
        .def_readwrite("data_len", &ParsedEntry::data_len)
        .def_readwrite("changed", &ParsedEntry::changed);

    py::class_<file_service::ParsedMmapInterface>(m, "ParsedMmapInterface")
        .def(py::init<std::string>(), py::arg("token_id"))
        .def("open_mmap", &file_service::ParsedMmapInterface::open_mmap)
        .def("write_entries", &file_service::ParsedMmapInterface::write_entries,
             py::arg("parsed_entries"))
        .def("close_mmap", &file_service::ParsedMmapInterface::close_mmap)
        .def("read_page", &file_service::ParsedMmapInterface::read_page,
             py::arg("first"), py::arg("last"))
        .def("read_page_from_can_id", &file_service::ParsedMmapInterface::read_page_from_can_id,
             py::arg("can_id"), py::arg("first"), py::arg("last"))
        .def("read_page_from_can_ids", &file_service::ParsedMmapInterface::read_page_from_can_ids,
             py::arg("can_ids"), py::arg("first"), py::arg("last"))
        .def("read_page_from_can_id_changed", &file_service::ParsedMmapInterface::read_page_from_can_id_changed,
             py::arg("can_id"), py::arg("first"), py::arg("last"))
        .def("read_page_from_can_ids_changed", &file_service::ParsedMmapInterface::read_page_from_can_ids_changed,
             py::arg("can_ids"), py::arg("first"), py::arg("last"))
        .def("read_page_from_channel", &file_service::ParsedMmapInterface::read_page_from_channel,
             py::arg("channel"), py::arg("first"), py::arg("last"))
        .def("read_page_from_channels", &file_service::ParsedMmapInterface::read_page_from_channels,
             py::arg("channels"), py::arg("first"), py::arg("last"))
        .def("read_page_from_direction", &file_service::ParsedMmapInterface::read_page_from_direction,
             py::arg("direction"), py::arg("first"), py::arg("last"))
        .def("read_page_from_directions", &file_service::ParsedMmapInterface::read_page_from_directions,
             py::arg("directions"), py::arg("first"), py::arg("last"))
       .def("get_total_entries_num", &file_service::ParsedMmapInterface::get_total_entries_num)
     .def("last_error_code", &file_service::ParsedMmapInterface::last_error_code);
}
