// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_object.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::string_view kMalformedPolicy{"FSIM-UVM-POLICY-001"};

[[noreturn]] void malformed(std::string message) {
  throw SystemVerilogUvmObjectPolicyError{std::string{kMalformedPolicy},
                                          std::move(message)};
}

[[nodiscard]] bool
valid_recursion(const SystemVerilogUvmRecursionPolicy policy) noexcept {
  switch (policy) {
  case SystemVerilogUvmRecursionPolicy::Deep:
  case SystemVerilogUvmRecursionPolicy::Shallow:
  case SystemVerilogUvmRecursionPolicy::Reference:
    return true;
  }
  return false;
}

[[nodiscard]] bool
valid_printer(const SystemVerilogUvmPrinterKind kind) noexcept {
  switch (kind) {
  case SystemVerilogUvmPrinterKind::Line:
  case SystemVerilogUvmPrinterKind::Tree:
  case SystemVerilogUvmPrinterKind::Table:
    return true;
  }
  return false;
}

[[nodiscard]] bool safe_token(const std::string_view value) noexcept {
  return !value.empty() && value.size() <= 32 &&
         value.find_first_of("\r\n\0") == std::string_view::npos;
}

[[nodiscard]] std::string escaped(const std::string_view value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(value.size());
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      if (character >= 0x20U && character <= 0x7eU) {
        result.push_back(static_cast<char>(character));
      } else {
        result += "\\x";
        result.push_back(digits[character >> 4U]);
        result.push_back(digits[character & 0x0fU]);
      }
      break;
    }
  }
  return result;
}

[[nodiscard]] std::string handle_text(const SystemVerilogClassHandle handle) {
  return handle == 0 ? std::string{"null"} : "@" + std::to_string(handle);
}

[[nodiscard]] std::string
value_text(const SystemVerilogClassPropertyValue &value) {
  if (value.packed.width() != 0)
    return value.packed.to_msb_string();
  if (value.kind == SystemVerilogClassPropertyKind::String) {
    return value.string;
  }
  if (value.kind == SystemVerilogClassPropertyKind::ClassHandle) {
    return handle_text(value.handle);
  }
  if (value.handle_container) {
    return "size=" + std::to_string(value.handle_container->size());
  }
  return "size=" + std::to_string(value.handles.size());
}

[[nodiscard]] bool
same_shape(const SystemVerilogClassPropertyValue &left,
           const SystemVerilogClassPropertyValue &right) noexcept {
  if (left.kind != right.kind || left.packed.width() != right.packed.width() ||
      left.handle_container.has_value() != right.handle_container.has_value()) {
    return false;
  }
  return !left.handle_container ||
         left.handle_container->kind() == right.handle_container->kind();
}

[[nodiscard]] SystemVerilogUvmRecursionPolicy
field_recursion(const SystemVerilogUvmFieldDescriptor &field,
                const SystemVerilogUvmRecursionPolicy fallback) noexcept {
  if (has_flag(field.flags, SystemVerilogUvmFieldFlag::Reference)) {
    return SystemVerilogUvmRecursionPolicy::Reference;
  }
  if (has_flag(field.flags, SystemVerilogUvmFieldFlag::Shallow)) {
    return SystemVerilogUvmRecursionPolicy::Shallow;
  }
  if (has_flag(field.flags, SystemVerilogUvmFieldFlag::Deep)) {
    return SystemVerilogUvmRecursionPolicy::Deep;
  }
  return fallback;
}

[[nodiscard]] bool
field_enabled(const SystemVerilogUvmFieldDescriptor &field,
              const SystemVerilogUvmComparerPolicy &policy) noexcept {
  if (has_flag(field.flags, SystemVerilogUvmFieldFlag::NoCompare)) {
    return false;
  }
  const auto abstract =
      has_flag(field.flags, SystemVerilogUvmFieldFlag::Abstract);
  const auto physical =
      has_flag(field.flags, SystemVerilogUvmFieldFlag::Physical) || !abstract;
  return (physical && policy.compare_physical) ||
         (abstract && policy.compare_abstract);
}

[[nodiscard]] std::string entry_type(const SystemVerilogUvmObjectEntry &entry) {
  if (!entry.type_name.empty())
    return escaped(entry.type_name);
  switch (entry.kind) {
  case SystemVerilogUvmObjectEntryKind::Object:
    return "object";
  case SystemVerilogUvmObjectEntryKind::Packed:
    return "integral";
  case SystemVerilogUvmObjectEntryKind::String:
    return "string";
  case SystemVerilogUvmObjectEntryKind::NullHandle:
    return "object";
  case SystemVerilogUvmObjectEntryKind::Reference:
    return "reference";
  case SystemVerilogUvmObjectEntryKind::Cycle:
    return "cycle";
  case SystemVerilogUvmObjectEntryKind::Container:
    return "array";
  case SystemVerilogUvmObjectEntryKind::Custom:
    return "custom";
  }
  return "unknown";
}

[[nodiscard]] std::string fit_column(std::string value,
                                     const std::size_t width) {
  if (value.size() > width) {
    if (width <= 3)
      return value.substr(0, width);
    value.resize(width - 3);
    value += "...";
  } else {
    value.append(width - value.size(), ' ');
  }
  return value;
}

} // namespace

SystemVerilogUvmObjectPolicyError::SystemVerilogUvmObjectPolicyError(
    std::string code, std::string message)
    : std::invalid_argument(std::move(message)),
      diagnostic_code_(std::move(code)) {}

SystemVerilogUvmComparisonResult
SystemVerilogUvmObjectService::compare_detailed(
    const SystemVerilogClassHandle left, const SystemVerilogClassHandle right,
    const SystemVerilogUvmComparerPolicy policy) {
  if (!valid_recursion(policy.recursion)) {
    malformed("unknown UVM comparer recursion policy");
  }
  if (policy.show_max > limits_.maximum_fields) {
    malformed("UVM comparer show_max exceeds the field ceiling");
  }

  SystemVerilogUvmComparisonResult result;
  std::map<SystemVerilogClassHandle, SystemVerilogClassHandle> left_to_right;
  std::map<SystemVerilogClassHandle, SystemVerilogClassHandle> right_to_left;
  std::size_t mismatch_bytes{};
  auto add_mismatch = [&](std::string path, std::string left_value,
                          std::string right_value,
                          const SystemVerilogUvmMismatchKind kind) {
    if (result.mismatch_count == std::numeric_limits<std::size_t>::max()) {
      throw std::length_error{"UVM compare mismatch count overflow"};
    }
    ++result.mismatch_count;
    if (result.mismatches.size() >= policy.show_max)
      return;
    const auto added = path.size() + left_value.size() + right_value.size();
    if (added > limits_.maximum_output_bytes - mismatch_bytes) {
      throw std::length_error{"UVM compare mismatch output budget exceeded"};
    }
    mismatch_bytes += added;
    result.mismatches.push_back(
        {std::move(path), std::move(left_value), std::move(right_value), kind});
  };
  const auto account_field = [&] {
    if (++result.compared_fields > limits_.maximum_fields) {
      throw std::length_error{"UVM compare field budget exceeded"};
    }
  };

  std::function<void(SystemVerilogClassHandle, SystemVerilogClassHandle,
                     std::string, std::size_t, SystemVerilogUvmRecursionPolicy)>
      compare_object;
  std::function<void(SystemVerilogClassHandle, SystemVerilogClassHandle,
                     std::string, std::size_t, SystemVerilogUvmRecursionPolicy)>
      compare_handle;

  compare_handle = [&](const SystemVerilogClassHandle lhs,
                       const SystemVerilogClassHandle rhs, std::string path,
                       const std::size_t depth,
                       const SystemVerilogUvmRecursionPolicy recursion) {
    if (recursion == SystemVerilogUvmRecursionPolicy::Reference) {
      if (lhs != rhs) {
        add_mismatch(std::move(path), handle_text(lhs), handle_text(rhs),
                     SystemVerilogUvmMismatchKind::Handle);
      }
      return;
    }
    compare_object(lhs, rhs, std::move(path), depth,
                   recursion == SystemVerilogUvmRecursionPolicy::Shallow
                       ? SystemVerilogUvmRecursionPolicy::Reference
                       : SystemVerilogUvmRecursionPolicy::Deep);
  };

  compare_object = [&](const SystemVerilogClassHandle lhs,
                       const SystemVerilogClassHandle rhs, std::string path,
                       const std::size_t depth,
                       const SystemVerilogUvmRecursionPolicy recursion) {
    if (lhs == 0 || rhs == 0) {
      if (lhs != rhs) {
        add_mismatch(std::move(path), handle_text(lhs), handle_text(rhs),
                     SystemVerilogUvmMismatchKind::Handle);
      }
      return;
    }
    if (depth >= limits_.maximum_depth) {
      throw std::length_error{"UVM compare recursion depth exceeded"};
    }
    if (const auto mapped = left_to_right.find(lhs);
        mapped != left_to_right.end()) {
      if (mapped->second != rhs) {
        add_mismatch(std::move(path), handle_text(mapped->second),
                     handle_text(rhs), SystemVerilogUvmMismatchKind::Alias);
      }
      return;
    }
    if (const auto mapped = right_to_left.find(rhs);
        mapped != right_to_left.end()) {
      add_mismatch(std::move(path), handle_text(lhs),
                   handle_text(mapped->second),
                   SystemVerilogUvmMismatchKind::Alias);
      return;
    }
    if (++result.compared_objects > limits_.maximum_objects) {
      throw std::length_error{"UVM compare object budget exceeded"};
    }
    left_to_right.emplace(lhs, rhs);
    right_to_left.emplace(rhs, lhs);

    const auto left_type = type_name(lhs);
    const auto right_type = type_name(rhs);
    if (policy.check_type && left_type != right_type) {
      add_mismatch(path, left_type, right_type,
                   SystemVerilogUvmMismatchKind::Type);
      return;
    }
    const auto &right_object = heap_->object(rhs);
    const auto &type = descriptor(lhs);
    for (const auto &field : type.fields) {
      if (!field_enabled(field, policy))
        continue;
      account_field();
      const auto field_path = path + "." + field.property;
      if (std::ranges::find(right_object.property_names, field.property) ==
          right_object.property_names.end()) {
        add_mismatch(field_path, "present", "missing",
                     SystemVerilogUvmMismatchKind::Shape);
        continue;
      }
      const auto &left_value = heap_->property(lhs, field.property);
      const auto &right_value = heap_->property(rhs, field.property);
      if (!same_shape(left_value, right_value)) {
        add_mismatch(field_path, value_text(left_value),
                     value_text(right_value),
                     SystemVerilogUvmMismatchKind::Shape);
        continue;
      }
      if (left_value.packed.width() != 0) {
        if (left_value.packed != right_value.packed) {
          add_mismatch(field_path, left_value.packed.to_msb_string(),
                       right_value.packed.to_msb_string(),
                       SystemVerilogUvmMismatchKind::Value);
        }
        continue;
      }
      if (left_value.kind == SystemVerilogClassPropertyKind::String) {
        if (left_value.string != right_value.string) {
          add_mismatch(field_path, left_value.string, right_value.string,
                       SystemVerilogUvmMismatchKind::Value);
        }
        continue;
      }
      const auto nested = field_recursion(field, recursion);
      if (left_value.kind == SystemVerilogClassPropertyKind::ClassHandle) {
        compare_handle(left_value.handle, right_value.handle, field_path,
                       depth + 1U, nested);
        continue;
      }
      if (!left_value.handles.empty() || !right_value.handles.empty()) {
        if (left_value.handles.size() != right_value.handles.size()) {
          add_mismatch(field_path, std::to_string(left_value.handles.size()),
                       std::to_string(right_value.handles.size()),
                       SystemVerilogUvmMismatchKind::Size);
        }
        const auto common =
            std::min(left_value.handles.size(), right_value.handles.size());
        for (std::size_t index = 0; index < common; ++index) {
          account_field();
          compare_handle(left_value.handles[index], right_value.handles[index],
                         field_path + "[" + std::to_string(index) + "]",
                         depth + 1U, nested);
        }
        continue;
      }
      if (!left_value.handle_container)
        continue;
      const auto &left_container = *left_value.handle_container;
      const auto &right_container = *right_value.handle_container;
      if (left_container.size() != right_container.size()) {
        add_mismatch(field_path, std::to_string(left_container.size()),
                     std::to_string(right_container.size()),
                     SystemVerilogUvmMismatchKind::Size);
      }
      const auto left_sequential = left_container.sequential_values();
      const auto right_sequential = right_container.sequential_values();
      const auto common =
          std::min(left_sequential.size(), right_sequential.size());
      for (std::size_t index = 0; index < common; ++index) {
        account_field();
        compare_handle(left_sequential[index], right_sequential[index],
                       field_path + "[" + std::to_string(index) + "]",
                       depth + 1U, nested);
      }
      const auto &left_keyed = left_container.keyed_values();
      const auto &right_keyed = right_container.keyed_values();
      for (const auto &[key, left_handle] : left_keyed) {
        account_field();
        const auto found = right_keyed.find(key);
        const auto keyed_path = field_path + "[\"" + escaped(key) + "\"]";
        if (found == right_keyed.end()) {
          add_mismatch(keyed_path, handle_text(left_handle), "missing",
                       SystemVerilogUvmMismatchKind::Size);
          continue;
        }
        compare_handle(left_handle, found->second, keyed_path, depth + 1U,
                       nested);
      }
      for (const auto &[key, right_handle] : right_keyed) {
        if (left_keyed.contains(key))
          continue;
        account_field();
        add_mismatch(field_path + "[\"" + escaped(key) + "\"]", "missing",
                     handle_text(right_handle),
                     SystemVerilogUvmMismatchKind::Size);
      }
    }
    if (type.do_compare && !type.do_compare(lhs, rhs)) {
      add_mismatch(path + ".$do_compare", "true", "false",
                   SystemVerilogUvmMismatchKind::Hook);
    }
  };

  auto root = left == 0 ? std::string{"<object>"} : full_name(left);
  if (root.empty())
    root = "<object>";
  compare_object(left, right, std::move(root), 0, policy.recursion);
  return result;
}

SystemVerilogUvmPrintResult SystemVerilogUvmObjectService::print_formatted(
    const SystemVerilogClassHandle object,
    const SystemVerilogUvmPrinterPolicy policy) {
  if (!valid_printer(policy.kind)) {
    malformed("unknown UVM printer policy");
  }
  if (!safe_token(policy.indentation)) {
    malformed("UVM printer indentation is empty, too long, or multiline");
  }
  if (!safe_token(policy.separator)) {
    malformed("UVM printer separator is empty, too long, or multiline");
  }
  const std::array widths{policy.name_width, policy.type_width,
                          policy.size_width, policy.value_width};
  for (const auto width : widths) {
    if (width < 4 || width > 4096) {
      malformed("UVM printer table column width is outside [4, 4096]");
    }
  }

  SystemVerilogUvmPrintResult result;
  result.entries = print(object);
  const auto append = [&](const std::string_view text) {
    if (text.size() > limits_.maximum_output_bytes - result.text.size()) {
      throw std::length_error{"UVM formatted print output budget exceeded"};
    }
    result.text.append(text);
  };
  const auto append_row = [&](std::string name, std::string type,
                              std::string size, std::string value) {
    append(fit_column(std::move(name), policy.name_width));
    append(policy.separator);
    if (policy.emit_type_names) {
      append(fit_column(std::move(type), policy.type_width));
      append(policy.separator);
    }
    if (policy.emit_sizes) {
      append(fit_column(std::move(size), policy.size_width));
      append(policy.separator);
    }
    append(fit_column(std::move(value), policy.value_width));
    append("\n");
  };

  if (policy.kind == SystemVerilogUvmPrinterKind::Table) {
    append_row("Name", "Type", "Size", "Value");
  }
  for (const auto &entry : result.entries) {
    const auto name = escaped(entry.path);
    const auto type = entry_type(entry);
    const auto size = std::to_string(entry.size);
    const auto value = escaped(entry.value);
    if (policy.kind == SystemVerilogUvmPrinterKind::Table) {
      append_row(name, type, size, value);
      continue;
    }
    if (policy.kind == SystemVerilogUvmPrinterKind::Tree) {
      if (entry.depth > limits_.maximum_output_bytes ||
          policy.indentation.size() >
              limits_.maximum_output_bytes /
                  std::max<std::size_t>(entry.depth, 1U)) {
        throw std::length_error{"UVM tree indentation budget exceeded"};
      }
      for (std::size_t depth = 0; depth < entry.depth; ++depth) {
        append(policy.indentation);
      }
      append(name);
      if (policy.emit_type_names) {
        append(" (");
        append(type);
        append(")");
      }
      if (policy.emit_sizes) {
        append(" [");
        append(size);
        append("]");
      }
      append(" = ");
      append(value);
      append("\n");
      continue;
    }
    append(name);
    if (policy.emit_type_names) {
      append(policy.separator);
      append(type);
    }
    if (policy.emit_sizes) {
      append(policy.separator);
      append(size);
    }
    append(policy.separator);
    append(value);
    append("\n");
  }
  return result;
}

} // namespace fsim::runtime
