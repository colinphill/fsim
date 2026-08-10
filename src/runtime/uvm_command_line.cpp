// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_command_line.hpp"

#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_report.hpp"

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

constexpr std::string_view kCommandInvalid{"FSIM-UVM-CMD-001"};
constexpr std::string_view kCommandResource{"FSIM-UVM-CMD-002"};

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
      || name == "+UVM_TIMEOUT"
      || name == "+UVM_MAX_QUIT_COUNT";
}

bool is_uvm_argument(const std::string_view argument) {
  return argument.starts_with("+UVM_")
      || argument.starts_with("+uvm_");
}

bool wildcard_match(
    const std::string_view pattern,
    const std::string_view value,
    std::size_t& work,
    const std::size_t maximum_work,
    const std::size_t source,
    const std::string& argument) {
  std::size_t pattern_index{};
  std::size_t value_index{};
  std::size_t star = std::string_view::npos;
  std::size_t retry{};
  const auto account = [&] {
    if (++work > maximum_work) {
      throw Error{source, argument,
                  "UVM report-control wildcard work exceeds its limit",
                  std::string{kCommandResource}};
    }
  };
  while (value_index < value.size()) {
    account();
    if (pattern_index < pattern.size()
        && (pattern[pattern_index] == '?'
            || pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size()
               && pattern[pattern_index] == '*') {
      star = pattern_index++;
      retry = value_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1;
      value_index = ++retry;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
    account();
    ++pattern_index;
  }
  return pattern_index == pattern.size();
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
    std::string message,
    std::string diagnostic_code)
    : std::runtime_error(
          std::move(message) + " in command-line plusarg '" + argument + "'"),
      argument_index_(argument_index),
      argument_(std::move(argument)),
      diagnostic_code_(std::move(diagnostic_code)) {}

SystemVerilogUvmCommandLineService::SystemVerilogUvmCommandLineService(
    SystemVerilogUvmFactoryService& factory,
    SystemVerilogUvmResourcePoolService& resources,
    SystemVerilogUvmConfigDbService& config,
    SystemVerilogUvmCommandLineLimits limits,
    std::string tool_name,
    std::string tool_version)
    : factory_(&factory), resources_(&resources), config_(&config),
      limits_(std::move(limits)), tool_name_(std::move(tool_name)),
      tool_version_(std::move(tool_version)) {
  if (limits_.max_arguments == 0 || limits_.max_argument_bytes == 0
      || limits_.max_total_argument_bytes == 0 || limits_.max_settings == 0
      || limits_.max_query_bytes == 0 || limits_.max_query_results == 0
      || limits_.max_query_result_bytes == 0 || limits_.max_tool_bytes == 0
      || limits_.max_report_control_matches == 0
      || limits_.max_report_control_work == 0
      || limits_.max_report_control_trace_bytes == 0) {
    throw std::invalid_argument{"UVM command-line limits must be nonzero"};
  }
  if (tool_name_.empty() || tool_version_.empty()
      || tool_name_.size() > limits_.max_tool_bytes
      || tool_version_.size() > limits_.max_tool_bytes) {
    throw std::invalid_argument{
        "UVM command-line tool name/version is empty or exceeds its limit"};
  }
}

Settings SystemVerilogUvmCommandLineService::parse(
    const std::span<const std::string> arguments) const {
  if (arguments.size() > limits_.max_arguments) {
    throw Error{0, {}, "UVM command-line argument count exceeds its limit",
                std::string{kCommandResource}};
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
      throw Error{index, argument,
                  "UVM command-line argument bytes exceed their limit",
                  std::string{kCommandResource}};
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
    } else if (argument == "+UVM_PHASE_TRACE") {
      result.phase_trace = true;
      ++recognized;
    } else if (argument == "+UVM_OBJECTION_TRACE") {
      result.objection_trace = true;
      ++recognized;
    } else if (name == "+UVM_RESOURCE_DB_TRACE"
               || name == "+UVM_CONFIG_DB_TRACE"
               || name == "+UVM_PHASE_TRACE"
               || name == "+UVM_OBJECTION_TRACE") {
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
      } else if (name == "+UVM_MAX_QUIT_COUNT") {
        const auto fields = split_fields(value);
        if (fields.size() != 2
            || (fields[1] != "YES" && fields[1] != "NO")) {
          throw Error{index, argument,
                      "max quit count requires unsigned count and YES or NO "
                      "overridable flag"};
        }
        ++result.max_quit_count_argument_count;
        const SystemVerilogUvmCommandMaxQuitSetting parsed{
            parse_unsigned(fields[0], index, argument, "max quit count"),
            fields[1] == "YES", index};
        if (!result.max_quit_count) result.max_quit_count = parsed;
      }
      ++recognized;
    }
    if (recognized > limits_.max_settings) {
      throw Error{index, argument,
                  "UVM command-line setting count exceeds its limit",
                  std::string{kCommandResource}};
    }
  }
  return result;
}

void SystemVerilogUvmCommandLineService::validate_query(
    const std::string_view query) const {
  if (query.empty()) {
    throw Error{0, {}, "UVM command-line query is empty",
                std::string{kCommandInvalid}};
  }
  if (query.size() > limits_.max_query_bytes) {
    throw Error{0, std::string{query},
                "UVM command-line query exceeds its text limit",
                std::string{kCommandResource}};
  }
}

void SystemVerilogUvmCommandLineService::validate_query_results(
    const std::span<const std::string> results,
    const std::string_view query) const {
  if (results.size() > limits_.max_query_results) {
    throw Error{0, std::string{query},
                "UVM command-line query result count exceeds its limit",
                std::string{kCommandResource}};
  }
  std::size_t bytes{};
  for (const auto& result : results) {
    if (result.size() > limits_.max_query_result_bytes
        || bytes > limits_.max_query_result_bytes - result.size()) {
      throw Error{0, std::string{query},
                  "UVM command-line query result bytes exceed their limit",
                  std::string{kCommandResource}};
    }
    bytes += result.size();
  }
}

std::vector<std::string>
SystemVerilogUvmCommandLineService::get_plusargs() const {
  std::vector<std::string> result;
  for (const auto& argument : settings_.arguments) {
    if (argument.starts_with('+')) result.push_back(argument);
  }
  validate_query_results(result, "+");
  return result;
}

std::vector<std::string>
SystemVerilogUvmCommandLineService::get_uvm_args() const {
  std::vector<std::string> result;
  for (const auto& argument : settings_.arguments) {
    if (is_uvm_argument(argument)) result.push_back(argument);
  }
  validate_query_results(result, "+UVM_/+uvm_");
  return result;
}

std::vector<std::string>
SystemVerilogUvmCommandLineService::get_arg_matches(
    const std::string_view match, const bool exact) const {
  validate_query(match);
  std::vector<std::string> result;
  for (const auto& argument : settings_.arguments) {
    if ((exact && argument == match)
        || (!exact && argument.starts_with(match))) {
      result.push_back(argument);
    }
  }
  validate_query_results(result, match);
  return result;
}

std::vector<std::string>
SystemVerilogUvmCommandLineService::get_arg_values(
    const std::string_view prefix) const {
  validate_query(prefix);
  std::vector<std::string> result;
  for (const auto& argument : settings_.arguments) {
    if (argument.starts_with(prefix)) {
      result.push_back(argument.substr(prefix.size()));
    }
  }
  validate_query_results(result, prefix);
  return result;
}

std::optional<std::string>
SystemVerilogUvmCommandLineService::get_arg_value(
    const std::string_view prefix) const {
  auto values = get_arg_values(prefix);
  return values.empty()
      ? std::nullopt
      : std::optional<std::string>{std::move(values.front())};
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
    throw Error{source, argument,
                "UVM command-line config settings exceed resource capacity",
                std::string{kCommandResource}};
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
  if (settings_.resource_db_trace) resources_->set_trace_enabled(true);
  if (settings_.config_db_trace) config_->set_trace_enabled(true);
  initial_report_settings_applied_ = false;
  report_settings_applied_.assign(
      settings_.verbosity_settings.size(), false);
}

void SystemVerilogUvmCommandLineService::apply_initial_report_settings(
    SystemVerilogUvmReportService& reports,
    SystemVerilogUvmObjectionService& objections) {
  if (initial_report_settings_applied_) return;
  reports.set_default_verbosity(settings_.initial_verbosity.value_or(200));
  if (settings_.max_quit_count) {
    (void)reports.server().set_max_quit_count(
        settings_.max_quit_count->count,
        settings_.max_quit_count->overridable);
  }
  objections.set_trace_enabled(settings_.objection_trace);
  initial_report_settings_applied_ = true;
}

std::vector<SystemVerilogUvmCommandReportApplication>
SystemVerilogUvmCommandLineService::apply_report_settings(
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmReportService& reports,
    const std::string_view phase,
    const std::uint64_t time) {
  if (phase.empty() || phase.size() > limits_.max_phase_bytes) {
    throw Error{0, {}, "UVM report-control phase is empty or exceeds its limit"};
  }
  struct Candidate {
    SystemVerilogUvmRootHandle root{};
    SystemVerilogClassHandle component{};
    std::string full_name;
  };
  std::vector<Candidate> candidates;
  for (const auto root : components.roots()) {
    for (const auto top : components.top_components(root)) {
      std::vector<SystemVerilogClassHandle> pending{top};
      while (!pending.empty()) {
        const auto component = pending.back();
        pending.pop_back();
        candidates.push_back({
            root, component, std::string{components.full_name(component)}});
        const auto children = components.children(component);
        pending.insert(pending.end(), children.rbegin(), children.rend());
      }
    }
  }
  std::ranges::sort(candidates, {}, [](const auto& candidate) {
    return std::pair{candidate.root, candidate.full_name};
  });

  std::vector<SystemVerilogUvmCommandReportApplication> result;
  std::vector<std::size_t> ready_settings;
  std::size_t work{};
  std::size_t trace_bytes{};
  for (std::size_t setting_index = 0;
       setting_index < settings_.verbosity_settings.size(); ++setting_index) {
    if (report_settings_applied_.at(setting_index)) continue;
    const auto& setting = settings_.verbosity_settings[setting_index];
    const auto ready = setting.phase == "time"
        ? phase == "time" && setting.time_offset
              && time >= *setting.time_offset
        : setting.phase == phase && !setting.time_offset;
    if (!ready) continue;
    const auto& argument = settings_.arguments.at(setting.source_index);
    for (const auto& candidate : candidates) {
      if (!wildcard_match(
              setting.component_pattern, candidate.full_name, work,
              limits_.max_report_control_work, setting.source_index,
              argument)) {
        continue;
      }
      if (result.size() >= limits_.max_report_control_matches) {
        throw Error{setting.source_index, argument,
                    "UVM report-control matches exceed their limit",
                    std::string{kCommandResource}};
      }
      SystemVerilogUvmCommandReportApplication application{
          setting.source_index, candidate.root, candidate.component,
          candidate.full_name, setting.id, setting.verbosity,
          setting.phase, time};
      const auto bytes = application.component_name.size()
          + application.id.size() + application.phase.size();
      if (bytes > limits_.max_report_control_trace_bytes - trace_bytes) {
        throw Error{setting.source_index, argument,
                    "UVM report-control trace bytes exceed their limit",
                    std::string{kCommandResource}};
      }
      trace_bytes += bytes;
      result.push_back(std::move(application));
    }
    ready_settings.push_back(setting_index);
  }
  std::set<SystemVerilogClassHandle> new_handlers;
  std::set<std::pair<SystemVerilogClassHandle, std::string>> new_settings;
  for (const auto& application : result) {
    if (!reports.has_handler(application.component)) {
      new_handlers.insert(application.component);
    }
    if (application.id != "_ALL_"
        && !reports.has_id_verbosity(application.component, application.id)) {
      new_settings.emplace(application.component, application.id);
    }
  }
  if (new_handlers.size()
          > reports.limits().max_handlers - reports.handler_count()
      || new_settings.size()
          > reports.limits().max_settings - reports.setting_count()) {
    const auto source = ready_settings.empty()
        ? std::size_t{}
        : settings_.verbosity_settings.at(ready_settings.back()).source_index;
    throw Error{
        source,
        source < settings_.arguments.size()
            ? settings_.arguments[source] : std::string{},
        "UVM report-control application exceeds report handler/setting "
        "capacity",
        std::string{kCommandResource}};
  }
  for (const auto& application : result) {
    if (application.id == "_ALL_") {
      reports.set_verbosity(application.component, application.verbosity);
    } else {
      reports.set_id_verbosity(
          application.component, application.id, application.verbosity);
    }
  }
  for (const auto setting_index : ready_settings) {
    report_settings_applied_[setting_index] = true;
  }
  return result;
}

}  // namespace fsim::runtime
