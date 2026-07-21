#include <string>
#include <cstring>
#include <algorithm>
#include <utility>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "can_parser_pybind.h"
#include "can_parser.h"
#include "can_decoder.h"
#include "can_log_decoder.h"
#include "metadata_storage_if.h"
#include "view_browser.h"

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

          .def(py::pickle(
               [](const LogRecord &p) {
                    // serialize: (timestamp, can_id, direction, data_bytes, channel, data_len)
                    std::string channel_str(p.channel, strnlen(p.channel, sizeof(p.channel)));
                    py::bytes data_bytes(reinterpret_cast<const char*>(p.data), static_cast<ssize_t>(p.data_len));
                    return py::make_tuple(py::float_(p.timestamp), py::int_(p.can_id), py::int_(p.direction), data_bytes, py::str(channel_str), py::int_(p.data_len));
               },
               [](py::object t) {
                    py::tuple tup = t.cast<py::tuple>();
                    LogRecord p;
                    p.timestamp = tup[0].cast<double>();
                    p.can_id = tup[1].cast<int>();
                    p.direction = tup[2].cast<int>();
                    std::string data = tup[3].cast<std::string>();
                    std::string channel = tup[4].cast<std::string>();
                    p.data_len = tup[5].cast<int>();
                    std::memset(p.data, 0, sizeof(p.data));
                    const size_t copy_n = std::min<size_t>(static_cast<size_t>(p.data_len), data.size());
                    if (copy_n > 0) std::memcpy(p.data, data.data(), copy_n);
                    std::memset(p.channel, 0, sizeof(p.channel));
                    std::strncpy(p.channel, channel.c_str(), sizeof(p.channel) - 1);
                    return p;
               }
          ))

     //    .def_property("data",
     //           [](const LogRecord& entry) {
     //                std::vector<uint8_t> out(std::begin(entry.data), std::end(entry.data));
     //                return out;
     //           },
     //           [](LogRecord& entry, const std::vector<uint8_t>& value) {
     //                std::memset(entry.data, 0, sizeof(entry.data));
     //                const size_t n = value.size() < sizeof(entry.data) ? value.size() : sizeof(entry.data);
     //                for (size_t i = 0; i < n; ++i) {
     //                     entry.data[i] = value[i];
     //                }
     //           }
     //      )
          .def_property(
               "data",
               // getter: return full 64-byte buffer as bytes
               [](const LogRecord& r) {
                    return py::bytes(reinterpret_cast<const char*>(r.data), 64);
               },
               // setter: accept bytes and copy into fixed C array, zero-fill remainder
               [](LogRecord& r, py::bytes value) {
                    std::string data = value;
                    if (data.size() > 64) {
                         throw std::runtime_error("CAN data exceeds 64 bytes");
                    }
                    std::fill(std::begin(r.data), std::end(r.data), 0);
                    std::copy(data.begin(), data.end(), r.data);
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

     // ParsedEntry extends LogRecord and is picklable
     py::class_<ParsedEntry, LogRecord>(m, "ParsedEntry")
          .def(py::init<>())
          .def_readwrite("line_number", &ParsedEntry::line_number)
          .def_readwrite("last_timestamp", &ParsedEntry::last_timestamp)
          .def_readwrite("changed", &ParsedEntry::changed)
          .def(py::pickle(
               [](const ParsedEntry &p) {
                    std::string channel_str(p.channel, strnlen(p.channel, sizeof(p.channel)));
                    py::bytes data_bytes(reinterpret_cast<const char*>(p.data), static_cast<ssize_t>(p.data_len));
                    return py::make_tuple(
                         py::float_(p.timestamp), py::int_(p.can_id), py::int_(p.direction), data_bytes, py::str(channel_str), py::int_(p.data_len),
                         py::int_(p.line_number), py::float_(p.last_timestamp), py::int_(p.changed)
                    );
               },
               [](py::object t) {
                    py::tuple tup = t.cast<py::tuple>();
                    ParsedEntry p;
                    p.timestamp = tup[0].cast<double>();
                    p.can_id = tup[1].cast<int>();
                    p.direction = tup[2].cast<int>();
                    std::string data = tup[3].cast<std::string>();
                    std::string channel = tup[4].cast<std::string>();
                    p.data_len = tup[5].cast<int>();
                    p.line_number = tup[6].cast<int>();
                    p.last_timestamp = tup[7].cast<double>();
                    p.changed = tup[8].cast<int>();
                    std::memset(p.data, 0, sizeof(p.data));
                    const size_t copy_n = std::min<size_t>(static_cast<size_t>(p.data_len), data.size());
                    if (copy_n > 0) std::memcpy(p.data, data.data(), copy_n);
                    std::memset(p.channel, 0, sizeof(p.channel));
                    std::strncpy(p.channel, channel.c_str(), sizeof(p.channel) - 1);
                    return p;
               }
          ));

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

    py::class_<LogQuery>(m, "LogQuery")
        .def(py::init<>())
        .def_readwrite("can_ids", &LogQuery::can_ids)
        .def_readwrite("channels", &LogQuery::channels)
        .def_readwrite("directions", &LogQuery::directions)
        .def_readwrite("changed_only", &LogQuery::changed_only)
        .def_readwrite("has_time_range", &LogQuery::has_time_range)
        .def_readwrite("first_ts", &LogQuery::first_ts)
        .def_readwrite("last_ts", &LogQuery::last_ts);

     py::class_<MetaDataStorageInterface>(m, "MetaDataStorageInterface")
          .def(py::init<std::string>(), py::arg("mmap_prefix"))
        .def("write_entries", &MetaDataStorageInterface::write_entries,
             py::arg("parsed_entries"))
        .def("update_entry", &MetaDataStorageInterface::update_entry,
             py::arg("row_index"), py::arg("entry"))
        .def("read_page", &MetaDataStorageInterface::read_page,
             py::arg("first"), py::arg("last"))
             // New lazy browsing API: execute the SQLite filter and return a
             // lightweight ViewBrowser that references the mmap payload.
             .def("browse", &MetaDataStorageInterface::browse, py::arg("query"))
             .def("browse_all", &MetaDataStorageInterface::browse_all)
          .def("get_first_last_timestamp", [](const MetaDataStorageInterface& self) -> py::object {
                double first_ts = 0.0;
                double last_ts = 0.0;
                const bool ok = self.get_first_last_timestamp(first_ts, last_ts);
                if (!ok) return py::none();
                return py::make_tuple(py::float_(first_ts), py::float_(last_ts));
           })
            .def("token_path", &MetaDataStorageInterface::token_path)
        .def("fetch_count", &MetaDataStorageInterface::fetch_count)
       ;

     using DataMmapReader = mmap::DataMmapInterface<mmap::Access::ReadOnly>;
     using DataMmapWriter = mmap::DataMmapInterface<mmap::Access::ReadWrite>;

     py::class_<DataMmapReader>(m, "DataMmapReader")
          .def(py::init<const std::string&>())
          .def("read_page", &DataMmapReader::read_page);

     py::class_<DataMmapWriter>(m, "DataMmapWriter")
          .def(py::init<const std::string&>())
          .def("append_entry", &DataMmapWriter::append_entry);

         // Expose ViewBrowser: a lightweight, read-only logical view over the
         // mmap-backed parsed entries. The returned ParsedEntry references the
         // underlying mmap; lifetime is tied to the DataMmap backing storage.
         py::class_<ViewBrowser>(m, "ViewBrowser")
                .def(py::init<const DataMmapReader&>())
                .def("set_full_view", &ViewBrowser::set_full_view, py::arg("row_count"))
                .def("set_rows", &ViewBrowser::set_rows, py::arg("rows"))
             .def("size", &ViewBrowser::size)
             .def("at", [](const ViewBrowser& vb, uint64_t idx) -> const ParsedEntry& {
                  return vb.at(idx);
             }, py::arg("logical_index"), py::return_value_policy::reference_internal)
             .def("__len__", &ViewBrowser::size)
             .def("__getitem__", [](const ViewBrowser& vb, uint64_t idx) -> const ParsedEntry& {
                  return vb.at(idx);
             }, py::return_value_policy::reference_internal)
             ;
}
