// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_command_line.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <set>
#include <system_error>

namespace fsim::runtime {
namespace {

using Error = SystemVerilogUvmCommandLineError;
using Settings = SystemVerilogUvmCommandLineSettings;

constexpr std::array<std::string_view, 9> kValuedOptionNames{
    "+uvm_set_inst_override", "+UVM_SET_INST_OVERRIDE",
    "+uvm_set_type_override", "+UVM_SET_TYPE_OVERRIDE",
    "+uvm_set_config_int", "+UVM_SET_CONFIG_INT",
    "+uvm_set_config_bitstream", "+UVM_SET_CONFIG_BITSTREAM",
    "+uvm_set_config_string"};

bool is_known_valued_name(const std::string_view name) {
  if (std::ranges::find(kValuedOptionNames, name)
      != kValuedOptionNames.end()) {
    return true;
  }
  return name == "+UVM_SET_CONFIG_STRING"
      || name == "+UVM_VERBOSITY"
      || name == "+uvm_set_verbosity"
      || name == "+UVM_TIMEOUT";
}

std::vector<std::string_view> split_fields(const std::string_view value) {
  std::vector<std::string_view> result;
  std::size_t begin{};
  while (true) {
    const auto comma = value.find(',', begin);
    result.push_back(value.substr(
        begin, comma == std::string_view::npos
                   ? std::string_view::npos : comma - begin));
    if (comma == std::string_view::npos) break;
    begin = comma + 1;
  }
  return result;
}

void require_nonempty(
    const std::string_view value,
    const std::size_t index,
    const std::string& argument,
    const std::string_view field) {
  if (value.empty()) {
    throw Error{index, argument, std::string{field} + " must not be empty"};
  }
}

void require_size(
    const std::string_view value,
    const std::size_t limit,
    const std::size_t index,
    const std::string& argument,
    const std::string_view field) {
  require_nonempty(value, index, argument, field);
  if (value.size() > limit) {
    throw Error{index, argument, std::string{field} + " exceeds its byte limit"};
  }
}

std::uint64_t parse_unsigned(
    const std::string_view value,
    const std::size_t index,
    const std::string& argument,
    const std::string_view field) {
  std::uint64_t result{};
  const auto [end, error] = std::from_chars(
      value.data(), value.data() + value.size(), result);
  if (value.empty() || error != std::errc{} || end != value.data() + value.size()) {
    throw Error{index, argument, std::string{field} + " must be an unsigned decimal integer"};
  }
  return result;
}

std::optional<std::int32_t> named_verbosity(const std::string_view value) {
  if (value == "UVM_NONE") return 0;
  if (value == "UVM_LOW") return 100;
  if (value == "UVM_MEDIUM") return 200;
  if (value == "UVM_HIGH") return 300;
  if (value == "UVM_FULL") return 400;
  if (value == "UVM_DEBUG") return 500;
  return std::nullopt;
}

std::int32_t parse_verbosity(
    const std::string_view value,
    const bool allow_numeric,
    const std::size_t index,
    const std::string& argument) {
  if (const auto named = named_verbosity(value)) return *named;
  if (allow_numeric) {
    std::int32_t numeric{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), numeric);
    if (!value.empty() && error == std::errc{}
        && end == value.data() + value.size() && numeric >= 0) {
      return numeric;
    }
  }
  throw Error{
      index, argument,
      "verbosity must be UVM_NONE, UVM_LOW, UVM_MEDIUM, UVM_HIGH, "
      "UVM_FULL, or UVM_DEBUG"};
}

unsigned digit_value(const char digit) {
  if (digit >= '0' && digit <= '9') return unsigned(digit - '0');
  if (digit >= 'a' && digit <= 'f') return unsigned(digit - 'a') + 10U;
  if (digit >= 'A' && digit <= 'F') return unsigned(digit - 'A') + 10U;
  return std::numeric_limits<unsigned>::max();
}

std::pair<unsigned, std::string_view> numeric_base(
    const std::string_view value) {
  if (value.size() < 2) return {10, value};
  const auto prefix = value.substr(0, 2);
  if (prefix == "'b" || prefix == "0b") return {2, value.substr(2)};
  if (prefix == "'o") return {8, value.substr(2)};
  if (prefix == "'d") return {10, value.substr(2)};
  if (prefix == "'h" || prefix == "'x" || prefix == "0x") {
    return {16, value.substr(2)};
  }
  return {10, value};
}

PackedLogic4 parse_config_integer(
    const std::string_view value,
    const std::size_t index,
    const std::string& argument) {
  const auto [base, digits] = numeric_base(value);
  std::uint32_t word{};
  bool negative{};
  if (base == 10) {
    std::string normalized;
    normalized.reserve(digits.size());
    for (const char digit : digits) {
      if (digit != '_') normalized.push_back(digit);
    }
    std::int64_t parsed{};
    const auto [end, error] = std::from_chars(
        normalized.data(), normalized.data() + normalized.size(), parsed);
    if (normalized.empty() || error != std::errc{}
        || end != normalized.data() + normalized.size()
        || parsed < std::numeric_limits<std::int32_t>::min()
        || parsed > std::numeric_limits<std::int32_t>::max()) {
      throw Error{index, argument, "config integer must fit a signed 32-bit value"};
    }
    negative = parsed < 0;
    word = static_cast<std::uint32_t>(static_cast<std::int32_t>(parsed));
  } else {
    bool saw_digit{};
    std::uint64_t parsed{};
    for (const char digit : digits) {
      if (digit == '_') continue;
      const auto next = digit_value(digit);
      if (next >= base
          || parsed > (std::numeric_limits<std::uint32_t>::max() - next) / base) {
        throw Error{index, argument, "config integer base digits are invalid or exceed 32 bits"};
      }
      saw_digit = true;
      parsed = parsed * base + next;
    }
    if (!saw_digit) {
      throw Error{index, argument, "config integer requires digits after its base prefix"};
    }
    word = static_cast<std::uint32_t>(parsed);
  }
  PackedLogic4 result(
      kSystemVerilogUvmBitstreamWidth,
      negative ? Logic4::one : Logic4::zero);
  for (std::size_t bit = 0; bit < 32; ++bit) {
    result.set(bit, (word & (std::uint32_t{1} << bit))
                        ? Logic4::one : Logic4::zero);
  }
  return result;
}

std::string decimal_bits(
    const std::string_view digits,
    const std::size_t index,
    const std::string& argument) {
  std::vector<unsigned char> bits(1, 0);
  bool saw_digit{};
  for (const char digit : digits) {
    if (digit == '_') continue;
    if (digit < '0' || digit > '9') {
      throw Error{index, argument, "bitstream decimal value contains a non-digit"};
    }
    saw_digit = true;
    unsigned carry = unsigned(digit - '0');
    for (auto& bit : bits) {
      const unsigned value = unsigned(bit) * 10U + carry;
      bit = static_cast<unsigned char>(value & 1U);
      carry = value >> 1U;
    }
    while (carry != 0) {
      if (bits.size() >= kSystemVerilogUvmBitstreamWidth) {
        throw Error{index, argument, "bitstream value exceeds UVM bitstream width"};
      }
      bits.push_back(static_cast<unsigned char>(carry & 1U));
      carry >>= 1U;
    }
  }
  if (!saw_digit) {
    throw Error{index, argument, "bitstream value requires decimal digits"};
  }
  std::string result;
  result.reserve(bits.size());
  for (auto bit = bits.rbegin(); bit != bits.rend(); ++bit) {
    result.push_back(*bit ? '1' : '0');
  }
  return result;
}

std::string based_bits(
    const unsigned base,
    const std::string_view digits,
    const std::size_t index,
    const std::string& argument) {
  const unsigned width = base == 2 ? 1U : base == 8 ? 3U : 4U;
  std::string result;
  for (const char digit : digits) {
    if (digit == '_') continue;
    const bool unknown = digit == 'x' || digit == 'X';
    const bool high_impedance = digit == 'z' || digit == 'Z' || digit == '?';
    const auto numeric = digit_value(digit);
    if (!unknown && !high_impedance && numeric >= base) {
      throw Error{index, argument, "bitstream value contains an invalid base digit"};
    }
    if (result.size() + width > kSystemVerilogUvmBitstreamWidth) {
      throw Error{index, argument, "bitstream value exceeds UVM bitstream width"};
    }
    for (unsigned bit = width; bit > 0; --bit) {
      result.push_back(unknown ? 'x' : high_impedance ? 'z'
          : (numeric & (1U << (bit - 1U))) ? '1' : '0');
    }
  }
  if (result.empty()) {
    throw Error{index, argument, "bitstream value requires digits after its base prefix"};
  }
  return result;
}

PackedLogic4 parse_config_bitstream(
    const std::string_view value,
    const std::size_t index,
    const std::string& argument) {
  const auto [base, digits] = numeric_base(value);
  auto bits = base == 10
      ? decimal_bits(digits, index, argument)
      : based_bits(base, digits, index, argument);
  if (bits.size() < kSystemVerilogUvmBitstreamWidth) {
    bits.insert(0, kSystemVerilogUvmBitstreamWidth - bits.size(), '0');
  }
  return PackedLogic4::from_msb_string(bits);
}

std::string config_key(
    const std::string_view context,
    const std::string_view component,
    const std::string_view field,
    const std::string_view type) {
  std::string result;
  result.reserve(
      context.size() + component.size() + field.size() + type.size() + 4);
  result.append(context);
  result.push_back('\0');
  result.append(component);
  result.push_back('\0');
  result.append(field);
  result.push_back('\0');
  result.append(type);
  return result;
}

std::string_view config_type(
    const SystemVerilogUvmCommandConfigKind kind) {
  return kind == SystemVerilogUvmCommandConfigKind::String
      ? kSystemVerilogUvmStringConfigType
      : kSystemVerilogUvmBitstreamConfigType;
}

}  // namespace

SystemVerilogUvmCommandLineError::SystemVerilogUvmCommandLineError(
    const std::size_t argument_index,
    std::string argument,
    std::string message)
    : std::runtime_error(
          std::move(message) + " in command-line plusarg '" + argument + "'"),
      argument_index_(argument_index),
      argument_(std::move(argument)) {}

SystemVerilogUvmCommandLineService::SystemVerilogUvmCommandLineService(
    SystemVerilogUvmFactoryService& factory,
    SystemVerilogUvmResourcePoolService& resources,
    SystemVerilogUvmConfigDbService& config,
    SystemVerilogUvmCommandLineLimits limits)
    : factory_(&factory), resources_(&resources), config_(&config),
      limits_(std::move(limits)) {
  if (limits_.max_arguments == 0 || limits_.max_argument_bytes == 0
      || limits_.max_total_argument_bytes == 0 || limits_.max_settings == 0) {
    throw std::invalid_argument{"UVM command-line limits must be nonzero"};
  }
}

Settings SystemVerilogUvmCommandLineService::parse(
    const std::span<const std::string> arguments) const {
  if (arguments.size() > limits_.max_arguments) {
    throw Error{0, {}, "UVM command-line argument count exceeds its limit"};
  }
  Settings result;
  result.arguments.assign(arguments.begin(), arguments.end());
  std::size_t total_bytes{};
  std::size_t recognized{};
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const auto& argument = arguments[index];
    if (argument.size() > limits_.max_argument_bytes
        || argument.size() > limits_.max_total_argument_bytes
        || total_bytes
            > limits_.max_total_argument_bytes - argument.size()) {
      throw Error{index, argument, "UVM command-line argument bytes exceed their limit"};
    }
    total_bytes += argument.size();
    const auto equal = argument.find('=');
    const std::string_view name{
        argument.data(),
        equal == std::string::npos ? argument.size() : equal};
    if (argument == "+UVM_RESOURCE_DB_TRACE") {
      result.resource_db_trace = true;
      ++recognized;
    } else if (argument == "+UVM_CONFIG_DB_TRACE") {
      result.config_db_trace = true;
      ++recognized;
    } else if (name == "+UVM_RESOURCE_DB_TRACE"
               || name == "+UVM_CONFIG_DB_TRACE") {
      throw Error{index, argument, "trace plusarg does not accept a value"};
    } else if (!is_known_valued_name(name)) {
      result.unknown_arguments.push_back(argument);
      continue;
    } else {
      if (equal == std::string::npos) {
        throw Error{index, argument, "recognized UVM plusarg requires '='"};
      }
      const std::string_view value{argument.data() + equal + 1,
                                   argument.size() - equal - 1};
      if (name == "+uvm_set_inst_override"
          || name == "+UVM_SET_INST_OVERRIDE") {
        const auto fields = split_fields(value);
        if (fields.size() != 3) {
          throw Error{index, argument, "instance override requires requested type, override type, and instance path"};
        }
        require_size(fields[0], limits_.max_type_name_bytes, index, argument, "requested type");
        require_size(fields[1], limits_.max_type_name_bytes, index, argument, "override type");
        require_size(fields[2], limits_.max_component_pattern_bytes, index, argument, "instance path");
        result.factory_settings.push_back({
            SystemVerilogUvmCommandFactoryKind::Instance,
            std::string{fields[0]}, std::string{fields[1]},
            std::string{fields[2]}, true, index});
      } else if (name == "+uvm_set_type_override"
                 || name == "+UVM_SET_TYPE_OVERRIDE") {
        const auto fields = split_fields(value);
        if (fields.size() < 2 || fields.size() > 3) {
          throw Error{index, argument, "type override requires requested type, override type, and optional replace flag"};
        }
        require_size(fields[0], limits_.max_type_name_bytes, index, argument, "requested type");
        require_size(fields[1], limits_.max_type_name_bytes, index, argument, "override type");
        const bool replace = fields.size() == 2 || fields[2] == "1";
        if (fields.size() == 3 && fields[2] != "0" && fields[2] != "1") {
          throw Error{index, argument, "type override replace flag must be 0 or 1"};
        }
        result.factory_settings.push_back({
            SystemVerilogUvmCommandFactoryKind::Type,
            std::string{fields[0]}, std::string{fields[1]}, {}, replace, index});
      } else if (name == "+uvm_set_config_int"
                 || name == "+UVM_SET_CONFIG_INT"
                 || name == "+uvm_set_config_bitstream"
                 || name == "+UVM_SET_CONFIG_BITSTREAM"
                 || name == "+uvm_set_config_string"
                 || name == "+UVM_SET_CONFIG_STRING") {
        const auto fields = split_fields(value);
        if (fields.size() != 3) {
          throw Error{index, argument, "config setting requires component, field, and value"};
        }
        require_size(fields[0], limits_.max_component_pattern_bytes, index, argument, "component pattern");
        require_size(fields[1], limits_.max_field_bytes, index, argument, "config field");
        const auto kind = name.ends_with("config_string")
                || name.ends_with("CONFIG_STRING")
            ? SystemVerilogUvmCommandConfigKind::String
            : name.ends_with("config_bitstream")
                    || name.ends_with("CONFIG_BITSTREAM")
                ? SystemVerilogUvmCommandConfigKind::Bitstream
                : SystemVerilogUvmCommandConfigKind::Integer;
        SystemVerilogUvmResourceValue parsed = kind
                == SystemVerilogUvmCommandConfigKind::String
            ? SystemVerilogUvmResourceValue{std::string{fields[2]}}
            : kind == SystemVerilogUvmCommandConfigKind::Bitstream
                ? SystemVerilogUvmResourceValue{
                      parse_config_bitstream(fields[2], index, argument)}
                : SystemVerilogUvmResourceValue{
                      parse_config_integer(fields[2], index, argument)};
        result.config_settings.push_back({
            kind, std::string{fields[0]}, std::string{fields[1]},
            std::move(parsed), index});
      } else if (name == "+UVM_VERBOSITY") {
        ++result.initial_verbosity_argument_count;
        const auto parsed = parse_verbosity(value, true, index, argument);
        if (!result.initial_verbosity) result.initial_verbosity = parsed;
      } else if (name == "+uvm_set_verbosity") {
        const auto fields = split_fields(value);
        if (fields.size() < 4 || fields.size() > 5
            || (fields[3] == "time") != (fields.size() == 5)) {
          throw Error{index, argument, "verbosity setting requires component, id, verbosity, and phase or time plus offset"};
        }
        require_size(fields[0], limits_.max_component_pattern_bytes, index, argument, "component pattern");
        require_size(fields[1], limits_.max_id_bytes, index, argument, "report id");
        require_size(fields[3], limits_.max_phase_bytes, index, argument, "phase");
        std::optional<std::uint64_t> offset;
        if (fields.size() == 5) {
          offset = parse_unsigned(fields[4], index, argument, "verbosity time offset");
        }
        result.verbosity_settings.push_back({
            std::string{fields[0]}, std::string{fields[1]},
            parse_verbosity(fields[2], false, index, argument),
            std::string{fields[3]}, offset, index});
      } else if (name == "+UVM_TIMEOUT") {
        const auto fields = split_fields(value);
        if (fields.size() != 2
            || (fields[1] != "YES" && fields[1] != "NO")) {
          throw Error{index, argument, "timeout requires unsigned ticks and YES or NO overridable flag"};
        }
        ++result.timeout_argument_count;
        const SystemVerilogUvmCommandTimeoutSetting parsed{
            parse_unsigned(fields[0], index, argument, "timeout"),
            fields[1] == "YES", index};
        if (!result.timeout) result.timeout = parsed;
      }
      ++recognized;
    }
    if (recognized > limits_.max_settings) {
      throw Error{index, argument, "UVM command-line setting count exceeds its limit"};
    }
  }
  return result;
}

void SystemVerilogUvmCommandLineService::validate_resource_capacity(
    const Settings& settings) const {
  std::set<std::string, std::less<>> keys;
  for (const auto& entry : config_->entries()) {
    keys.insert(config_key(
        entry.context_name, entry.instance_pattern,
        entry.field_pattern, entry.type_identity));
  }
  const auto existing = keys.size();
  for (const auto& setting : settings.config_settings) {
    keys.insert(config_key(
        {}, setting.component_pattern, setting.field,
        config_type(setting.kind)));
  }
  const auto additional = keys.size() - existing;
  if (config_->entries().size() + additional > config_->limits().max_entries
      || resources_->size() + additional > resources_->limits().max_resources) {
    const auto source = settings.config_settings.empty()
        ? std::size_t{} : settings.config_settings.back().source_index;
    const auto argument = source < settings.arguments.size()
        ? settings.arguments[source] : std::string{};
    throw Error{source, argument, "UVM command-line config settings exceed resource capacity"};
  }
}

void SystemVerilogUvmCommandLineService::apply(
    const std::span<const std::string> arguments) {
  auto parsed = parse(arguments);
  validate_resource_capacity(parsed);
  const auto apply_failure = [&](const std::size_t source,
                                 const std::exception& error) -> Error {
    return Error{
        source, parsed.arguments.at(source),
        "cannot apply UVM setting: " + std::string{error.what()}};
  };
  for (const auto kind : {
           SystemVerilogUvmCommandFactoryKind::Instance,
           SystemVerilogUvmCommandFactoryKind::Type}) {
    for (const auto& setting : parsed.factory_settings) {
      if (setting.kind != kind) continue;
      try {
        if (kind == SystemVerilogUvmCommandFactoryKind::Instance) {
          (void)factory_->set_instance_override_by_name(
              setting.requested_type, setting.override_type,
              setting.instance_pattern);
        } else {
          (void)factory_->set_type_override_by_name(
              setting.requested_type, setting.override_type, setting.replace);
        }
      } catch (const std::exception& error) {
        throw apply_failure(setting.source_index, error);
      }
    }
  }
  for (const auto kind : {
           SystemVerilogUvmCommandConfigKind::Integer,
           SystemVerilogUvmCommandConfigKind::Bitstream,
           SystemVerilogUvmCommandConfigKind::String}) {
    for (const auto& setting : parsed.config_settings) {
      if (setting.kind != kind) continue;
      const auto type = kind == SystemVerilogUvmCommandConfigKind::String
          ? SystemVerilogUvmResourceType{
                std::string{kSystemVerilogUvmStringConfigType},
                SystemVerilogUvmResourceValueKind::String, 0}
          : SystemVerilogUvmResourceType{
                std::string{kSystemVerilogUvmBitstreamConfigType},
                SystemVerilogUvmResourceValueKind::Packed,
                kSystemVerilogUvmBitstreamWidth};
      try {
        (void)config_->set(
            {"", 0}, setting.component_pattern, setting.field,
            type, setting.value, SystemVerilogUvmConfigPhase::Build);
      } catch (const std::exception& error) {
        throw apply_failure(setting.source_index, error);
      }
    }
  }
  settings_ = std::move(parsed);
}

}  // namespace fsim::runtime
