// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

[[nodiscard]] bool valid_timescale(std::string_view timescale) {
  constexpr std::array units = {"s", "ms", "us", "ns", "ps", "fs"};
  constexpr std::array magnitudes = {"1", "10", "100"};
  for (const auto magnitude : magnitudes) {
    for (const auto unit : units) {
      if (timescale == std::string(magnitude) + unit) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::string make_identifier(std::uint32_t index) {
  // VCD identifiers are base-94 strings using the printable range ! through ~.
  std::string result;
  do {
    result.push_back(static_cast<char>('!' + (index % 94)));
    index /= 94;
  } while (index != 0);
  return result;
}

[[nodiscard]] std::string escaped_name(std::string_view value) {
  const auto simple = !value.empty() &&
                      std::all_of(value.begin(), value.end(), [](char c) {
                        const auto byte = static_cast<unsigned char>(c);
                        return std::isalnum(byte) != 0 || c == '_' || c == '$';
                      });
  if (simple) {
    return std::string(value);
  }
  return "\\" + std::string(value);
}

[[nodiscard]] char vcd_char(Logic4 value) noexcept {
  switch (value) {
  case Logic4::zero:
    return '0';
  case Logic4::one:
    return '1';
  case Logic4::x:
    return 'x';
  case Logic4::z:
    return 'z';
  }
  return 'x';
}

[[nodiscard]] char vcd_char(Logic9 value) noexcept {
  switch (value) {
  case Logic9::zero:
  case Logic9::l:
    return '0';
  case Logic9::one:
  case Logic9::h:
    return '1';
  case Logic9::z:
    return 'z';
  case Logic9::u:
  case Logic9::x:
  case Logic9::w:
  case Logic9::dont_care:
    return 'x';
  }
  return 'x';
}

struct Scope {
  std::string name;
  std::vector<std::uint32_t> declarations;
  std::vector<Scope> children;
};

Scope &find_or_add_scope(Scope &parent, std::string_view name) {
  const auto found = std::find_if(
      parent.children.begin(), parent.children.end(),
      [name](const Scope &scope) { return scope.name == name; });
  if (found != parent.children.end()) {
    return *found;
  }
  parent.children.push_back(Scope{std::string(name), {}, {}});
  return parent.children.back();
}

} // namespace

struct VcdWriter::Impl {
  struct Declaration {
    std::string hierarchical_name;
    std::string reference;
    std::string identifier;
    std::size_t width{};
    SystemVerilogScalarKind scalar_kind{SystemVerilogScalarKind::None};
    std::string last_value;
    bool has_value{};
  };

  Impl(std::ostream &stream, std::string_view scale, std::size_t capacity)
      : output(stream), timescale(scale),
        buffer_capacity(std::max<std::size_t>(capacity, 256)) {
    if (!valid_timescale(scale)) {
      throw std::invalid_argument(
          "VCD timescale must be 1, 10, or 100 followed by s/ms/us/ns/ps/fs");
    }
  }

  std::ostream &output;
  std::string timescale;
  std::size_t buffer_capacity;
  std::string buffer;
  std::vector<Declaration> declarations;
  bool started{};
  SimulationTick current_time{};

  void append(std::string_view value) {
    buffer.append(value);
    if (buffer.size() >= buffer_capacity) {
      flush();
    }
  }

  void append(char value) {
    buffer.push_back(value);
    if (buffer.size() >= buffer_capacity) {
      flush();
    }
  }

  void flush() {
    if (!buffer.empty()) {
      output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      buffer.clear();
      if (!output) {
        throw std::runtime_error("failed to write VCD output");
      }
    }
  }

  Declaration &get(VcdSignal signal) {
    if (signal.index >= declarations.size()) {
      throw std::out_of_range("invalid VCD signal handle");
    }
    return declarations[signal.index];
  }

  void emit_scope(const Scope &scope) {
    if (!scope.name.empty()) {
      append("$scope module ");
      append(escaped_name(scope.name));
      append(" $end\n");
    }

    for (const auto index : scope.declarations) {
      const auto &declaration = declarations[index];
      const bool real = declaration.scalar_kind
                  == SystemVerilogScalarKind::ShortReal
          || declaration.scalar_kind == SystemVerilogScalarKind::Real
          || declaration.scalar_kind == SystemVerilogScalarKind::Realtime;
      append(real ? "$var real " : "$var wire ");
      append(std::to_string(declaration.width));
      append(" ");
      append(declaration.identifier);
      append(" ");
      append(escaped_name(declaration.reference));
      append(" $end\n");
    }
    for (const auto &child : scope.children) {
      emit_scope(child);
    }
    if (!scope.name.empty()) {
      append("$upscope $end\n");
    }
  }

  void write_value(Declaration &declaration, std::string value) {
    if (!started) {
      throw std::logic_error("VCD writer has not begun");
    }
    if (declaration.scalar_kind == SystemVerilogScalarKind::ShortReal
        || declaration.scalar_kind == SystemVerilogScalarKind::Real
        || declaration.scalar_kind == SystemVerilogScalarKind::Realtime) {
      throw std::invalid_argument(
          "VCD real declarations require typed scalar changes");
    }
    if (value.size() != declaration.width) {
      throw std::invalid_argument("VCD value width does not match declaration");
    }
    if (declaration.has_value && declaration.last_value == value) {
      return;
    }
    declaration.last_value = std::move(value);
    declaration.has_value = true;

    if (declaration.width == 1) {
      append(declaration.last_value.front());
      append(declaration.identifier);
      append('\n');
      return;
    }
    append('b');
    append(declaration.last_value);
    append(' ');
    append(declaration.identifier);
    append('\n');
  }

  void write_real(Declaration& declaration, std::string value) {
    if (!started) {
      throw std::logic_error("VCD writer has not begun");
    }
    if (declaration.has_value && declaration.last_value == value) return;
    declaration.last_value = std::move(value);
    declaration.has_value = true;
    append('r');
    append(declaration.last_value);
    append(' ');
    append(declaration.identifier);
    append('\n');
  }
};

VcdWriter::VcdWriter(std::ostream &output, std::string_view timescale,
                     std::size_t buffer_capacity)
    : impl_(std::make_unique<Impl>(output, timescale, buffer_capacity)) {}

VcdWriter::~VcdWriter() {
  if (!impl_) {
    return;
  }
  try {
    impl_->flush();
  } catch (...) {
    // Destructors cannot reliably report stream errors. Explicit flush() does.
  }
}

VcdWriter::VcdWriter(VcdWriter &&) noexcept = default;
VcdWriter &VcdWriter::operator=(VcdWriter &&) noexcept = default;

VcdSignal VcdWriter::declare_signal(std::string_view hierarchical_name,
                                    std::size_t width) {
  if (impl_->started) {
    throw std::logic_error("cannot declare a VCD signal after begin");
  }
  if (hierarchical_name.empty()) {
    throw std::invalid_argument("VCD signal name cannot be empty");
  }
  if (width == 0) {
    throw std::invalid_argument("VCD signal width must be greater than zero");
  }
  const auto index = static_cast<std::uint32_t>(impl_->declarations.size());
  if (static_cast<std::size_t>(index) != impl_->declarations.size()) {
    throw std::length_error("too many VCD signals");
  }
  const auto separator = hierarchical_name.find_last_of('.');
  const auto reference =
      separator == std::string_view::npos
          ? hierarchical_name
          : hierarchical_name.substr(separator + 1);
  if (reference.empty()) {
    throw std::invalid_argument("VCD signal name has an empty component");
  }
  impl_->declarations.push_back(
      {std::string(hierarchical_name), std::string(reference),
       make_identifier(index), width, SystemVerilogScalarKind::None, {}, false});
  return VcdSignal{index};
}

VcdSignal VcdWriter::declare_systemverilog_scalar(
    const std::string_view hierarchical_name,
    const SystemVerilogScalarKind kind) {
  const bool real = kind == SystemVerilogScalarKind::ShortReal
      || kind == SystemVerilogScalarKind::Real
      || kind == SystemVerilogScalarKind::Realtime;
  if (!real && kind != SystemVerilogScalarKind::Time
      && kind != SystemVerilogScalarKind::Chandle) {
    throw std::invalid_argument("unsupported SystemVerilog VCD scalar kind");
  }
  const auto signal = declare_signal(
      hierarchical_name, real ? 1U : 64U);
  impl_->declarations[signal.index].scalar_kind = kind;
  return signal;
}

void VcdWriter::begin(SimulationTick initial_time) {
  if (impl_->started) {
    throw std::logic_error("VCD writer has already begun");
  }

  Scope root;
  for (std::uint32_t index = 0; index < impl_->declarations.size(); ++index) {
    const auto &declaration = impl_->declarations[index];
    auto *scope = &root;
    std::string_view remaining = declaration.hierarchical_name;
    while (true) {
      const auto separator = remaining.find('.');
      if (separator == std::string_view::npos) {
        break;
      }
      const auto component = remaining.substr(0, separator);
      if (component.empty()) {
        throw std::invalid_argument("VCD signal name has an empty scope");
      }
      scope = &find_or_add_scope(*scope, component);
      remaining.remove_prefix(separator + 1);
    }
    scope->declarations.push_back(index);
  }

  impl_->append("$version fsim $end\n");
  impl_->append("$timescale ");
  impl_->append(impl_->timescale);
  impl_->append(" $end\n");
  impl_->emit_scope(root);
  impl_->append("$enddefinitions $end\n");
  impl_->append("#");
  impl_->append(std::to_string(initial_time));
  impl_->append("\n");
  impl_->current_time = initial_time;
  impl_->started = true;
}

void VcdWriter::change(VcdSignal signal, Logic4 value) {
  auto &declaration = impl_->get(signal);
  impl_->write_value(declaration, std::string(1, vcd_char(value)));
}

void VcdWriter::change(VcdSignal signal, Logic9 value) {
  auto &declaration = impl_->get(signal);
  impl_->write_value(declaration, std::string(1, vcd_char(value)));
}

void VcdWriter::change(VcdSignal signal, const PackedBit2 &value) {
  auto &declaration = impl_->get(signal);
  impl_->write_value(declaration, value.to_msb_string());
}

void VcdWriter::change(VcdSignal signal, const PackedLogic4 &value) {
  auto &declaration = impl_->get(signal);
  std::string encoded(value.width(), 'x');
  if (value.is_logic9()) {
    for (std::size_t index = 0; index < value.width(); ++index) {
      encoded[value.width() - index - 1] =
          vcd_char(value.get_logic9(index));
    }
  } else {
    for (std::size_t index = 0; index < value.width(); ++index) {
      encoded[value.width() - index - 1] =
          vcd_char(value.get(index));
    }
  }
  impl_->write_value(declaration, std::move(encoded));
}

void VcdWriter::change(VcdSignal signal, const PackedLogic9 &value) {
  auto &declaration = impl_->get(signal);
  std::string encoded(value.width(), 'x');
  for (std::size_t index = 0; index < value.width(); ++index) {
    encoded[value.width() - index - 1] = vcd_char(value.get(index));
  }
  impl_->write_value(declaration, std::move(encoded));
}

void VcdWriter::change(
    const VcdSignal signal,
    const SystemVerilogScalarValue& value) {
  auto& declaration = impl_->get(signal);
  if (declaration.scalar_kind != value.kind) {
    throw std::invalid_argument("VCD scalar kind does not match declaration");
  }
  if (value.kind == SystemVerilogScalarKind::Time
      || value.kind == SystemVerilogScalarKind::Chandle) {
    const auto encoded = encode_systemverilog_scalar_payload(value);
    if (!encoded) {
      throw std::invalid_argument("invalid VCD exact scalar payload");
    }
    change(signal, encoded.value);
    return;
  }
  const auto formatted = format_systemverilog_scalar(value);
  if (!formatted) {
    throw std::invalid_argument("invalid VCD real payload");
  }
  impl_->write_real(declaration, formatted.text);
}

void VcdWriter::set_time(SimulationTick time) {
  if (!impl_->started) {
    throw std::logic_error("VCD writer has not begun");
  }
  if (time < impl_->current_time) {
    throw std::invalid_argument("VCD time cannot move backwards");
  }
  if (time == impl_->current_time) {
    return;
  }
  impl_->append("#");
  impl_->append(std::to_string(time));
  impl_->append("\n");
  impl_->current_time = time;
}

void VcdWriter::flush() { impl_->flush(); }
bool VcdWriter::begun() const noexcept { return impl_->started; }
SimulationTick VcdWriter::time() const noexcept { return impl_->current_time; }

} // namespace fsim::runtime
