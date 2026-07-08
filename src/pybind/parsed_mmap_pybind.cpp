#include <string>
#include <cstring>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "can_parser_pybind.h"
#include "can_parser.h"
#include "can_decoder.h"
#include "can_log_decoder.h"
#include "metadata_storage_if.h"

namespace py = pybind11;

PYBIND11_MODULE(fs_core, m) {
    m.doc() = "pybind11 bindings for MetaDataStorageInterface";

     m.def("abi_version", []() {
          return fs_core_abi_version();
     });

    py::class_<LogRecord>(m, "LogRecord")
        .def(py::init<>())
        .def_readwrite("timestamp", &LogRecord::timestamp)
        .def_readwrite("can_id", &LogRecord::can_id)
        .def_readwrite("direction", &LogRecord::direction)
        .def_readwrite("data_len", &LogRecord::data_len)
        .def_property("data",
               [](const LogRecord& entry) {
                    std::vector<uint8_t> out(std::begin(entry.data), std::end(entry.data));
                    return out;
               },
               [](LogRecord& entry, const std::vector<uint8_t>& value) {
                    std::memset(entry.data, 0, sizeof(entry.data));
                    const size_t n = value.size() < sizeof(entry.data) ? value.size() : sizeof(entry.data);
                    for (size_t i = 0; i < n; ++i) {
                         entry.data[i] = value[i];
                    }
               }
          )
        .def_property("channel",
               [](const LogRecord& entry) {
                    const size_t n = strnlen(entry.channel, sizeof(entry.channel));
                    return std::string(entry.channel, n);
               },
               [](LogRecord& entry, const std::string& value) {
                    std::memset(entry.channel, 0, sizeof(entry.channel));
                    std::strncpy(entry.channel, value.c_str(), sizeof(entry.channel) - 1);
               }
          );

    py::class_<ParsedEntry, LogRecord>(m, "ParsedEntry")
        .def(py::init<>())
        .def_readwrite("line_number", &ParsedEntry::line_number)
        .def_readwrite("last_timestamp", &ParsedEntry::last_timestamp)
        .def_readwrite("changed", &ParsedEntry::changed);

     py::class_<EntryUpdate>(m, "EntryUpdate")
          .def(py::init<>())
          .def_readwrite("row_index", &EntryUpdate::row_index)
          .def_readwrite("record", &EntryUpdate::record);

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

     py::class_<CanDatabaseModel>(m, "CanDatabaseModel")
          .def(py::init<>())
          .def_readwrite("messages", &CanDatabaseModel::messages)
          .def_readwrite("signals", &CanDatabaseModel::signals)
          .def_readwrite("canid_to_msg", &CanDatabaseModel::canid_to_msg);

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
              .def("decode_entry", py::overload_cast<const LogRecord&, uint32_t>(&CanDecoder::decode_entry, py::const_),
                   py::arg("entry"), py::arg("max_signals") = 0);

     m.def("can_decoder_run", [](const std::string& parsed_mmap_token, CanDatabaseModel model) {
          return ::can_decoder_run(parsed_mmap_token.c_str(), std::move(model));
     }, py::arg("parsed_mmap_token"), py::arg("model"));

    py::class_<file_service::LogQuery>(m, "LogQuery")
        .def(py::init<>())
        .def_readwrite("can_ids", &file_service::LogQuery::can_ids)
        .def_readwrite("channels", &file_service::LogQuery::channels)
        .def_readwrite("directions", &file_service::LogQuery::directions)
        .def_readwrite("changed_only", &file_service::LogQuery::changed_only)
        .def_readwrite("has_time_range", &file_service::LogQuery::has_time_range)
        .def_readwrite("first_ts", &file_service::LogQuery::first_ts)
        .def_readwrite("last_ts", &file_service::LogQuery::last_ts);

     py::class_<file_service::MetaDataStorageInterface>(m, "MetaDataStorageInterface")
          .def(py::init<std::string>(), py::arg("mmap_prefix"))
        .def("open_storage", &file_service::MetaDataStorageInterface::open_storage)
        .def("open_mmap", [](file_service::MetaDataStorageInterface& self) {
             self.open_storage();
             return 0;
         })
        .def("write_entries", &file_service::MetaDataStorageInterface::write_entries,
             py::arg("parsed_entries"))
        .def("update_entries", &file_service::MetaDataStorageInterface::update_entries,
             py::arg("entry_updates"))
        .def("close_storage", &file_service::MetaDataStorageInterface::close_storage)
        .def("close_mmap", [](file_service::MetaDataStorageInterface& self) {
             self.close_storage();
         })
        .def("read_page", &file_service::MetaDataStorageInterface::read_page,
             py::arg("first"), py::arg("last"))
        .def("read_page_multi", &file_service::MetaDataStorageInterface::read_page_multi,
             py::arg("query"), py::arg("first"), py::arg("last"))
        .def("read_all_entries", &file_service::MetaDataStorageInterface::read_all_entries)
        .def("get_first_last_timestamp", [](const file_service::MetaDataStorageInterface& self) -> py::tuple {
             double first_ts = 0.0;
             double last_ts = 0.0;
             self.get_first_last_timestamp(first_ts, last_ts);
             return py::make_tuple(py::float_(first_ts), py::float_(last_ts));
         })
        .def("get_file_path", &file_service::MetaDataStorageInterface::get_file_path)
        .def("fetch_count", &file_service::MetaDataStorageInterface::fetch_count)
     .def("last_error_code", &file_service::MetaDataStorageInterface::last_error_code);
}
