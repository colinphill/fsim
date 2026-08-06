// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "fsim/runtime/file_binary.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <system_error>

namespace fsim::runtime::simir {

namespace {

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
  if (spelling == "r" || spelling == "rb") {
    return Mode{ios::in | ios::binary, true, false};
  }
  if (spelling == "w" || spelling == "wb") {
    return Mode{
        ios::out | ios::trunc | ios::binary, false, true};
  }
  if (spelling == "a" || spelling == "ab") {
    return Mode{
        ios::out | ios::app | ios::binary, false, true};
  }
  if (spelling == "r+" || spelling == "r+b"
      || spelling == "rb+") {
    return Mode{
        ios::in | ios::out | ios::binary, true, true};
  }
  if (spelling == "w+" || spelling == "w+b"
      || spelling == "wb+") {
    return Mode{
        ios::in | ios::out | ios::trunc | ios::binary,
        true,
        true};
  }
  if (spelling == "a+" || spelling == "a+b"
      || spelling == "ab+") {
    return Mode{
        ios::in | ios::out | ios::app | ios::binary,
        true,
        true};
  }
  return std::nullopt;
}

[[nodiscard]] std::uint32_t vhdl_open_status(
    const std::string_view message) {
  return message.find("unsupported") != std::string_view::npos
          && message.find("mode") != std::string_view::npos
      ? 3U : 2U;
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
    relative = fsim::support::path_from_utf8(path_text).lexically_normal();
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
          std::nullopt,
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
  line.reserve(128);
  while (line.size() < maximum_string_bytes) {
    const auto character = read_file_character(process, handle);
    if (character < 0) {
      if (file.stream->eof()) {
        count = static_cast<std::uint32_t>(line.size());
        return line;
      }
      count = 0;
      return {};
    }
    line.push_back(static_cast<char>(character));
    if (character == '\n') {
      count = static_cast<std::uint32_t>(line.size());
      return line;
    }
  }
  const auto extra = read_file_character(process, handle);
  if (extra < 0) {
    if (file.stream->eof()) {
      count = static_cast<std::uint32_t>(line.size());
      return line;
    }
    count = 0;
    return {};
  }
  auto character = extra;
  while (character >= 0 && character != '\n') {
    character = read_file_character(process, handle);
  }
  file.last_error =
      "SystemVerilog input line exceeds the 4096-byte limit";
  count = 0;
  return {};
}

std::int32_t Interpreter::Impl::read_file_character(
    const ProcessId process,
    const FileHandle handle) {
  auto& file = checked_file(process, handle);
  if (!file.readable) {
    throw std::runtime_error{
        "SystemVerilog file handle is not open for reading"};
  }
  if (file.pushback) {
    const auto character = *file.pushback;
    file.pushback.reset();
    return character;
  }
  const auto character = file.stream->get();
  if (character != std::char_traits<char>::eof()) {
    return static_cast<unsigned char>(character);
  }
  if (!file.stream->eof()) {
    file.last_error = "failed to read SystemVerilog text file character";
  }
  return -1;
}

std::int32_t Interpreter::Impl::unread_file_character(
    const ProcessId process,
    const FileHandle handle,
    const std::int32_t character) {
  auto& file = checked_file(process, handle);
  if (!file.readable) {
    throw std::runtime_error{
        "SystemVerilog file handle is not open for reading"};
  }
  if (file.pushback || character < 0 || character > 255) {
    return -1;
  }
  file.stream->clear();
  file.pushback = static_cast<std::uint8_t>(character);
  return character;
}

bool Interpreter::Impl::file_end_of_file(
    const ProcessId process,
    const FileHandle handle) {
  const auto& file = checked_file(process, handle);
  return !file.pushback && file.stream->eof();
}

std::string Interpreter::Impl::file_error(
    const ProcessId process,
    const FileHandle handle,
    bool& has_error) {
  auto& file = checked_file(process, handle);
  has_error = !file.last_error.empty();
  return file.last_error;
}

std::int32_t Interpreter::Impl::position_file(
    const ProcessId process,
    const FileHandle handle,
    const FilePositionKind kind,
    std::int32_t offset,
    std::int32_t origin) {
  auto& file = checked_file(process, handle);
  const auto fail = [&](const std::string_view message) {
    file.last_error = message;
    return std::int32_t{-1};
  };
  if (kind == FilePositionKind::tell) {
    const auto position = file.readable
        ? file.stream->tellg() : file.stream->tellp();
    if (position == std::streampos{-1})
      return fail("failed to query SystemVerilog file position");
    auto value = static_cast<std::streamoff>(position);
    if (file.pushback) --value;
    if (value < 0 || value > std::numeric_limits<std::int32_t>::max())
      return fail("SystemVerilog file position exceeds 32-bit range");
    return static_cast<std::int32_t>(value);
  }
  if (kind == FilePositionKind::rewind) {
    offset = 0;
    origin = 0;
  }
  std::ios::seekdir direction;
  if (origin == 0) direction = std::ios::beg;
  else if (origin == 1) direction = std::ios::cur;
  else if (origin == 2) direction = std::ios::end;
  else return fail("$fseek origin must be 0, 1, or 2");
  file.stream->clear();
  if (file.writable) file.stream->flush();
  file.pushback.reset();
  if (file.readable) file.stream->seekg(offset, direction);
  else file.stream->seekp(offset, direction);
  if (!file.stream->good())
    return fail("failed to seek SystemVerilog file");
  return 0;
}

void Interpreter::Impl::flush_file(
    const ProcessId process,
    const std::optional<FileHandle> handle) {
  const auto flush = [](FileState& file) {
    if (!file.writable) return;
    file.stream->flush();
    if (!file.stream->good()) {
      file.last_error = "failed to flush SystemVerilog file";
      throw std::runtime_error{file.last_error};
    }
  };
  if (handle) {
    flush(checked_file(process, *handle));
    return;
  }
  for (auto& [id, file] : files) {
    static_cast<void>(id);
    if (file.owner == process && !file.closed && file.stream) flush(file);
  }
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
    if (operation.vhdl
        && known_file_handle(process, operation.destination) != 0) {
      if (!operation.status) {
        throw std::runtime_error{
            "VHDL file object is already open"};
      }
      get_register(process, *operation.status) =
          PackedLogic4::from_aval_bval(2, 1, 0);
      ++process.pc;
      return;
    }
    const auto handle = open_file(
        process.program.id,
        get_string_register(process, operation.path),
        get_string_register(process, operation.mode));
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(32, handle, 0);
    if (operation.status) {
      get_register(process, *operation.status) =
          PackedLogic4::from_aval_bval(2, 0, 0);
    }
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    if (operation.status) {
      get_register(process, operation.destination) =
          PackedLogic4::from_aval_bval(32, 0, 0);
      get_register(process, *operation.status) =
          PackedLogic4::from_aval_bval(
              2, vhdl_open_status(error.what()), 0);
      ++process.pc;
      return;
    }
    throw InterpreterError{
        process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileClose& operation) {
  try {
    const auto handle = known_file_handle(process, operation.handle);
    if (handle != 0) {
      close_file(process.program.id, handle);
    } else if (!operation.ignore_zero) {
      if (operation.clear_handle) {
        throw std::runtime_error{"VHDL file object is not open"};
      }
      close_file(process.program.id, handle);
    }
    if (operation.clear_handle) {
      get_register(process, operation.handle) =
          PackedLogic4::from_aval_bval(32, 0, 0);
    }
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
            operation.zero_pad,
            operation.scalar_kind),
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
    if (operation.clear_source) {
      get_string_register(process, operation.source).clear();
    }
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
    std::int32_t result{};
    const auto handle = known_file_handle(process, operation.handle);
    if (operation.kind == FileReadKind::line) {
      std::uint32_t count{};
      auto line = read_file_line(process.program.id, handle, count);
      if (operation.vhdl_textio) {
        if (count == 0) {
          throw std::runtime_error{"VHDL readline reached end of file"};
        }
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty() && line.back() == '\r') line.pop_back();
      }
      if (count != 0) {
        get_string_register(process, operation.target) = std::move(line);
      }
      result = static_cast<std::int32_t>(count);
    } else if (operation.kind == FileReadKind::character) {
      result = read_file_character(process.program.id, handle);
    } else {
      const auto character = get_register(process, operation.source).low_word();
      result = character.bval == 0
          ? unread_file_character(
                process.program.id, handle,
                static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(character.aval)))
          : -1;
    }
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(result), 0);
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
    const auto handle = known_file_handle(process, operation.handle);
    bool eof{};
    if (operation.lookahead) {
      const auto character = read_file_character(
          process.program.id, handle);
      eof = character < 0;
      if (!eof && unread_file_character(
              process.program.id, handle, character) != character) {
        throw std::runtime_error{
            "VHDL endfile could not preserve lookahead"};
      }
    } else {
      eof = file_end_of_file(process.program.id, handle);
    }
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(
            operation.lookahead ? 1U : 32U,
            eof ? 1U : 0U, 0);
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

void Interpreter::Impl::execute_file(
    ProcessState& process, const FileScan& operation) {
  try {
    InputScanResult scanned;
    if (operation.string_source) {
      auto& source = get_string_register(process, operation.source);
      scanned = scan_formatted_string(
          operation, source);
      if (operation.consume_string_source) {
        source.erase(0, std::min(source.size(), scanned.consumed));
      }
    } else {
      const auto handle = known_file_handle(process, operation.handle);
      const std::function<std::int32_t()> read = [&] {
        return read_file_character(process.program.id, handle);
      };
      const std::function<void(std::int32_t)> unread = [&](const auto value) {
        if (unread_file_character(process.program.id, handle, value) != value) {
          throw std::runtime_error{"failed to preserve file scan lookahead"};
        }
      };
      scanned = scan_formatted_input(operation, read, unread);
    }
    const auto expected = static_cast<std::int32_t>(std::ranges::count_if(
        operation.conversions,
        [](const InputScanConversion& conversion) {
          return !conversion.suppress;
        }));
    const bool complete = scanned.assignments == expected;
    if (operation.success) {
      get_register(process, *operation.success) =
          PackedLogic4::from_aval_bval(1, complete ? 1U : 0U, 0);
    }
    if (operation.require_assignments && !complete) {
      throw std::runtime_error{
          "VHDL file read did not convert the requested element"};
    }
    for (std::size_t index = 0; index < scanned.values.size(); ++index) {
      if (!scanned.values[index]) continue;
      const auto& target = operation.conversions[index].target;
      auto& value = *scanned.values[index];
      switch (target.kind) {
      case InputScanTargetKind::packed_register:
        get_register(process, target.id) = std::move(value.packed);
        break;
      case InputScanTargetKind::packed_signal:
        commit_driver(process.program.id, target.id, std::move(value.packed));
        break;
      case InputScanTargetKind::string_register:
        get_string_register(process, target.id) = std::move(value.text);
        break;
      case InputScanTargetKind::string_object:
        get_string_object(target.id).initial_value = std::move(value.text);
        break;
      }
    }
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(scanned.assignments), 0);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process, const FileBinaryRead& operation) {
  try {
    const auto known_integer = [&](const RegisterId id) {
      const auto word = get_register(process, id).low_word();
      if (word.width != 32 || word.bval != 0) {
        throw std::runtime_error{"$fread bounds must be known 32-bit integers"};
      }
      return static_cast<std::int32_t>(static_cast<std::uint32_t>(word.aval));
    };
    std::optional<ContainerValue> container;
    if (operation.target_kind == FileBinaryTargetKind::container_register) {
      container = get_container_register(process, operation.target);
    } else if (operation.target_kind
               == FileBinaryTargetKind::container_object) {
      container = read_container_object_value(operation.target);
    }
    const auto handle = known_file_handle(process, operation.handle);
    const std::function<std::int32_t()> read = [&] {
      return read_file_character(process.program.id, handle);
    };
    auto result = read_binary_file(
        operation, std::move(container),
        operation.has_start
            ? std::optional{known_integer(operation.start)} : std::nullopt,
        operation.has_count
            ? std::optional{known_integer(operation.count)} : std::nullopt,
        read);
    switch (operation.target_kind) {
    case FileBinaryTargetKind::packed_register:
      get_register(process, operation.target) = std::move(result.packed);
      break;
    case FileBinaryTargetKind::packed_signal:
      commit_driver(
          process.program.id, operation.target, std::move(result.packed));
      break;
    case FileBinaryTargetKind::container_register:
      get_container_register(process, operation.target) =
          std::move(*result.container);
      break;
    case FileBinaryTargetKind::container_object:
      write_container_object_value(operation.target, *result.container);
      break;
    }
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(32, result.bytes, 0);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process, const FilePosition& operation) {
  try {
    const auto known_integer = [&](const RegisterId id) {
      const auto word = get_register(process, id).low_word();
      if (word.width != 32 || word.bval != 0)
        throw std::runtime_error{
            "file position operands must be known 32-bit integers"};
      return static_cast<std::int32_t>(
          static_cast<std::uint32_t>(word.aval));
    };
    const auto result = position_file(
        process.program.id,
        known_file_handle(process, operation.handle),
        operation.kind,
        operation.kind == FilePositionKind::seek
            ? known_integer(operation.offset) : 0,
        operation.kind == FilePositionKind::seek
            ? known_integer(operation.origin) : 0);
    get_register(process, operation.destination) =
        PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(result), 0);
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{process.program.id, process.pc, error.what()};
  }
}

void Interpreter::Impl::execute_file(
    ProcessState& process, const FileFlush& operation) {
  try {
    flush_file(
        process.program.id,
        operation.all
            ? std::nullopt
            : std::optional{known_file_handle(process, operation.handle)});
    ++process.pc;
  } catch (const InterpreterError&) {
    throw;
  } catch (const std::exception& error) {
    throw InterpreterError{process.program.id, process.pc, error.what()};
  }
}

}  // namespace fsim::runtime::simir
