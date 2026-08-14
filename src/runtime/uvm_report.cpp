// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_report.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint32_t kKnownActions =
    static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Display)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Log)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Count)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Exit)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::CallHook)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Stop)
    | static_cast<std::uint32_t>(SystemVerilogUvmReportAction::Record);

std::size_t checked_sum(
    const std::size_t left,
    const std::size_t right,
    const std::string_view message) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw std::length_error{std::string{message}};
  }
  return left + right;
}

void append_checked(
    std::string& destination,
    const std::string_view text,
    const std::size_t maximum) {
  if (text.size() > maximum - destination.size()) {
    throw std::length_error{"UVM report composed payload exceeds its budget"};
  }
  destination.append(text);
}

void append_escaped(
    std::string& destination,
    const std::string_view value,
    const std::size_t maximum) {
  constexpr std::array<char, 16> digits{
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  for (const char raw_byte : value) {
    const auto byte = static_cast<unsigned char>(raw_byte);
    switch (byte) {
    case '\\': append_checked(destination, "\\\\", maximum); break;
    case '"': append_checked(destination, "\\\"", maximum); break;
    case '\n': append_checked(destination, "\\n", maximum); break;
    case '\r': append_checked(destination, "\\r", maximum); break;
    case '\t': append_checked(destination, "\\t", maximum); break;
    default:
      if (byte < 0x20U || byte == 0x7fU) {
        const std::array escaped{
            '\\', 'x', digits[byte >> 4U], digits[byte & 0x0fU]};
        append_checked(
            destination,
            std::string_view{escaped.data(), escaped.size()},
            maximum);
      } else {
        const auto character = static_cast<char>(byte);
        append_checked(
            destination, std::string_view{&character, 1}, maximum);
      }
      break;
    }
  }
}

bool display_element(const SystemVerilogUvmReportElement& element) {
  return has_action(element.action, SystemVerilogUvmReportAction::Display)
      || has_action(element.action, SystemVerilogUvmReportAction::Log);
}

std::string decimal_bits(
    const PackedLogic4& value,
    const bool signed_value) {
  std::vector<bool> bits(value.width());
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    const auto digit = value.get(bit);
    if (digit != Logic4::zero && digit != Logic4::one) return "X";
    bits[bit] = digit == Logic4::one;
  }
  const auto negative = signed_value && !bits.empty() && bits.back();
  if (negative) {
    bool carry = true;
    for (auto&& bit : bits) {
      const auto sum = static_cast<unsigned>(!bit)
          + static_cast<unsigned>(carry);
      bit = (sum & 1U) != 0;
      carry = sum > 1U;
    }
  }
  constexpr std::uint64_t base{1'000'000'000ULL};
  std::vector<std::uint32_t> limbs{0};
  for (auto bit = bits.rbegin(); bit != bits.rend(); ++bit) {
    std::uint64_t carry = *bit ? 1U : 0U;
    for (auto& limb : limbs) {
      const auto expanded = static_cast<std::uint64_t>(limb) * 2U + carry;
      limb = static_cast<std::uint32_t>(expanded % base);
      carry = expanded / base;
    }
    if (carry != 0) limbs.push_back(static_cast<std::uint32_t>(carry));
  }
  auto result = negative ? std::string{"-"} : std::string{};
  result += std::to_string(limbs.back());
  for (auto limb = limbs.rbegin() + 1; limb != limbs.rend(); ++limb) {
    const auto group = std::to_string(*limb);
    result.append(9U - group.size(), '0');
    result += group;
  }
  return result;
}

std::string grouped_bits(
    const PackedLogic4& value,
    const std::size_t group_width) {
  constexpr std::string_view digits{"0123456789ABCDEF"};
  const auto groups = (value.width() + group_width - 1U) / group_width;
  std::string result;
  result.reserve(groups);
  for (std::size_t group = groups; group-- > 0;) {
    unsigned numeric{};
    bool unknown{};
    bool all_z{true};
    for (std::size_t offset = 0; offset < group_width; ++offset) {
      const auto bit = group * group_width + offset;
      if (bit >= value.width()) continue;
      const auto digit = value.get(bit);
      if (digit == Logic4::one) numeric |= 1U << offset;
      if (digit == Logic4::x || digit == Logic4::z) unknown = true;
      if (digit != Logic4::z) all_z = false;
    }
    result.push_back(
        unknown ? (all_z ? 'Z' : 'X') : digits[numeric]);
  }
  return result;
}

std::string format_integer(
    const SystemVerilogUvmReportElement& element) {
  const auto& value = std::get<PackedLogic4>(element.value);
  auto result = std::to_string(element.size);
  switch (element.radix) {
  case SystemVerilogUvmReportRadix::Binary:
    result += "'b" + value.to_msb_string();
    break;
  case SystemVerilogUvmReportRadix::Octal:
    result += "'o" + grouped_bits(value, 3);
    break;
  case SystemVerilogUvmReportRadix::Decimal:
    result += "'sd" + decimal_bits(value, true);
    break;
  case SystemVerilogUvmReportRadix::Unsigned:
    result += "'d" + decimal_bits(value, false);
    break;
  case SystemVerilogUvmReportRadix::Hexadecimal:
    result += "'h" + grouped_bits(value, 4);
    break;
  default:
    throw std::invalid_argument{"invalid UVM report element radix"};
  }
  return result;
}

}  // namespace

SystemVerilogUvmReportElementContainer::
SystemVerilogUvmReportElementContainer(
    SystemVerilogUvmReportElementLimits limits)
    : limits_(limits) {
  if (limits_.max_elements == 0
      || limits_.max_name_bytes == 0
      || limits_.max_string_bytes == 0
      || limits_.max_packed_width == 0
      || limits_.max_total_bytes == 0) {
    throw std::invalid_argument{
        "UVM report element limits must be nonzero"};
  }
}

void SystemVerilogUvmReportElementContainer::add_integer(
    std::string name,
    PackedLogic4 value,
    const std::size_t size,
    const SystemVerilogUvmReportRadix radix,
    const SystemVerilogUvmReportAction action) {
  if (size == 0 || size > limits_.max_packed_width
      || value.width() != size) {
    throw std::length_error{
        "UVM report integer element has an invalid width"};
  }
  switch (radix) {
  case SystemVerilogUvmReportRadix::Binary:
  case SystemVerilogUvmReportRadix::Octal:
  case SystemVerilogUvmReportRadix::Decimal:
  case SystemVerilogUvmReportRadix::Unsigned:
  case SystemVerilogUvmReportRadix::Hexadecimal:
    break;
  default:
    throw std::invalid_argument{"invalid UVM report element radix"};
  }
  append({
      SystemVerilogUvmReportElementKind::Integer,
      std::move(name),
      std::move(value),
      size,
      radix,
      action});
}

void SystemVerilogUvmReportElementContainer::add_string(
    std::string name,
    std::string value,
    const SystemVerilogUvmReportAction action) {
  if (value.size() > limits_.max_string_bytes) {
    throw std::length_error{
        "UVM report string element exceeds its byte budget"};
  }
  append({
      SystemVerilogUvmReportElementKind::String,
      std::move(name),
      std::move(value),
      0,
      SystemVerilogUvmReportRadix::Binary,
      action});
}

void SystemVerilogUvmReportElementContainer::add_object(
    std::string name,
    const SystemVerilogClassHandle object,
    const SystemVerilogUvmReportAction action) {
  append({
      SystemVerilogUvmReportElementKind::Object,
      std::move(name),
      object,
      0,
      SystemVerilogUvmReportRadix::Binary,
      action});
}

void SystemVerilogUvmReportElementContainer::erase(
    const std::size_t index) {
  if (index >= elements_.size()) {
    throw std::out_of_range{"UVM report element index is invalid"};
  }
  total_bytes_ -= storage_bytes(elements_[index]);
  elements_.erase(elements_.begin() + static_cast<std::ptrdiff_t>(index));
}

void SystemVerilogUvmReportElementContainer::clear() noexcept {
  elements_.clear();
  total_bytes_ = 0;
}

void SystemVerilogUvmReportElementContainer::append(
    SystemVerilogUvmReportElement element) {
  if (elements_.size() >= limits_.max_elements) {
    throw std::length_error{"UVM report element budget exceeded"};
  }
  if (element.name.size() > limits_.max_name_bytes) {
    throw std::length_error{
        "UVM report element name exceeds its byte budget"};
  }
  validate_action(element.action);
  const auto bytes = storage_bytes(element);
  if (bytes > limits_.max_total_bytes - total_bytes_) {
    throw std::length_error{
        "UVM report element storage budget exceeded"};
  }
  elements_.push_back(std::move(element));
  total_bytes_ += bytes;
}

std::size_t SystemVerilogUvmReportElementContainer::storage_bytes(
    const SystemVerilogUvmReportElement& element) const {
  auto result = element.name.size();
  switch (element.kind) {
  case SystemVerilogUvmReportElementKind::Integer:
    result = checked_sum(
        result, (element.size + 7U) / 8U,
        "UVM report element storage budget exceeded");
    break;
  case SystemVerilogUvmReportElementKind::String:
    result = checked_sum(
        result, std::get<std::string>(element.value).size(),
        "UVM report element storage budget exceeded");
    break;
  case SystemVerilogUvmReportElementKind::Object:
    result = checked_sum(
        result, sizeof(SystemVerilogClassHandle),
        "UVM report element storage budget exceeded");
    break;
  default:
    throw std::invalid_argument{"invalid UVM report element kind"};
  }
  return result;
}

void SystemVerilogUvmReportElementContainer::validate_action(
    const SystemVerilogUvmReportAction action) const {
  if ((static_cast<std::uint32_t>(action) & ~kKnownActions) != 0) {
    throw std::invalid_argument{"invalid UVM report element action"};
  }
}

SystemVerilogUvmReportService::SystemVerilogUvmReportService(
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmReportLimits limits)
    : objects_(&objects), limits_(limits) {
  if (limits_.max_id_bytes == 0
      || limits_.max_message_bytes == 0
      || limits_.max_filename_bytes == 0
      || limits_.max_context_bytes == 0
      || limits_.max_report_object_name_bytes == 0
      || limits_.max_composed_bytes == 0
      || limits_.max_handlers == 0
      || limits_.max_settings == 0) {
    throw std::invalid_argument{"UVM report limits must be nonzero"};
  }
  (void)SystemVerilogUvmReportElementContainer{limits_.elements};
}

SystemVerilogUvmReportService::SystemVerilogUvmReportService(
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmReportLimits limits)
    : SystemVerilogUvmReportService(objects, std::move(limits)) {
  components_ = &components;
}

bool SystemVerilogUvmReportService::enabled(
    const SystemVerilogClassHandle report_object,
    const std::int32_t verbosity,
    const SystemVerilogUvmReportSeverity severity,
    const std::string_view id) const {
  validate_object(report_object);
  validate_severity(severity);
  validate_id(id);
  return policy(report_object, severity, id).verbosity >= verbosity;
}

SystemVerilogUvmReportMessage SystemVerilogUvmReportService::construct(
    const SystemVerilogUvmReportRequest& request) const {
  validate_request(request);
  SystemVerilogUvmReportMessage result;
  result.report_object = request.report_object;
  result.report_object_name = object_name(request.report_object);
  result.severity = request.severity;
  result.id = request.id;
  result.message = request.message;
  result.verbosity = effective_verbosity(request);
  result.filename = request.filename;
  result.line = request.line;
  result.timestamp = request.timestamp;
  result.context = request.context;
  const auto resolved = policy(
      request.report_object, request.severity, request.id);
  result.severity = resolved.severity;
  result.action = resolved.action;
  result.file = resolved.file;
  result.elements = request.elements;
  return result;
}

bool SystemVerilogUvmReportService::report(
    const SystemVerilogUvmReportRequest& request) {
  validate_request(request);
  const auto verbosity = effective_verbosity(request);
  if (request.severity == SystemVerilogUvmReportSeverity::Info
      && !request.report_enabled_checked
      && !enabled(
          request.report_object, verbosity, request.severity, request.id)) {
    ++filtered_count_;
    return false;
  }
  if (next_sequence_ == 0) {
    throw std::overflow_error{"UVM report sequence overflow"};
  }
  auto message = construct(request);
  if (!run_hooks(message)) {
    ++hook_filtered_count_;
    return false;
  }
  if (!run_catchers(message)) {
    return false;
  }
  message.sequence = next_sequence_;
  const auto payload = has_action(
          message.action, SystemVerilogUvmReportAction::Display)
          || has_action(message.action, SystemVerilogUvmReportAction::Log)
      ? compose_payload(message)
      : std::string{};
  if (!server_.process(message, payload)) {
    ++action_filtered_count_;
    return false;
  }
  ++next_sequence_;
  ++routed_count_;
  if (route_hook_) {
    try {
      route_hook_(message);
    } catch (...) {
      ++route_failures_;
    }
  }
  return true;
}

std::string SystemVerilogUvmReportService::compose_payload(
    const SystemVerilogUvmReportMessage& message) const {
  validate_message(message);
  std::string result;
  const auto visible = std::count_if(
      message.elements.elements().begin(),
      message.elements.elements().end(),
      display_element);
  if (static_cast<std::size_t>(visible)
      > std::numeric_limits<std::size_t>::max() / 16U) {
    throw std::length_error{
        "UVM report composed payload exceeds its budget"};
  }
  const auto initial = checked_sum(
      message.message.size(), static_cast<std::size_t>(visible) * 16U,
      "UVM report composed payload exceeds its budget");
  result.reserve(std::min(initial, limits_.max_composed_bytes));
  append_checked(result, message.message, limits_.max_composed_bytes);
  for (const auto& element : message.elements.elements()) {
    if (!display_element(element)) continue;
    append_checked(result, "\n +", limits_.max_composed_bytes);
    append_checked(result, element.name, limits_.max_composed_bytes);
    append_checked(result, " = ", limits_.max_composed_bytes);
    switch (element.kind) {
    case SystemVerilogUvmReportElementKind::Integer: {
      append_checked(
          result, format_integer(element), limits_.max_composed_bytes);
      break;
    }
    case SystemVerilogUvmReportElementKind::String:
      append_checked(result, "\"", limits_.max_composed_bytes);
      append_escaped(
          result,
          std::get<std::string>(element.value),
          limits_.max_composed_bytes);
      append_checked(result, "\"", limits_.max_composed_bytes);
      break;
    case SystemVerilogUvmReportElementKind::Object: {
      const auto object = std::get<SystemVerilogClassHandle>(element.value);
      if (object == 0) {
        append_checked(result, "null", limits_.max_composed_bytes);
        break;
      }
      validate_object(object);
      const auto reference = "@" + std::to_string(object);
      append_checked(result, reference, limits_.max_composed_bytes);
      const auto name = object_name(object);
      if (!name.empty()) {
        append_checked(result, "{", limits_.max_composed_bytes);
        append_checked(result, name, limits_.max_composed_bytes);
        append_checked(result, "}", limits_.max_composed_bytes);
      }
      break;
    }
    default:
      throw std::invalid_argument{"invalid UVM report element kind"};
    }
  }
  return result;
}

void SystemVerilogUvmReportService::validate_request(
    const SystemVerilogUvmReportRequest& request) const {
  validate_object(request.report_object);
  validate_severity(request.severity);
  validate_id(request.id);
  if (request.message.size() > limits_.max_message_bytes) {
    throw std::length_error{"UVM report message exceeds its byte budget"};
  }
  if (request.filename.size() > limits_.max_filename_bytes) {
    throw std::length_error{"UVM report filename exceeds its byte budget"};
  }
  if (request.context.size() > limits_.max_context_bytes) {
    throw std::length_error{"UVM report context exceeds its byte budget"};
  }
  validate_elements(request.elements);
  const auto name = object_name(request.report_object);
  if (name.size() > limits_.max_report_object_name_bytes) {
    throw std::length_error{
        "UVM report object name exceeds its byte budget"};
  }
}

void SystemVerilogUvmReportService::validate_message(
    const SystemVerilogUvmReportMessage& message) const {
  validate_object(message.report_object);
  validate_severity(message.severity);
  validate_action(message.action);
  if (message.report_object_name.size()
          > limits_.max_report_object_name_bytes
      || message.id.size() > limits_.max_id_bytes
      || message.message.size() > limits_.max_message_bytes
      || message.filename.size() > limits_.max_filename_bytes
      || message.context.size() > limits_.max_context_bytes) {
    throw std::length_error{"UVM report message exceeds its field budget"};
  }
  validate_elements(message.elements);
}

void SystemVerilogUvmReportService::validate_object(
    const SystemVerilogClassHandle object) const {
  if (object != 0 && !objects_->contains(object)) {
    throw std::invalid_argument{"invalid UVM report object handle"};
  }
}

void SystemVerilogUvmReportService::validate_severity(
    const SystemVerilogUvmReportSeverity severity) const {
  switch (severity) {
  case SystemVerilogUvmReportSeverity::Info:
  case SystemVerilogUvmReportSeverity::Warning:
  case SystemVerilogUvmReportSeverity::Error:
  case SystemVerilogUvmReportSeverity::Fatal:
    return;
  default:
    throw std::invalid_argument{"invalid UVM report severity"};
  }
}

void SystemVerilogUvmReportService::validate_action(
    const SystemVerilogUvmReportAction action) const {
  if ((static_cast<std::uint32_t>(action) & ~kKnownActions) != 0) {
    throw std::invalid_argument{"invalid UVM report action"};
  }
}

void SystemVerilogUvmReportService::validate_id(
    const std::string_view id) const {
  if (id.size() > limits_.max_id_bytes) {
    throw std::length_error{"UVM report ID exceeds its byte budget"};
  }
}

void SystemVerilogUvmReportService::validate_elements(
    const SystemVerilogUvmReportElementContainer& elements) const {
  if (elements.size() > limits_.elements.max_elements
      || elements.total_bytes() > limits_.elements.max_total_bytes) {
    throw std::length_error{"UVM report element budget exceeded"};
  }
  for (const auto& element : elements.elements()) {
    if (element.name.size() > limits_.elements.max_name_bytes) {
      throw std::length_error{
          "UVM report element name exceeds its byte budget"};
    }
    const auto actions = static_cast<std::uint32_t>(element.action);
    if ((actions & ~kKnownActions) != 0) {
      throw std::invalid_argument{"invalid UVM report element action"};
    }
    switch (element.kind) {
    case SystemVerilogUvmReportElementKind::Integer:
      if (element.size == 0
          || element.size > limits_.elements.max_packed_width
          || std::get<PackedLogic4>(element.value).width() != element.size) {
        throw std::length_error{
            "UVM report integer element has an invalid width"};
      }
      break;
    case SystemVerilogUvmReportElementKind::String:
      if (std::get<std::string>(element.value).size()
          > limits_.elements.max_string_bytes) {
        throw std::length_error{
            "UVM report string element exceeds its byte budget"};
      }
      break;
    case SystemVerilogUvmReportElementKind::Object:
      validate_object(std::get<SystemVerilogClassHandle>(element.value));
      break;
    default:
      throw std::invalid_argument{"invalid UVM report element kind"};
    }
  }
}

std::int32_t SystemVerilogUvmReportService::effective_verbosity(
    const SystemVerilogUvmReportRequest& request) const noexcept {
  if (request.verbosity) return *request.verbosity;
  return request.severity == SystemVerilogUvmReportSeverity::Info
      ? 200
      : 0;
}

std::string SystemVerilogUvmReportService::object_name(
    const SystemVerilogClassHandle object) const {
  return object == 0 ? std::string{} : objects_->full_name(object);
}

}  // namespace fsim::runtime
