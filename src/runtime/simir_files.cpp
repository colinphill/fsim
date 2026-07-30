// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <system_error>

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] std::filesystem::path utf8_path(
    const std::string_view value) {
  std::u8string encoded;
  encoded.reserve(value.size());
  for (const auto byte : value) {
    encoded.push_back(static_cast<char8_t>(
        static_cast<unsigned char>(byte)));
  }
  return std::filesystem::path{encoded};
}

[[nodiscard]] bool below_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
  auto root_part = root.begin();
  auto candidate_part = candidate.begin();
  for (; root_part != root.end(); ++root_part, ++candidate_part) {
    if (candidate_part == candidate.end()
        || *root_part != *candidate_part) {
      return false;
    }
  }
  return true;
}

struct Mode {
  std::ios::openmode flags{};
  bool readable{};
  bool writable{};
};

[[nodiscard]] std::optional<Mode> file_mode(
    const std::string_view spelling) {
  using std::ios;
  if (spelling == "r") {
    return Mode{ios::in, true, false};
  }
  if (spelling == "w") {
    return Mode{ios::out | ios::trunc, false, true};
  }
  if (spelling == "a") {
    return Mode{ios::out | ios::app, false, true};
  }
  if (spelling == "r+") {
    return Mode{ios::in | ios::out, true, true};
  }
  if (spelling == "w+") {
    return Mode{ios::in | ios::out | ios::trunc, true, true};
  }
  if (spelling == "a+") {
    return Mode{ios::in | ios::out | ios::app, true, true};
  }
  return std::nullopt;
}

}  // namespace

void Interpreter::Impl::set_file_root(
    std::filesystem::path root) {
  if (started) {
    throw std::logic_error{
        "cannot set the SimIR file root after start"};
  }
  if (root.empty()) {
    file_root.clear();
    return;
  }
  std::error_code error;
  root = std::filesystem::weakly_canonical(root, error);
  if (error || !std::filesystem::is_directory(root, error)) {
    throw std::invalid_argument{
        "SimIR file root is not an accessible directory"};
  }
  file_root = std::move(root);
}

FileHandle Interpreter::Impl::open_file(
    const ProcessId process,
    const std::string_view path_text,
    const std::string_view mode_text) {
  if (file_root.empty()) {
    throw std::runtime_error{
        "SystemVerilog file access has no configured project root"};
  }
  const auto mode = file_mode(mode_text);
  if (!mode) {
    throw std::runtime_error{
        "unsupported SystemVerilog text file mode '"
        + std::string{mode_text} + "'"};
  }
  std::filesystem::path relative;
  try {
    relative = utf8_path(path_text).lexically_normal();
  } catch (const std::exception&) {
    throw std::runtime_error{
        "SystemVerilog filename is not a valid UTF-8 path"};
  }
  if (relative.empty() || relative.is_absolute()
      || relative.has_root_name()) {
    throw std::runtime_error{
        "SystemVerilog filename must be manifest-root-relative"};
  }
  for (const auto& component : relative) {
    if (component == "..") {
      throw std::runtime_error{
          "SystemVerilog filename escapes the manifest root"};
    }
  }
  const auto joined = file_root / relative;
  std::error_code error;
  auto checked = std::filesystem::weakly_canonical(
      mode->readable && !mode->writable
          ? joined
          : joined.parent_path(),
      error);
  if (error) {
    throw std::runtime_error{
        "cannot resolve SystemVerilog filename '"
        + std::string{path_text} + "': " + error.message()};
  }
  if (mode->writable) {
    checked /= joined.filename();
  }
  if (!below_root(file_root, checked)) {
    throw std::runtime_error{
        "SystemVerilog filename resolves outside the manifest root"};
  }

  auto stream =
      std::make_unique<std::fstream>(checked, mode->flags);
  if (!stream->is_open()) {
    throw std::runtime_error{
        "cannot open SystemVerilog text file '"
        + std::string{path_text} + "' in mode '"
        + std::string{mode_text} + "'"};
  }
  if (next_file_handle == 0) {
    throw std::length_error{
        "SystemVerilog file handle space is exhausted"};
  }
  const auto handle = next_file_handle++;
  files.emplace(
      handle,
      FileState{
          process,
          std::move(checked),
          std::string{mode_text},
          std::move(stream),
          {},
          false,
          mode->readable,
          mode->writable});
  return handle;
}

Interpreter::Impl::FileState& Interpreter::Impl::checked_file(
    const ProcessId process,
    const FileHandle handle) {
  if (handle == 0) {
    throw std::runtime_error{
        "invalid zero SystemVerilog file handle"};
  }
  const auto found = files.find(handle);
  if (found == files.end()) {
    throw std::runtime_error{
        "unknown SystemVerilog file handle "
        + std::to_string(handle)};
  }
  if (found->second.owner != process) {
    throw std::runtime_error{
        "SystemVerilog file handle is owned by another process"};
  }
  if (found->second.closed || !found->second.stream) {
    throw std::runtime_error{
        "SystemVerilog file handle is already closed"};
  }
  return found->second;
}

void Interpreter::Impl::close_file(
    const ProcessId process,
    const FileHandle handle) {
  auto& file = checked_file(process, handle);
  // A successful read through EOF leaves eofbit/failbit set. Clear those
  // input-status bits before close so they cannot masquerade as a close
  // failure.
  file.stream->clear();
  if (file.writable) {
    file.stream->flush();
  }
  file.stream->close();
  const bool failed = file.stream->fail();
  file.stream.reset();
  file.closed = true;
  if (failed) {
    file.last_error = "failed to close text file";
    throw std::runtime_error{file.last_error};
  }
}

void Interpreter::Impl::write_file(
    const ProcessId process,
    const FileHandle handle,
    const std::string_view text,
    const bool newline) {
  auto& file = checked_file(process, handle);
  if (!file.writable) {
    throw std::runtime_error{
        "SystemVerilog file handle is not open for writing"};
  }
  file.stream->write(
      text.data(), static_cast<std::streamsize>(text.size()));
  if (newline) {
    file.stream->put('\n');
  }
  file.stream->flush();
  if (!file.stream->good()) {
    file.last_error = "failed to write SystemVerilog text file";
    throw std::runtime_error{file.last_error};
  }
}

std::string Interpreter::Impl::read_file_line(
    const ProcessId process,
    const FileHandle handle,
    std::uint32_t& count) {
  auto& file = checked_file(process, handle);
  if (!file.readable) {
    throw std::runtime_error{
        "SystemVerilog file handle is not open for reading"};
  }
  std::string line;
  if (!std::getline(*file.stream, line)) {
    count = 0;
    if (file.stream->eof()) {
      return {};
    }
    file.last_error = "failed to read SystemVerilog text file";
    return {};
  }
  if (line.size() == maximum_string_bytes
      && !file.stream->eof()) {
    file.last_error =
        "SystemVerilog input line exceeds the 4096-byte limit";
    count = 0;
    return {};
  }
  if (line.size() > maximum_string_bytes) {
    file.last_error =
        "SystemVerilog input line exceeds the 4096-byte limit";
    count = 0;
    return {};
  }
  count = static_cast<std::uint32_t>(line.size());
  if (!file.stream->eof()) {
    line.push_back('\n');
    ++count;
  }
  return line;
}

bool Interpreter::Impl::file_end_of_file(
    const ProcessId process,
    const FileHandle handle) {
  return checked_file(process, handle).stream->eof();
}

std::string Interpreter::Impl::file_error(
    const ProcessId process,
    const FileHandle handle,
    bool& has_error) {
  auto& file = checked_file(process, handle);
  has_error = !file.last_error.empty();
  return file.last_error;
}

FileHandle Interpreter::Impl::known_file_handle(
    ProcessState& process,
    const RegisterId handle_register) {
  const auto word =
      get_register(process, handle_register).low_word();
  if (word.width == 0 || word.width > 32
      || word.bval != 0
      || word.aval
          > std::numeric_limits<FileHandle>::max()) {
    throw InterpreterError{
        process.program.id,
        process.pc,
        "file handle must be a known 32-bit integral value"};
  }
  return static_cast<FileHandle>(word.aval);
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileOpen& operation) {
  try {
    const auto handle = open_file(
        process.program.id,
        get_string_register(process, operation.path),
        get_string_register(process, operation.mode));
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(32, handle, 0);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileClose& operation) {
  try {
    close_file(
        process.program.id,
        known_file_handle(process, operation.handle));
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileWriteLiteral& operation) {
  try {
    write_file(
        process.program.id,
        known_file_handle(process, operation.handle),
        operation.text,
        operation.newline);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileWriteFormatted& operation) {
  try {
    write_file(
        process.program.id,
        known_file_handle(process, operation.handle),
        make_formatted_output(
            operation.prefix,
            operation.suffix,
            operation.format,
            get_register(process, operation.source),
            operation.signed_decimal,
            operation.suppress_leading_zero,
            operation.minimum_width,
            operation.left_justify,
            operation.zero_pad),
        operation.newline);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileWriteString& operation) {
  try {
    write_file(
        process.program.id,
        known_file_handle(process, operation.handle),
        operation.prefix
            + get_string_register(process, operation.source)
            + operation.suffix,
        operation.newline);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileReadLine& operation) {
  try {
    std::uint32_t count{};
    auto line = read_file_line(
        process.program.id,
        known_file_handle(process, operation.handle),
        count);
    get_string_register(process, operation.target) =
        std::move(line);
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(32, count, 0);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileEndOfFile& operation) {
  try {
    const bool eof = file_end_of_file(
        process.program.id,
        known_file_handle(process, operation.handle));
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(
            32, eof ? 1U : 0U, 0);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileErrorStatus& operation) {
  try {
    bool has_error{};
    auto message = file_error(
        process.program.id,
        known_file_handle(process, operation.handle),
        has_error);
    get_string_register(process, operation.target) =
        std::move(message);
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(
            32, has_error ? 1U : 0U, 0);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

}  // namespace fsim::runtime::simir
