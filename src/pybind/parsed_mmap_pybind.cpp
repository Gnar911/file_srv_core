#include <string>
#include <cstring>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "can_parser_pybind.h"
#include "can_parser.h"
#include "can_decoder.h"
#include "can_log_decoder.h"
#include "parsed_mmap_if.h"

namespace py = pybind11;

PYBIND11_MODULE(fs_core, m) {
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
        .def_readwrite("changed", &ParsedEntry::changed)
        .def_property("data",
               [](const ParsedEntry& entry) {
                    std::vector<uint8_t> out(std::begin(entry.data), std::end(entry.data));
                    return out;
               },
               [](ParsedEntry& entry, const std::vector<uint8_t>& value) {
                    std::memset(entry.data, 0, sizeof(entry.data));
                    const size_t n = value.size() < sizeof(entry.data) ? value.size() : sizeof(entry.data);
                    for (size_t i = 0; i < n; ++i) {
                         entry.data[i] = value[i];
                    }
               }
          )
        .def_property("channel",
               [](const ParsedEntry& entry) {
                    const size_t n = strnlen(entry.channel, sizeof(entry.channel));
                    return std::string(entry.channel, n);
               },
               [](ParsedEntry& entry, const std::string& value) {
                    std::memset(entry.channel, 0, sizeof(entry.channel));
                    std::strncpy(entry.channel, value.c_str(), sizeof(entry.channel) - 1);
               }
          );

     bind_can_parser(m);

     py::class_<MessageDef>(m, "MessageDef")
          .def(py::init<>())
          .def_readwrite("can_id", &MessageDef::can_id)
          .def_readwrite("signal_count", &MessageDef::signal_count)
          .def_readwrite("msg_length", &MessageDef::msg_length)
          .def_readwrite("signal_offset", &MessageDef::signal_offset)
          .def_readwrite("padding", &MessageDef::padding);

     py::class_<SignalDef>(m, "SignalDef")
          .def(py::init<>())
          .def_readwrite("start_bit", &SignalDef::start_bit)
          .def_readwrite("bit_length", &SignalDef::bit_length)
          .def_readwrite("byte_order", &SignalDef::byte_order)
          .def_readwrite("is_signed", &SignalDef::is_signed)
          .def_readwrite("has_choices", &SignalDef::has_choices)
          .def_readwrite("padding1", &SignalDef::padding1)
          .def_readwrite("scale", &SignalDef::scale)
          .def_readwrite("offset", &SignalDef::offset);

     py::class_<DecodedSignal>(m, "DecodedSignal")
          .def(py::init<>())
          .def_readwrite("signal_name", &DecodedSignal::signal_name)
          .def_readwrite("raw_value", &DecodedSignal::raw_value)
          .def_readwrite("phys_value", &DecodedSignal::phys_value);

     py::class_<DecodeError>(m, "DecodeError")
          .def(py::init<>())
          .def_readwrite("rc", &DecodeError::rc)
          .def_property("error_message",
               [](const DecodeError& error) {
                    return std::string(error.error_message);
               },
               [](DecodeError& error, const std::string& value) {
                    std::memset(error.error_message, 0, sizeof(error.error_message));
                    const size_t n = value.size() < sizeof(error.error_message) - 1 ? value.size() : sizeof(error.error_message) - 1;
                    std::memcpy(error.error_message, value.data(), n);
               }
          );

     py::class_<CanDecoder>(m, "CanDecoder")
          .def(py::init<>())
          .def("load_db", [](CanDecoder& self,
                                 const std::vector<MessageDef>& messages,
                                 const std::vector<SignalDef>& signals) {
               CanDatabaseModel model;
               model.messages = messages;
               model.signals = signals;
               model.canid_to_msg.reserve(messages.size());
               for (uint32_t i = 0; i < static_cast<uint32_t>(messages.size()); ++i) {
                    model.canid_to_msg[messages[i].can_id] = i;
               }
               return self.load_db(model);
          },
          py::arg("messages"),
          py::arg("signals"))
          .def("free_db", &CanDecoder::free_db)
          .def("is_loaded", &CanDecoder::is_loaded)
          .def("decode_entry", py::overload_cast<const ParsedEntry&, uint32_t>(&CanDecoder::decode_entry, py::const_),
                py::arg("entry"), py::arg("max_signals") = 0);

     m.def("can_decoder_run", [](const std::string& parsed_mmap_token, const CanDecoder& decoder) {
          return ::can_decoder_run(parsed_mmap_token.c_str(), decoder);
     }, py::arg("parsed_mmap_token"), py::arg("decoder"));

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
