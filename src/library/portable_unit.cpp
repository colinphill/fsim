// SPDX-License-Identifier: Apache-2.0
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"

#include <boost/pfr/core.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::library {
namespace {

constexpr std::string_view kMagic = "FSIMUNIT";
constexpr std::string_view kUdpMagic = "FSIMUDPD";
constexpr std::string_view kCode = "FSIM-LIB-0006";
constexpr std::size_t kMaximumArchiveNesting = 512;

template <typename T>
struct IsVector : std::false_type {};
template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type {};

template <typename T>
struct IsOptional : std::false_type {};
template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <typename T>
struct IsSharedPtr : std::false_type {};
template <typename T>
struct IsSharedPtr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::Expression>
auto archive_fields(T& value) {
  return std::tie(
      value.kind, value.text, value.operands, value.span,
      value.aggregate_choices, value.aggregate_choice_expressions,
      value.nominal_type, value.decoded_string, value.call_argument_names);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::Type>
auto archive_fields(T& value) {
  return std::tie(
      value.domain, value.spelling, value.packed_range, value.is_signed,
      value.packed_range_expression, value.named_type, value.named_type_span,
      value.nominal_type, value.vhdl_type_declaration,
      value.vhdl_resolution_function, value.enumeration_literals,
      value.enumeration_range, value.enumeration_range_expression,
      value.enumeration_base_range, value.enumeration_base_range_expression,
      value.packed_members, value.packed_aggregate, value.integer_range,
      value.integer_range_expression, value.integer_base_range,
      value.integer_base_range_expression, value.discrete_range_expression,
      value.vhdl_array, value.vhdl_access, value.vhdl_file,
      value.vhdl_physical, value.vhdl_protected,
      value.vhdl_array_constraints, value.systemverilog_container);
}

template <typename T>
  requires std::same_as<
      std::remove_cv_t<T>, frontend::SignalDeclaration>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.direction, value.is_port, value.span,
      value.net_delay, value.interface_type, value.modport,
      value.default_value);
}

template <typename T>
  requires std::same_as<
      std::remove_cv_t<T>, frontend::VariableDeclaration>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.initializer, value.span,
      value.vhdl_shared, value.vhdl_file, value.vhdl_file_open_kind);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::FunctionArgument>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.direction, value.span, value.reference,
      value.default_value, value.vhdl_file);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::ParameterOverride>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.value, value.span, value.type_value,
      value.default_box);
}

template <typename T>
  requires std::same_as<
      std::remove_cv_t<T>, frontend::ParameterDeclaration>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.default_value, value.local, value.span,
      value.kind, value.default_type, value.function_profile,
      value.procedure_profile, value.package_profile, value.object_class,
      value.direction);
}

template <typename T>
  requires std::same_as<std::remove_cv_t<T>, frontend::TaskArgument>
auto archive_fields(T& value) {
  return std::tie(
      value.name, value.type, value.direction, value.span, value.reference,
      value.default_value);
}

class Writer final {
 public:
  void raw(const std::string_view bytes) { bytes_.append(bytes); }

  void unsigned64(const std::uint64_t value) {
    for (unsigned shift = 0; shift != 64; shift += 8) {
      bytes_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
  }

  template <typename T>
  void write(T& value) {
    if (!failure_.empty()) {
      return;
    }
    if (++depth_ > kMaximumArchiveNesting) {
      --depth_;
      failure_ = "portable unit exceeds the safe structural nesting depth";
      return;
    }
    using Value = std::remove_cv_t<T>;
    if constexpr (std::same_as<Value, bool>) {
      bytes_.push_back(value ? '\1' : '\0');
    } else if constexpr (std::is_enum_v<Value>) {
      auto underlying = static_cast<std::underlying_type_t<Value>>(value);
      write(underlying);
    } else if constexpr (std::is_integral_v<Value>) {
      if constexpr (std::is_signed_v<Value>) {
        unsigned64(std::bit_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)));
      } else {
        unsigned64(static_cast<std::uint64_t>(value));
      }
    } else if constexpr (std::same_as<Value, std::string>) {
      unsigned64(value.size());
      raw(value);
    } else if constexpr (IsVector<Value>::value) {
      unsigned64(value.size());
      for (auto& item : value) {
        write(item);
      }
    } else if constexpr (IsOptional<Value>::value) {
      bytes_.push_back(value.has_value() ? '\1' : '\0');
      if (value.has_value()) {
        write(*value);
      }
    } else if constexpr (IsSharedPtr<Value>::value) {
      bytes_.push_back(value ? '\1' : '\0');
      if (value) {
        if (!active_shared_.insert(value.get()).second) {
          failure_ = "portable unit contains a cyclic owning type graph";
        } else {
          write(*value);
          active_shared_.erase(value.get());
        }
      }
    } else if constexpr (requires { archive_fields(value); }) {
      std::apply([&](auto&... fields) { (write(fields), ...); },
                 archive_fields(value));
    } else if constexpr (std::is_aggregate_v<Value>) {
      boost::pfr::for_each_field(value, [&](auto& field) { write(field); });
    } else {
      static_assert(sizeof(Value) == 0, "unsupported portable-unit field");
    }
    --depth_;
  }

  std::string finish() && { return std::move(bytes_); }
  [[nodiscard]] const std::string& failure() const noexcept { return failure_; }

 private:
  std::string bytes_;
  std::size_t depth_{};
  std::unordered_set<const void*> active_shared_;
  std::string failure_;
};

class Reader final {
 public:
  explicit Reader(const std::string_view bytes) : bytes_(bytes) {}

  bool raw(const std::string_view expected) {
    if (remaining() < expected.size()
        || bytes_.substr(position_, expected.size()) != expected) {
      return fail("portable unit has an invalid magic header");
    }
    position_ += expected.size();
    return true;
  }

  bool unsigned64(std::uint64_t& value) {
    if (remaining() < 8) {
      return fail("portable unit is truncated");
    }
    value = 0;
    for (unsigned shift = 0; shift != 64; shift += 8) {
      value |= static_cast<std::uint64_t>(
          static_cast<unsigned char>(bytes_[position_++])) << shift;
    }
    return true;
  }

  template <typename T>
  bool read(T& value) {
    using Value = std::remove_cv_t<T>;
    if (!enter()) {
      return false;
    }
    const bool result = [&]() {
      if constexpr (std::same_as<Value, bool>) {
        if (remaining() == 0 || static_cast<unsigned char>(bytes_[position_]) > 1) {
          return fail("portable unit contains an invalid boolean");
        }
        value = bytes_[position_++] != 0;
        return true;
      } else if constexpr (std::is_enum_v<Value>) {
        std::underlying_type_t<Value> underlying{};
        if (!read(underlying)) {
          return false;
        }
        value = static_cast<Value>(underlying);
        return true;
      } else if constexpr (std::is_integral_v<Value>) {
        std::uint64_t encoded{};
        if (!unsigned64(encoded)) {
          return false;
        }
        if constexpr (std::is_signed_v<Value>) {
          const auto decoded = std::bit_cast<std::int64_t>(encoded);
          if (decoded < std::numeric_limits<Value>::min()
              || decoded > std::numeric_limits<Value>::max()) {
            return fail("portable integer is outside the host type range");
          }
          value = static_cast<Value>(decoded);
        } else {
          if (encoded > std::numeric_limits<Value>::max()) {
            return fail("portable integer is outside the host type range");
          }
          value = static_cast<Value>(encoded);
        }
        return true;
      } else if constexpr (std::same_as<Value, std::string>) {
        std::uint64_t size{};
        if (!unsigned64(size) || size > remaining()) {
          return fail("portable string length exceeds the artifact");
        }
        value.assign(bytes_.substr(position_, static_cast<std::size_t>(size)));
        position_ += static_cast<std::size_t>(size);
        return true;
      } else if constexpr (IsVector<Value>::value) {
        std::uint64_t size{};
        if (!unsigned64(size)
            || size > static_cast<std::uint64_t>(remaining()) + 1U
            || size > value.max_size()) {
          return fail("portable vector length exceeds the artifact");
        }
        value.clear();
        value.reserve(static_cast<std::size_t>(size));
        for (std::uint64_t index = 0; index < size; ++index) {
          value.emplace_back();
          if (!read(value.back())) {
            return false;
          }
        }
        return true;
      } else if constexpr (IsOptional<Value>::value) {
        bool present{};
        if (!read(present)) {
          return false;
        }
        if (!present) {
          value.reset();
          return true;
        }
        value.emplace();
        return read(*value);
      } else if constexpr (IsSharedPtr<Value>::value) {
        bool present{};
        if (!read(present)) {
          return false;
        }
        if (!present) {
          value.reset();
          return true;
        }
        value = std::make_shared<typename Value::element_type>();
        return read(*value);
      } else if constexpr (requires { archive_fields(value); }) {
        bool ok = true;
        std::apply(
            [&](auto&... fields) { ((ok = ok && read(fields)), ...); },
            archive_fields(value));
        return ok;
      } else if constexpr (std::is_aggregate_v<Value>) {
        bool ok = true;
        boost::pfr::for_each_field(
            value, [&](auto& field) { ok = ok && read(field); });
        return ok;
      } else {
        static_assert(sizeof(Value) == 0, "unsupported portable-unit field");
      }
    }();
    leave();
    return result;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - position_;
  }
  [[nodiscard]] const std::string& failure() const noexcept { return failure_; }

 private:
  bool enter() {
    if (++depth_ > kMaximumArchiveNesting) {
      --depth_;
      return fail("portable unit exceeds the safe structural nesting depth");
    }
    return true;
  }
  void leave() noexcept { --depth_; }
  bool fail(std::string message) {
    if (failure_.empty()) {
      failure_ = std::move(message);
    }
    return false;
  }

  std::string_view bytes_;
  std::size_t position_{};
  std::size_t depth_{};
  std::string failure_;
};

template <typename T>
bool has_absolute_span(
    const T& value,
    std::unordered_set<const void*>& visited) {
  using Value = std::remove_cv_t<T>;
  if constexpr (std::same_as<Value, frontend::SourceSpan>) {
    return std::filesystem::path(value.source_name).is_absolute()
        || std::filesystem::path(value.physical_source_name).is_absolute();
  } else if constexpr (
      std::is_arithmetic_v<Value> || std::is_enum_v<Value>
      || std::same_as<Value, std::string>) {
    return false;
  } else if constexpr (IsVector<Value>::value) {
    return std::ranges::any_of(
        value, [&](const auto& item) { return has_absolute_span(item, visited); });
  } else if constexpr (IsOptional<Value>::value) {
    return value.has_value() && has_absolute_span(*value, visited);
  } else if constexpr (IsSharedPtr<Value>::value) {
    return value && visited.insert(value.get()).second
        && has_absolute_span(*value, visited);
  } else if constexpr (requires { archive_fields(value); }) {
    bool found = false;
    std::apply(
        [&](const auto&... fields) {
          ((found = found || has_absolute_span(fields, visited)), ...);
        },
        archive_fields(value));
    return found;
  } else if constexpr (std::is_aggregate_v<Value>) {
    bool found = false;
    boost::pfr::for_each_field(
        value,
        [&](const auto& field) {
          found = found || has_absolute_span(field, visited);
        });
    return found;
  } else {
    static_assert(sizeof(Value) == 0, "unsupported portable-unit field");
  }
}

std::optional<std::string> mapped_source_name(
    const std::string& name,
    const std::span<const SourceNameMapping> mappings) {
  if (name.empty()) {
    return name;
  }
  const auto normalized = std::filesystem::path(name).lexically_normal();
  const auto found = std::ranges::find_if(
      mappings,
      [&](const auto& mapping) {
        if (mapping.producer_name == name
            || std::filesystem::path(mapping.producer_name).lexically_normal()
                == normalized) {
          return true;
        }
        // Windows path comparison is lexically case-sensitive even when the
        // underlying filesystem is not.  Producer paths name checked source
        // files, so use filesystem identity as the portable fallback.
        std::error_code error;
        return std::filesystem::equivalent(
                   fsim::support::path_from_utf8(mapping.producer_name),
                   fsim::support::path_from_utf8(name), error)
            && !error;
      });
  if (found != mappings.end()) {
    return found->logical_name;
  }
  return std::filesystem::path(name).is_absolute()
      ? std::nullopt : std::optional<std::string>{name};
}

template <typename T>
bool remap_spans(
    T& value,
    const std::span<const SourceNameMapping> mappings,
    std::unordered_set<const void*>& visited,
    std::string& missing) {
  using Value = std::remove_cv_t<T>;
  if constexpr (std::same_as<Value, frontend::SourceSpan>) {
    const auto source = mapped_source_name(value.source_name, mappings);
    const auto physical = mapped_source_name(
        value.physical_source_name, mappings);
    if (!source.has_value() || !physical.has_value()) {
      missing = !source.has_value()
          ? value.source_name : value.physical_source_name;
      return false;
    }
    value.source_name = *source;
    value.physical_source_name = *physical;
    return true;
  } else if constexpr (
      std::is_arithmetic_v<Value> || std::is_enum_v<Value>
      || std::same_as<Value, std::string>) {
    return true;
  } else if constexpr (IsVector<Value>::value) {
    return std::ranges::all_of(
        value,
        [&](auto& item) {
          return remap_spans(item, mappings, visited, missing);
        });
  } else if constexpr (IsOptional<Value>::value) {
    return !value.has_value()
        || remap_spans(*value, mappings, visited, missing);
  } else if constexpr (IsSharedPtr<Value>::value) {
    return !value || !visited.insert(value.get()).second
        || remap_spans(*value, mappings, visited, missing);
  } else if constexpr (requires { archive_fields(value); }) {
    bool ok = true;
    std::apply(
        [&](auto&... fields) {
          ((ok = ok && remap_spans(fields, mappings, visited, missing)), ...);
        },
        archive_fields(value));
    return ok;
  } else if constexpr (std::is_aggregate_v<Value>) {
    bool ok = true;
    boost::pfr::for_each_field(
        value,
        [&](auto& field) {
          ok = ok && remap_spans(field, mappings, visited, missing);
        });
    return ok;
  } else {
    static_assert(sizeof(Value) == 0, "unsupported portable-unit field");
  }
}

}  // namespace

bool relocate_unit_sources(
    frontend::DesignUnit& unit,
    const std::span<const SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics) {
  std::unordered_set<const void*> visited;
  std::string missing;
  if (!remap_spans(unit, mappings, visited, missing)) {
    diagnostics.error(
        std::string{kCode},
        "no relocatable source identity was supplied for producer path '"
            + missing + "'");
    return false;
  }
  visited.clear();
  if (has_absolute_span(unit, visited)) {
    diagnostics.error(
        std::string{kCode},
        "source relocation left a producer-absolute path in the unit");
    return false;
  }
  return true;
}

bool relocate_udp_sources(
    frontend::VerilogUdpDeclaration& declaration,
    const std::span<const SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics) {
  std::unordered_set<const void*> visited;
  std::string missing;
  if (!remap_spans(declaration, mappings, visited, missing)) {
    diagnostics.error(
        std::string{kCode},
        "no relocatable source identity was supplied for producer path '"
            + missing + "'");
    return false;
  }
  visited.clear();
  if (has_absolute_span(declaration, visited)) {
    diagnostics.error(
        std::string{kCode},
        "source relocation left a producer-absolute path in the UDP "
        "declaration");
    return false;
  }
  return true;
}

std::optional<std::string> serialize_portable_unit(
    const frontend::DesignUnit& unit,
    diagnostic::Engine& diagnostics) {
  std::unordered_set<const void*> visited;
  if (has_absolute_span(unit, visited)) {
    diagnostics.error(
        std::string{kCode},
        "portable unit contains a producer-absolute source path");
    return std::nullopt;
  }
  Writer writer;
  writer.raw(kMagic);
  auto schema = kOwningUnitSchemaVersion;
  writer.write(schema);
  auto owning = unit;
  writer.write(owning);
  if (!writer.failure().empty()) {
    diagnostics.error(std::string{kCode}, writer.failure());
    return std::nullopt;
  }
  return std::move(writer).finish();
}

std::optional<std::string> serialize_portable_udp(
    const frontend::VerilogUdpDeclaration& declaration,
    diagnostic::Engine& diagnostics) {
  if (!frontend::verilog_udp_declaration_well_formed(declaration)) {
    diagnostics.error(
        std::string{kCode},
        "portable UDP declaration is structurally invalid or exceeds its "
        "resource budget");
    return std::nullopt;
  }
  std::unordered_set<const void*> visited;
  if (has_absolute_span(declaration, visited)) {
    diagnostics.error(
        std::string{kCode},
        "portable UDP declaration contains a producer-absolute source path");
    return std::nullopt;
  }
  Writer writer;
  writer.raw(kUdpMagic);
  auto schema = kUdpDeclarationSchemaVersion;
  writer.write(schema);
  auto owning = declaration;
  writer.write(owning);
  if (!writer.failure().empty()) {
    diagnostics.error(std::string{kCode}, writer.failure());
    return std::nullopt;
  }
  return std::move(writer).finish();
}

std::optional<frontend::DesignUnit> deserialize_portable_unit(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  Reader reader(bytes);
  std::uint32_t schema{};
  frontend::DesignUnit unit;
  if (!reader.raw(kMagic) || !reader.read(schema)
      || schema != kOwningUnitSchemaVersion || !reader.read(unit)
      || reader.remaining() != 0) {
    auto message = reader.failure();
    if (message.empty() && schema != kOwningUnitSchemaVersion) {
      message = "unsupported portable owning-unit schema "
          + std::to_string(schema);
    } else if (message.empty()) {
      message = "portable unit contains trailing bytes";
    }
    diagnostics.error(
        std::string{kCode}, std::move(message),
        {std::move(source_name), {1, 1, 0}, {1, 1, 0}});
    return std::nullopt;
  }
  return unit;
}

std::optional<frontend::VerilogUdpDeclaration> deserialize_portable_udp(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  Reader reader(bytes);
  std::uint32_t schema{};
  frontend::VerilogUdpDeclaration declaration;
  if (!reader.raw(kUdpMagic) || !reader.read(schema)
      || schema != kUdpDeclarationSchemaVersion
      || !reader.read(declaration) || reader.remaining() != 0
      || !frontend::verilog_udp_declaration_well_formed(declaration)) {
    auto message = reader.failure();
    if (message.empty() && schema != kUdpDeclarationSchemaVersion) {
      message = "unsupported portable UDP-declaration schema "
          + std::to_string(schema);
    } else if (message.empty() && reader.remaining() != 0) {
      message = "portable UDP declaration contains trailing bytes";
    } else if (message.empty()) {
      message = "portable UDP declaration is structurally invalid or "
          "exceeds its resource budget";
    }
    diagnostics.error(
        std::string{kCode}, std::move(message),
        {std::move(source_name), {1, 1, 0}, {1, 1, 0}});
    return std::nullopt;
  }
  return declaration;
}

}  // namespace fsim::library
