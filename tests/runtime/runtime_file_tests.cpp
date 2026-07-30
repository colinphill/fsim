// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

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
      FileClose{0},
      LoadStringConstant{2, "r"},
      FileOpen{2, 0, 2},
      FileReadLine{3, 2, 4},
      WriteStringObject{first, 4},
      FileReadLine{3, 2, 4},
      WriteStringObject{second, 4},
      FileReadLine{3, 2, 4},
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
      FileReadLine{1, 0, 2},
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
       LoadStringConstant{1, "rb"},
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
