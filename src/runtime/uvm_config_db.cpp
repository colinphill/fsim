// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_config_db.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

enum class Quantifier : std::uint8_t {
  One,
  ZeroOrOne,
  ZeroOrMore,
  OneOrMore,
};

struct RegexAtom {
  char value{};
  bool any{};
  Quantifier quantifier{Quantifier::One};
};

const char* trace_action_name(
    const SystemVerilogUvmConfigTraceAction action) noexcept {
  switch (action) {
    case SystemVerilogUvmConfigTraceAction::Set: return "set";
    case SystemVerilogUvmConfigTraceAction::Get: return "get";
    case SystemVerilogUvmConfigTraceAction::Exists: return "exists";
  }
  return "unknown";
}

void append_quoted(std::string& output, const std::string_view text) {
  output.push_back('"');
  for (const char character : text) {
    switch (character) {
      case '\\': output += "\\\\"; break;
      case '"': output += "\\\""; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 0x20U) {
          constexpr char digits[]{"0123456789abcdef"};
          output += "\\x";
          output.push_back(digits[
              (static_cast<unsigned char>(character) >> 4U) & 0xfU]);
          output.push_back(digits[
              static_cast<unsigned char>(character) & 0xfU]);
        } else {
          output.push_back(character);
        }
    }
  }
  output.push_back('"');
}

bool glob_matches(
    const std::string_view pattern,
    const std::string_view value) noexcept {
  std::size_t pattern_index{};
  std::size_t value_index{};
  std::size_t star{std::string_view::npos};
  std::size_t star_value{};
  while (value_index < value.size()) {
    if (pattern_index < pattern.size()
        && (pattern[pattern_index] == '?'
            || pattern[pattern_index] == value[value_index])) {
      ++pattern_index;
      ++value_index;
    } else if (pattern_index < pattern.size()
               && pattern[pattern_index] == '*') {
      star = pattern_index++;
      star_value = value_index;
    } else if (star != std::string_view::npos) {
      pattern_index = star + 1;
      value_index = ++star_value;
    } else {
      return false;
    }
  }
  while (pattern_index < pattern.size()
         && pattern[pattern_index] == '*') {
    ++pattern_index;
  }
  return pattern_index == pattern.size();
}

std::vector<RegexAtom> parse_safe_regex(
    std::string_view pattern,
    const std::size_t maximum_states) {
  if (pattern.size() < 2 || pattern.front() != '/'
      || pattern.back() != '/') {
    throw std::invalid_argument{"UVM config regex must use /.../ delimiters"};
  }
  pattern.remove_prefix(1);
  pattern.remove_suffix(1);
  if (!pattern.empty() && pattern.front() == '^') pattern.remove_prefix(1);
  if (!pattern.empty() && pattern.back() == '$') pattern.remove_suffix(1);
  std::vector<RegexAtom> atoms;
  atoms.reserve(std::min(pattern.size(), maximum_states));
  bool escaped{};
  for (const char character : pattern) {
    if (escaped) {
      atoms.push_back({character, false, Quantifier::One});
      escaped = false;
    } else if (character == '\\') {
      escaped = true;
    } else if (character == '*' || character == '+' || character == '?') {
      if (atoms.empty()
          || atoms.back().quantifier != Quantifier::One) {
        throw std::invalid_argument{"invalid UVM config regex quantifier"};
      }
      atoms.back().quantifier = character == '*'
          ? Quantifier::ZeroOrMore
          : character == '+'
              ? Quantifier::OneOrMore
              : Quantifier::ZeroOrOne;
    } else if (character == '.' ) {
      atoms.push_back({{}, true, Quantifier::One});
    } else if (character == '[' || character == ']'
               || character == '(' || character == ')'
               || character == '|' || character == '{'
               || character == '}') {
      throw std::invalid_argument{
          "unsupported non-linear UVM config regex construct"};
    } else {
      atoms.push_back({character, false, Quantifier::One});
    }
    if (atoms.size() > maximum_states) {
      throw std::length_error{"UVM config regex state budget exceeded"};
    }
  }
  if (escaped) {
    throw std::invalid_argument{"dangling escape in UVM config regex"};
  }
  return atoms;
}

bool safe_regex_matches(
    const std::string_view pattern,
    const std::string_view value,
    const std::size_t maximum_states) {
  const auto atoms = parse_safe_regex(pattern, maximum_states);
  if (value.size() > maximum_states
      || atoms.size() > maximum_states
      || (atoms.size() + 1U) > maximum_states / (value.size() + 1U)) {
    throw std::length_error{"UVM config regex work budget exceeded"};
  }
  std::vector<bool> previous(value.size() + 1U);
  std::vector<bool> current(value.size() + 1U);
  previous[0] = true;
  for (const auto& atom : atoms) {
    std::fill(current.begin(), current.end(), false);
    const auto matches = [&](const std::size_t index) {
      return atom.any || atom.value == value[index];
    };
    switch (atom.quantifier) {
      case Quantifier::One:
        for (std::size_t index{1}; index <= value.size(); ++index) {
          current[index] = previous[index - 1U] && matches(index - 1U);
        }
        break;
      case Quantifier::ZeroOrOne:
        current[0] = previous[0];
        for (std::size_t index{1}; index <= value.size(); ++index) {
          current[index] = previous[index]
              || (previous[index - 1U] && matches(index - 1U));
        }
        break;
      case Quantifier::ZeroOrMore:
        current[0] = previous[0];
        for (std::size_t index{1}; index <= value.size(); ++index) {
          current[index] = previous[index]
              || (current[index - 1U] && matches(index - 1U));
        }
        break;
      case Quantifier::OneOrMore:
        for (std::size_t index{1}; index <= value.size(); ++index) {
          current[index] = (previous[index - 1U]
              || current[index - 1U]) && matches(index - 1U);
        }
        break;
    }
    previous.swap(current);
  }
  return previous.back();
}

std::string regex_escape(const std::string_view value) {
  std::string result;
  result.reserve(value.size() * 2U);
  for (const char character : value) {
    if (character == '.' || character == '*' || character == '+'
        || character == '?' || character == '^' || character == '$'
        || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  return result;
}

}  // namespace

SystemVerilogUvmConfigDbService::SystemVerilogUvmConfigDbService(
    SystemVerilogUvmResourcePoolService& resources,
    SystemVerilogUvmConfigDbLimits limits)
    : resources_(&resources), limits_(limits) {
  if (limits_.default_precedence < 0
      || static_cast<std::uint64_t>(limits_.default_precedence)
          < limits_.max_context_depth
      || limits_.max_report_bytes == 0 || limits_.max_trace_records == 0
      || limits_.max_trace_bytes == 0) {
    throw std::invalid_argument{
        "UVM config default precedence must cover the context-depth budget"};
  }
}

SystemVerilogUvmResourceHandle SystemVerilogUvmConfigDbService::set(
    const SystemVerilogUvmConfigContext& context,
    const std::string_view instance_name,
    const std::string_view field_name,
    SystemVerilogUvmResourceType type,
    SystemVerilogUvmResourceValue value,
    const SystemVerilogUvmConfigPhase phase) {
  validate_context(context);
  validate_pattern(instance_name, limits_.max_instance_pattern_bytes);
  validate_pattern(field_name, limits_.max_field_pattern_bytes);
  const auto instance_pattern = full_instance_name(context, instance_name);
  validate_pattern(
      instance_pattern, limits_.max_instance_pattern_bytes);
  if (type.identity.empty()
      || type.identity.size()
          > resources_->limits().max_type_identity_bytes) {
    throw std::length_error{"UVM config type identity exceeds its budget"};
  }
  validate_wake_budget(instance_pattern, field_name);
  const auto key = entry_key(
      context.full_name, instance_pattern, field_name, type.identity);
  const auto precedence = phase == SystemVerilogUvmConfigPhase::Build
      ? limits_.default_precedence
          - static_cast<std::int64_t>(context.depth)
      : limits_.default_precedence;
  auto found = entries_.find(key);
  if (found != entries_.end()) {
    if (!resources_->write(
            found->second.resource, std::move(value), context.full_name)) {
      throw std::logic_error{"UVM config resource unexpectedly became read-only"};
    }
    resources_->set_precedence(found->second.resource, precedence);
    resources_->set_priority(
        found->second.resource, SystemVerilogUvmResourcePriority::High);
    found->second.precedence = precedence;
    found->second.phase = phase;
    found->second.update_order = next_update_order_++;
    trigger_waiters(found->second);
    append_trace({
        0, SystemVerilogUvmConfigTraceAction::Set,
        context.full_name, instance_pattern, std::string{field_name},
        found->second.type_identity, found->second.resource, true});
    return found->second.resource;
  }
  if (entries_.size() >= limits_.max_entries) {
    throw std::length_error{"UVM config entry capacity exceeded"};
  }
  SystemVerilogUvmResourceDescriptor descriptor;
  descriptor.name = std::string{field_name};
  descriptor.scope_pattern = "*";
  descriptor.type = std::move(type);
  descriptor.value = std::move(value);
  descriptor.precedence = precedence;
  const auto handle = resources_->insert(std::move(descriptor));
  SystemVerilogUvmConfigEntry entry{
      handle,
      context.full_name,
      instance_pattern,
      std::string{field_name},
      resources_->snapshot(handle).type.identity,
      precedence,
      next_entry_order_++,
      next_update_order_++,
      phase};
  try {
    const auto [inserted, success] = entries_.emplace(key, std::move(entry));
    if (!success) {
      (void)resources_->erase(handle);
      throw std::logic_error{"duplicate UVM config entry key"};
    }
    resources_->set_priority(handle, SystemVerilogUvmResourcePriority::High);
    trigger_waiters(inserted->second);
  } catch (...) {
    entries_.erase(key);
    (void)resources_->erase(handle);
    throw;
  }
  append_trace({
      0, SystemVerilogUvmConfigTraceAction::Set,
      context.full_name, instance_pattern, std::string{field_name},
      entries_.at(key).type_identity, handle, true});
  return handle;
}

std::optional<SystemVerilogUvmResourceValue>
SystemVerilogUvmConfigDbService::get(
    const SystemVerilogUvmConfigContext& context,
    const std::string_view instance_name,
    const std::string_view field_name,
    const std::string_view type_identity,
    const std::string_view accessor) {
  validate_context(context);
  const auto full_name = full_instance_name(context, instance_name);
  const auto* entry = resolve(full_name, field_name, type_identity);
  if (!entry) {
    append_trace({
        0, SystemVerilogUvmConfigTraceAction::Get,
        context.full_name, full_name, std::string{field_name},
        std::string{type_identity}, 0, false});
    return std::nullopt;
  }
  auto value = resources_->read(entry->resource, accessor);
  append_trace({
      0, SystemVerilogUvmConfigTraceAction::Get,
      context.full_name, full_name, std::string{field_name},
      std::string{type_identity}, entry->resource, true});
  return value;
}

bool SystemVerilogUvmConfigDbService::exists(
    const SystemVerilogUvmConfigContext& context,
    const std::string_view instance_name,
    const std::string_view field_name,
    const std::string_view type_identity,
    const bool spell_check,
    std::vector<std::string>* spelling) const {
  validate_context(context);
  const auto full_name = full_instance_name(context, instance_name);
  const auto result = resolve(full_name, field_name, type_identity) != nullptr;
  if (!result && spell_check && spelling) {
    *spelling = resources_->spell_check(
        field_name, resources_->limits().max_spell_distance);
  }
  const auto* entry = result
      ? resolve(full_name, field_name, type_identity) : nullptr;
  append_trace({
      0, SystemVerilogUvmConfigTraceAction::Exists,
      context.full_name, full_name, std::string{field_name},
      std::string{type_identity}, entry ? entry->resource : 0, result});
  return result;
}

SystemVerilogUvmConfigWaiterToken
SystemVerilogUvmConfigDbService::wait_modified(
    const SystemVerilogUvmConfigContext& context,
    const std::string_view instance_name,
    const std::string_view field_name,
    WaiterCallback callback) {
  validate_context(context);
  if (!callback) {
    throw std::invalid_argument{"UVM config waiter callback must be callable"};
  }
  if (waiters_.size() >= limits_.max_waiters) {
    throw std::length_error{"UVM config waiter capacity exceeded"};
  }
  const auto full_name = full_instance_name(context, instance_name);
  validate_pattern(full_name, limits_.max_instance_pattern_bytes);
  validate_pattern(field_name, limits_.max_field_pattern_bytes);
  if (next_waiter_token_ == 0
      || next_waiter_token_ == std::numeric_limits<
          SystemVerilogUvmConfigWaiterToken>::max()) {
    throw std::overflow_error{"UVM config waiter tokens exhausted"};
  }
  const auto token = next_waiter_token_++;
  waiters_.emplace(token, Waiter{
      token, full_name, std::string{field_name},
      next_waiter_order_++, std::move(callback)});
  return token;
}

bool SystemVerilogUvmConfigDbService::cancel_waiter(
    const SystemVerilogUvmConfigWaiterToken token) noexcept {
  return waiters_.erase(token) != 0;
}

std::vector<SystemVerilogUvmConfigEntry>
SystemVerilogUvmConfigDbService::entries() const {
  std::vector<SystemVerilogUvmConfigEntry> result;
  result.reserve(entries_.size());
  for (const auto& [key, entry] : entries_) {
    (void)key;
    result.push_back(entry);
  }
  std::ranges::sort(
      result, {}, &SystemVerilogUvmConfigEntry::creation_order);
  return result;
}

void SystemVerilogUvmConfigDbService::append_trace(
    SystemVerilogUvmConfigTraceRecord record) const {
  if (!trace_enabled_) return;
  if (trace_records_.size() == limits_.max_trace_records) {
    trace_records_.pop_front();
    ++dropped_trace_records_;
  }
  record.sequence = next_trace_sequence_++;
  trace_records_.push_back(std::move(record));
  if (trace_callback_) {
    try {
      trace_callback_(trace_records_.back());
    } catch (...) {
      ++trace_callback_failures_;
    }
  }
}

std::string SystemVerilogUvmConfigDbService::trace_text() const {
  std::string result;
  const auto append = [&](const std::string_view text) {
    if (result.size() > limits_.max_trace_bytes
        || text.size() > limits_.max_trace_bytes - result.size()) {
      throw std::length_error{"UVM config trace byte budget exceeded"};
    }
    result += text;
  };
  for (const auto& record : trace_records_) {
    std::string line{"UVM_CONFIG_DB_TRACE sequence="};
    line += std::to_string(record.sequence) + " action=";
    line += trace_action_name(record.action);
    line += " context=";
    append_quoted(line, record.context_name);
    line += " instance=";
    append_quoted(line, record.instance_name);
    line += " field=";
    append_quoted(line, record.field_name);
    line += " type=";
    append_quoted(line, record.type_identity);
    line += " resource=" + std::to_string(record.resource);
    line += record.success ? " success=yes\n" : " success=no\n";
    append(line);
  }
  return result;
}

std::string SystemVerilogUvmConfigDbService::report(const bool audit) const {
  std::string result{"UVM Config DB\n"};
  const auto append = [&](const std::string_view text) {
    if (result.size() > limits_.max_report_bytes
        || text.size() > limits_.max_report_bytes - result.size()) {
      throw std::length_error{"UVM config report byte budget exceeded"};
    }
    result += text;
  };
  std::map<SystemVerilogUvmResourceHandle, std::size_t> audit_counts;
  if (audit) {
    for (const auto& record : resources_->audit_records()) {
      ++audit_counts[record.resource];
    }
  }
  for (const auto& entry : entries()) {
    const auto resource = resources_->snapshot(entry.resource);
    std::string line{"  ["};
    line += std::to_string(entry.creation_order) + "] context=";
    append_quoted(line, entry.context_name);
    line += " instance=";
    append_quoted(line, entry.instance_pattern);
    line += " field=";
    append_quoted(line, entry.field_pattern);
    line += " type=";
    append_quoted(line, entry.type_identity);
    line += " resource=" + std::to_string(entry.resource);
    line += " precedence=" + std::to_string(entry.precedence);
    line += entry.phase == SystemVerilogUvmConfigPhase::Build
        ? " phase=build" : " phase=runtime";
    line += " reads=" + std::to_string(resource.read_count);
    line += " writes=" + std::to_string(resource.write_count);
    if (audit) {
      line += " audit=" + std::to_string(audit_counts[entry.resource]);
    }
    line.push_back('\n');
    append(line);
  }
  return result;
}

std::string SystemVerilogUvmConfigDbService::full_instance_name(
    const SystemVerilogUvmConfigContext& context,
    const std::string_view instance_name) const {
  if (instance_name.empty()) return context.full_name;
  if (context.full_name.empty()) return std::string{instance_name};
  if (instance_name.size() >= 2 && instance_name.front() == '/'
      && instance_name.back() == '/') {
    auto expression = instance_name.substr(1, instance_name.size() - 2U);
    if (!expression.empty() && expression.front() == '^') {
      expression.remove_prefix(1);
    }
    if (!expression.empty() && expression.back() == '$') {
      expression.remove_suffix(1);
    }
    return "/^" + regex_escape(context.full_name)
        + "\\." + std::string{expression} + "$/";
  }
  return context.full_name + "." + std::string{instance_name};
}

std::string SystemVerilogUvmConfigDbService::entry_key(
    const std::string_view context,
    const std::string_view instance_pattern,
    const std::string_view field_pattern,
    const std::string_view type_identity) const {
  std::string result;
  result.reserve(
      context.size() + instance_pattern.size() + field_pattern.size()
      + type_identity.size() + 3U);
  result.append(context);
  result.push_back('\x1f');
  result.append(instance_pattern);
  result.push_back('\x1f');
  result.append(field_pattern);
  result.push_back('\x1f');
  result.append(type_identity);
  return result;
}

void SystemVerilogUvmConfigDbService::validate_context(
    const SystemVerilogUvmConfigContext& context) const {
  if (context.full_name.size() > limits_.max_context_bytes) {
    throw std::length_error{"UVM config context exceeds its byte budget"};
  }
  if (context.depth > limits_.max_context_depth) {
    throw std::length_error{"UVM config context depth exceeds its budget"};
  }
  if (context.full_name.empty() && context.depth != 0) {
    throw std::invalid_argument{"UVM config root context must have depth zero"};
  }
}

void SystemVerilogUvmConfigDbService::validate_pattern(
    const std::string_view pattern,
    const std::size_t byte_limit) const {
  if (pattern.size() > byte_limit) {
    throw std::length_error{"UVM config pattern exceeds its byte budget"};
  }
  if (pattern.size() >= 2 && pattern.front() == '/'
      && pattern.back() == '/') {
    (void)parse_safe_regex(pattern, limits_.max_regex_states);
  }
}

bool SystemVerilogUvmConfigDbService::pattern_matches(
    const std::string_view pattern,
    const std::string_view value) const {
  if (pattern.size() >= 2 && pattern.front() == '/'
      && pattern.back() == '/') {
    return safe_regex_matches(pattern, value, limits_.max_regex_states);
  }
  return glob_matches(pattern, value);
}

const SystemVerilogUvmConfigEntry*
SystemVerilogUvmConfigDbService::resolve(
    const std::string_view instance_name,
    const std::string_view field_name,
    const std::string_view type_identity) const {
  validate_pattern(instance_name, limits_.max_instance_pattern_bytes);
  validate_pattern(field_name, limits_.max_field_pattern_bytes);
  const SystemVerilogUvmConfigEntry* result{};
  for (const auto& [key, entry] : entries_) {
    (void)key;
    if (entry.type_identity != type_identity
        || !pattern_matches(entry.instance_pattern, instance_name)
        || !pattern_matches(entry.field_pattern, field_name)) {
      continue;
    }
    if (!result || entry.precedence > result->precedence
        || (entry.precedence == result->precedence
            && entry.update_order > result->update_order)) {
      result = &entry;
    }
  }
  return result;
}

void SystemVerilogUvmConfigDbService::validate_wake_budget(
    const std::string_view instance_pattern,
    const std::string_view field_pattern) const {
  std::size_t matching{};
  for (const auto& [token, waiter] : waiters_) {
    (void)token;
    if (pattern_matches(instance_pattern, waiter.instance_name)
        && pattern_matches(field_pattern, waiter.field_name)
        && ++matching > limits_.max_wake_callbacks_per_set) {
      throw std::length_error{"UVM config wake callback budget exceeded"};
    }
  }
}

void SystemVerilogUvmConfigDbService::trigger_waiters(
    const SystemVerilogUvmConfigEntry& entry) {
  std::vector<Waiter> wake;
  for (const auto& [token, waiter] : waiters_) {
    (void)token;
    if (pattern_matches(entry.instance_pattern, waiter.instance_name)
        && pattern_matches(entry.field_pattern, waiter.field_name)) {
      if (wake.size() >= limits_.max_wake_callbacks_per_set) {
        throw std::length_error{"UVM config wake callback budget exceeded"};
      }
      wake.push_back(waiter);
    }
  }
  std::ranges::sort(wake, {}, &Waiter::registration_order);
  for (const auto& waiter : wake) {
    waiters_.erase(waiter.token);
  }
  for (const auto& waiter : wake) {
    try {
      waiter.callback(waiter.token, entry);
    } catch (...) {
      ++callback_failures_;
    }
  }
}

}  // namespace fsim::runtime
