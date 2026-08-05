// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/file_binary.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/runtime/simir.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error{std::string{message}};
  }
}

class TestDirectory {
public:
  explicit TestDirectory(const std::string_view name) {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path()
        / ("fsim-" + std::string{name} + "-"
           + std::to_string(suffix));
    std::filesystem::create_directories(path);
  }

  ~TestDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path path;
};

[[nodiscard]] fsim::runtime::PackedLogic4 number(
    const std::uint32_t value) {
  return fsim::runtime::PackedLogic4::from_aval_bval(
      32, value, 0);
}

}  // namespace

void test_simir_text_files() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  TestDirectory files{"simir-files"};
  Interpreter interpreter;
  interpreter.set_file_root(files.path);
  const auto first = interpreter.add_string_object({"first", {}});
  const auto second = interpreter.add_string_object({"second", {}});
  const auto third = interpreter.add_string_object({"third", {}});
  const auto error_text =
      interpreter.add_string_object({"error", "not-cleared"});

  Process process;
  process.id = 0;
  process.name = "text_file_round_trip";
  process.register_count = 6;
  process.string_register_count = 6;
  process.debug_locals = {
      {"count", "integer", 3, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"eof", "integer", 4, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"error", "integer", 5, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}}};
  process.operations = {
      LoadStringConstant{0, "round-trip.txt"},
      LoadStringConstant{1, "w"},
      FileOpen{0, 0, 1},
      FileWriteLiteral{0, "head", true},
      LoadConstant{1, number(42)},
      FileWriteFormatted{
          0, 1, 32, OutputFormat::decimal,
          "value=", "", true},
      LoadStringConstant{3, "body"},
      FileWriteString{0, 3, "<", ">", false},
      FileFlush{0, false},
      FileFlush{0, true},
      FileClose{0},
      LoadStringConstant{2, "r"},
      FileOpen{2, 0, 2},
      FileReadLine{3, 2, 4, 0, FileReadKind::line},
      WriteStringObject{first, 4},
      FileReadLine{3, 2, 4, 0, FileReadKind::line},
      WriteStringObject{second, 4},
      FileReadLine{3, 2, 4, 0, FileReadKind::line},
      WriteStringObject{third, 4},
      FileEndOfFile{4, 2},
      FileErrorStatus{5, 2, 5},
      WriteStringObject{error_text, 5},
      FileClose{2},
      Halt{}};
  (void)interpreter.add_process(std::move(process));
  require(
      interpreter.run().status == RunStatus::completed,
      "text-file round trip completes");
  require(
      interpreter.string_object_value(first) == "head\n",
      "first line retains its newline");
  require(
      interpreter.string_object_value(second) == "value=42\n",
      "formatted line round trips");
  require(
      interpreter.string_object_value(third) == "<body>",
      "final unterminated line round trips");
  require(
      interpreter.string_object_value(error_text).empty(),
      "successful file has an empty error string");
  require(
      interpreter.read_debug_local(0, 0) == number(6),
      "$fgets count includes the final line bytes");
  require(
      interpreter.read_debug_local(0, 1) == number(1),
      "EOF is visible after the final unterminated line");
  require(
      interpreter.read_debug_local(0, 2) == number(0),
      "successful file reports no error");

  std::ifstream written(files.path / "round-trip.txt");
  const std::string contents{
      std::istreambuf_iterator<char>{written},
      std::istreambuf_iterator<char>{}};
  require(
      contents == "head\nvalue=42\n<body>",
      "file writes flush in deterministic operation order");

  Interpreter scanner;
  const auto scanned_text =
      scanner.add_string_object({"scanned", {}});
  Process scan_process;
  scan_process.id = 0;
  scan_process.name = "formatted_string_scan";
  scan_process.register_count = 3;
  scan_process.string_register_count = 2;
  scan_process.debug_locals = {
      {"count", "integer", 0, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"hex", "logic [15:0]", 1, 16, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"decimal", "integer", 2, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}}};
  scan_process.operations = {
      LoadStringConstant{0, "  tag=2a name=alpha % 17"},
      FileScan{
          0, 0, 0, true,
          {
              {" tag=", InputScanFormat::hexadecimal, 0, false,
               {InputScanTargetKind::packed_register, 1, 16, false}},
              {" name=", InputScanFormat::string, 0, false,
               {InputScanTargetKind::string_register, 1, 1, false}},
              {" % ", InputScanFormat::decimal, 0, false,
               {InputScanTargetKind::packed_register, 2, 32, true}},
          },
          {}},
      WriteStringObject{scanned_text, 1},
      Halt{}};
  (void)scanner.add_process(std::move(scan_process));
  require(
      scanner.run().status == RunStatus::completed,
      "formatted string scan completes");
  require(
      scanner.read_debug_local(0, 0) == number(3),
      "formatted scan reports three assignments");
  require(
      scanner.read_debug_local(0, 1)
          == PackedLogic4::from_aval_bval(16, 0x2a, 0),
      "formatted hexadecimal scan preserves target width");
  require(
      scanner.read_debug_local(0, 2) == number(17),
      "formatted decimal scan stores its value");
  require(
      scanner.string_object_value(scanned_text) == "alpha",
      "formatted string scan stores bounded text");

  FileScan scalar_scan;
  scalar_scan.string_source = true;
  scalar_scan.conversions = {
      {"real=", InputScanFormat::real, 0, false,
       {InputScanTargetKind::packed_register, 0, 64, true,
        SystemVerilogScalarKind::Real}},
      {" time=", InputScanFormat::decimal, 0, false,
       {InputScanTargetKind::packed_register, 1, 64, true,
        SystemVerilogScalarKind::Time}},
      {" handle=", InputScanFormat::hexadecimal, 0, false,
       {InputScanTargetKind::packed_register, 2, 64, true,
        SystemVerilogScalarKind::Chandle}},
  };
  const auto scalar_values = scan_formatted_string(
      scalar_scan, "real=1.2_5 time=1_7 handle=0");
  require(
      scalar_values.assignments == 3
          && scalar_values.values[0] && scalar_values.values[1]
          && scalar_values.values[2],
      "scalar scan stages every successful conversion");
  const auto decoded_real = decode_systemverilog_scalar_payload(
      scalar_values.values[0]->packed, SystemVerilogScalarKind::Real);
  const auto decoded_time = decode_systemverilog_scalar_payload(
      scalar_values.values[1]->packed, SystemVerilogScalarKind::Time);
  require(
      decoded_real && decoded_real.value.as_real() == 1.25
          && decoded_time && decoded_time.value.as_time() == 17
          && scalar_values.values[2]->packed.low_word().aval == 0,
      "scalar scan preserves real, time, and null chandle payloads");

  const auto overflowing = scan_formatted_string(
      FileScan{0, 0, 0, true,
          {{"", InputScanFormat::real, 0, false,
            {InputScanTargetKind::packed_register, 0, 64, true,
             SystemVerilogScalarKind::Real}}}},
      "1e9999");
  require(
      overflowing.assignments == 0 && !overflowing.values[0],
      "overflowing real scan leaves its target transaction uncommitted");
  const auto partial_scalar = scan_formatted_string(
      FileScan{0, 0, 0, true,
          {{"real=", InputScanFormat::real, 0, false,
            {InputScanTargetKind::packed_register, 0, 64, true,
             SystemVerilogScalarKind::Real}},
           {" time=", InputScanFormat::decimal, 0, false,
            {InputScanTargetKind::packed_register, 1, 64, true,
             SystemVerilogScalarKind::Time}}}},
      "real=2.5 time=oops");
  require(
      partial_scalar.assignments == 1 && partial_scalar.values[0]
          && !partial_scalar.values[1],
      "failed later conversion preserves only prior staged copy-outs");
  const auto nonnull_chandle = scan_formatted_string(
      FileScan{0, 0, 0, true,
          {{"", InputScanFormat::hexadecimal, 0, false,
            {InputScanTargetKind::packed_register, 0, 64, true,
             SystemVerilogScalarKind::Chandle}}}},
      "1");
  require(
      nonnull_chandle.assignments == 0 && !nonnull_chandle.values[0],
      "text input cannot fabricate a non-null chandle identity");
  std::size_t chandle_byte{};
  bool rejected_binary_chandle{};
  try {
    static_cast<void>(read_binary_file(
        FileBinaryRead{0, 0, 0, FileBinaryTargetKind::packed_register,
            64, true, 0, 0, false, false,
            SystemVerilogScalarKind::Chandle},
        std::nullopt, std::nullopt, std::nullopt,
        [&]() -> std::int32_t {
          return chandle_byte++ == 7U ? 1 : 0;
        }));
  } catch (const std::invalid_argument& error) {
    rejected_binary_chandle = std::string_view{error.what()}.find(
        "null handle") != std::string_view::npos;
  }
  require(
      rejected_binary_chandle,
      "binary input cannot fabricate a non-null chandle identity");

  {
    std::ofstream binary(
        files.path / "binary.bin",
        std::ios::binary | std::ios::trunc);
    const std::string bytes{"\x12\x34\x56\x78\x9a", 5};
    binary.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(binary.good(), "binary input fixture is writable");
  }
  ContainerType memory_type;
  memory_type.element_width = 8;
  memory_type.fixed = true;
  memory_type.index_left = 3;
  memory_type.index_right = 0;
  memory_type.dimensions = {{3, 0}};
  Interpreter binary_reader;
  binary_reader.set_file_root(files.path);
  const auto binary_memory = binary_reader.add_container_object(
      {"binary-memory", default_container_value(memory_type), std::nullopt});
  Process binary_process;
  binary_process.id = 0;
  binary_process.name = "binary_file_read";
  binary_process.register_count = 18;
  binary_process.string_register_count = 2;
  binary_process.container_register_count = 1;
  binary_process.container_register_types = {memory_type};
  binary_process.debug_locals = {
      {"packed-bytes", "integer", 1, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"packed", "logic [15:0]", 2, 16, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"memory-bytes", "integer", 3, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"partial", "logic [15:0]", 6, 16, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"partial-bytes", "integer", 7, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"tell", "integer", 8, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"invalid-seek", "integer", 11, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"seek", "integer", 17, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"positioned", "byte", 12, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"positioned-bytes", "integer", 13, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"rewind", "integer", 14, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"rewound", "byte", 15, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"rewound-bytes", "integer", 16, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}}};
  binary_process.operations = {
      LoadStringConstant{0, "binary.bin"},
      LoadStringConstant{1, "rb"},
      FileOpen{0, 0, 1},
      FileBinaryRead{
          1, 0, 2, FileBinaryTargetKind::packed_register,
          16, false, 0, 0, false, false},
      LoadConstant{4, number(2)},
      LoadConstant{5, number(2)},
      FileBinaryRead{
          3, 0, 0, FileBinaryTargetKind::container_register,
          8, false, 4, 5, true, true},
      WriteContainerObject{binary_memory, 0},
      FilePosition{8, 0, 0, 0, FilePositionKind::tell},
      FileBinaryRead{
          7, 0, 6, FileBinaryTargetKind::packed_register,
          16, false, 0, 0, false, false},
      LoadConstant{
          9, number(static_cast<std::uint32_t>(-3))},
      LoadConstant{10, number(3)},
      FilePosition{11, 0, 9, 10, FilePositionKind::seek},
      LoadConstant{10, number(1)},
      FilePosition{17, 0, 9, 10, FilePositionKind::seek},
      FileBinaryRead{
          13, 0, 12, FileBinaryTargetKind::packed_register,
          8, false, 0, 0, false, false},
      FilePosition{14, 0, 0, 0, FilePositionKind::rewind},
      FileBinaryRead{
          16, 0, 15, FileBinaryTargetKind::packed_register,
          8, false, 0, 0, false, false},
      FileFlush{0, false},
      FileFlush{0, true},
      FileClose{0},
      Halt{}};
  (void)binary_reader.add_process(std::move(binary_process));
  require(
      binary_reader.run().status == RunStatus::completed,
      "binary file read completes");
  require(
      binary_reader.read_debug_local(0, 0) == number(2)
          && binary_reader.read_debug_local(0, 1)
              == PackedLogic4::from_aval_bval(16, 0x1234, 0),
      "packed $fread reports bytes and stores big-endian data");
  const auto& loaded_memory =
      binary_reader.container_object_value(binary_memory);
  require(
      binary_reader.read_debug_local(0, 2) == number(2)
          && loaded_memory.elements[0]
              == PackedLogic4{8, Logic4::x}
          && loaded_memory.elements[1]
              == PackedLogic4::from_aval_bval(8, 0x56, 0)
          && loaded_memory.elements[2]
              == PackedLogic4::from_aval_bval(8, 0x78, 0)
          && loaded_memory.elements[3]
              == PackedLogic4{8, Logic4::x},
      "memory $fread honors declared direction, start, and count");
  require(
      binary_reader.read_debug_local(0, 3)
              == PackedLogic4::from_aval_bval(16, 0x9a00, 0)
          && binary_reader.read_debug_local(0, 4) == number(1),
      "partial packed $fread fills from the most-significant byte");
  require(
      binary_reader.read_debug_local(0, 5) == number(4)
          && binary_reader.read_debug_local(0, 6)
              == number(static_cast<std::uint32_t>(-1))
          && binary_reader.read_debug_local(0, 7) == number(0)
          && binary_reader.read_debug_local(0, 8)
              == PackedLogic4::from_aval_bval(8, 0x56, 0)
          && binary_reader.read_debug_local(0, 9) == number(1),
      "$ftell and $fseek report invalid origins and preserve byte positions");
  require(
      binary_reader.read_debug_local(0, 10) == number(0)
          && binary_reader.read_debug_local(0, 11)
              == PackedLogic4::from_aval_bval(8, 0x12, 0)
          && binary_reader.read_debug_local(0, 12) == number(1),
      "$rewind restores the beginning before the next binary read");

  {
    std::ofstream oversized(
        files.path / "oversized.txt",
        std::ios::binary | std::ios::trunc);
    oversized << std::string(maximum_string_bytes + 1U, 'x')
              << '\n';
    require(oversized.good(), "oversized input fixture is writable");
  }
  Interpreter bounded;
  bounded.set_file_root(files.path);
  const auto bounded_error =
      bounded.add_string_object({"bounded-error", {}});
  Process bounded_reader;
  bounded_reader.id = 0;
  bounded_reader.name = "bounded_file_reader";
  bounded_reader.register_count = 3;
  bounded_reader.string_register_count = 4;
  bounded_reader.operations = {
      LoadStringConstant{0, "oversized.txt"},
      LoadStringConstant{1, "r"},
      FileOpen{0, 0, 1},
      FileReadLine{1, 0, 2, 0, FileReadKind::line},
      FileErrorStatus{2, 0, 3},
      WriteStringObject{bounded_error, 3},
      FileClose{0},
      Halt{}};
  (void)bounded.add_process(std::move(bounded_reader));
  require(
      bounded.run().status == RunStatus::completed,
      "overlong input is reported through $ferror");
  require(
      bounded.string_object_value(bounded_error).find(
          "4096-byte limit")
      != std::string::npos,
      "overlong input retains a bounded error message");

  const auto expect_failure =
      [&](std::vector<Operation> operations,
          const std::string_view expected) {
        Interpreter failing;
        failing.set_file_root(files.path);
        Process candidate;
        candidate.id = 0;
        candidate.name = "text_file_failure";
        candidate.register_count = 2;
        candidate.string_register_count = 2;
        candidate.operations = std::move(operations);
        (void)failing.add_process(std::move(candidate));
        try {
          (void)failing.run();
          require(false, "invalid file process must fail");
        } catch (const InterpreterError& error) {
          require(
              std::string_view{error.what()}.find(expected)
                  != std::string_view::npos,
              "file failure retains the expected diagnostic");
        }
      };

  expect_failure(
      {LoadStringConstant{0, "../escape.txt"},
       LoadStringConstant{1, "w"},
       FileOpen{0, 0, 1},
       Halt{}},
      "escapes the manifest root");
  expect_failure(
      {LoadStringConstant{0, files.path.string()},
       LoadStringConstant{1, "r"},
       FileOpen{0, 0, 1},
       Halt{}},
      "manifest-root-relative");
  expect_failure(
      {LoadStringConstant{0, "missing.txt"},
       LoadStringConstant{1, "r"},
       FileOpen{0, 0, 1},
       Halt{}},
      "cannot open SystemVerilog text file");
  expect_failure(
      {LoadStringConstant{0, "invalid.txt"},
       LoadStringConstant{1, "rt"},
       FileOpen{0, 0, 1},
       Halt{}},
      "unsupported SystemVerilog text file mode");
  expect_failure(
      {LoadStringConstant{0, "closed.txt"},
       LoadStringConstant{1, "w"},
       FileOpen{0, 0, 1},
       FileClose{0},
       FileClose{0},
       Halt{}},
      "already closed");
  expect_failure(
      {LoadConstant{0, number(0)},
       FileClose{0},
       Halt{}},
      "invalid zero");
  expect_failure(
      {LoadConstant{0, number(99)},
       FileClose{0},
       Halt{}},
      "unknown SystemVerilog file handle");

  {
    Interpreter lifecycle;
    lifecycle.set_file_root(files.path);
    Process unclosed;
    unclosed.id = 0;
    unclosed.name = "unclosed_file";
    unclosed.register_count = 1;
    unclosed.string_register_count = 2;
    unclosed.operations = {
        LoadStringConstant{0, "lifecycle.txt"},
        LoadStringConstant{1, "w"},
        FileOpen{0, 0, 1},
        FileWriteLiteral{0, "closed-on-destruction", true},
        Halt{}};
    (void)lifecycle.add_process(std::move(unclosed));
    require(
        lifecycle.run().status == RunStatus::completed,
        "unclosed file process completes");
  }
  require(
      std::filesystem::file_size(files.path / "lifecycle.txt") == 22,
      "session destruction closes and preserves the final file");

  Interpreter ownership;
  ownership.set_file_root(files.path);
  Process owner;
  owner.id = 0;
  owner.name = "file_owner";
  owner.register_count = 1;
  owner.string_register_count = 2;
  owner.operations = {
      LoadStringConstant{0, "owned.txt"},
      LoadStringConstant{1, "w"},
      FileOpen{0, 0, 1},
      Halt{}};
  Process borrower;
  borrower.id = 1;
  borrower.name = "file_borrower";
  borrower.register_count = 1;
  borrower.operations = {
      LoadConstant{0, number(1)},
      FileClose{0},
      Halt{}};
  (void)ownership.add_process(std::move(owner));
  (void)ownership.add_process(std::move(borrower));
  try {
    (void)ownership.run();
    require(false, "cross-process file-handle use must fail");
  } catch (const InterpreterError& error) {
    require(
        std::string_view{error.what()}.find("owned by another process")
            != std::string_view::npos,
        "file handles retain process ownership");
  }
}

}  // namespace fsim::tests::runtime
