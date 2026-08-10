// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_object.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-COPY-001"};
constexpr std::string_view kForeignHandle{"FSIM-UVM-COPY-002"};
constexpr std::string_view kIncompatible{"FSIM-UVM-COPY-003"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-COPY-004"};
constexpr std::string_view kCallbackFailure{"FSIM-UVM-COPY-005"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmCopyError{std::string{code}, std::string{message}};
}

[[nodiscard]] bool valid_recursion(
    const SystemVerilogUvmRecursionPolicy policy) noexcept {
  switch (policy) {
  case SystemVerilogUvmRecursionPolicy::Deep:
  case SystemVerilogUvmRecursionPolicy::Shallow:
  case SystemVerilogUvmRecursionPolicy::Reference:
    return true;
  }
  return false;
}

[[nodiscard]] bool same_shape(
    const SystemVerilogClassPropertyValue& left,
    const SystemVerilogClassPropertyValue& right) noexcept {
  if (left.kind != right.kind
      || left.handle_container.has_value()
          != right.handle_container.has_value()) {
    return false;
  }
  return !left.handle_container
      || left.handle_container->kind() == right.handle_container->kind();
}

[[nodiscard]] SystemVerilogUvmRecursionPolicy field_recursion(
    const SystemVerilogUvmFieldDescriptor& field,
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

[[nodiscard]] bool field_enabled(
    const SystemVerilogUvmFieldDescriptor& field,
    const SystemVerilogUvmCopierPolicy& policy) noexcept {
  if (has_flag(field.flags, SystemVerilogUvmFieldFlag::NoCopy)) return false;
  const auto abstract =
      has_flag(field.flags, SystemVerilogUvmFieldFlag::Abstract);
  const auto physical =
      has_flag(field.flags, SystemVerilogUvmFieldFlag::Physical) || !abstract;
  return (physical && policy.copy_physical)
      || (abstract && policy.copy_abstract);
}

}  // namespace

SystemVerilogUvmCopyError::SystemVerilogUvmCopyError(
    std::string code,
    std::string message)
    : std::runtime_error{std::move(message)},
      diagnostic_code_(std::move(code)) {}

SystemVerilogClassHandle SystemVerilogUvmObjectService::clone(
    const SystemVerilogClassHandle source) {
  return clone_detailed(object_handle(source)).destination.object();
}

SystemVerilogUvmCopyResult SystemVerilogUvmObjectService::clone_detailed(
    const SystemVerilogUvmObjectHandle& source,
    const SystemVerilogUvmCopierPolicy policy) {
  if (!source.valid()) {
    fail(kInvalidHandle, "UVM clone source handle is empty or stale");
  }
  if (source.owner_ != owner_) {
    fail(kForeignHandle, "UVM clone source belongs to another object service");
  }
  if (!contains(source.object_)) {
    fail(kInvalidHandle, "UVM clone source handle is empty or stale");
  }
  SystemVerilogClassHandle result{};
  try {
    result = create(source.object_, std::string{name(source.object_)});
  } catch (const SystemVerilogUvmCopyError&) {
    throw;
  } catch (const std::length_error&) {
    fail(kResourceLimit, "UVM clone root creation exceeded a ceiling");
  } catch (const std::exception&) {
    fail(kCallbackFailure, "UVM clone root creation failed");
  } catch (...) {
    fail(kCallbackFailure, "UVM clone root creation failed");
  }
  try {
    auto copied = copy_detailed(object_handle(result), source, policy);
    ++copied.created_objects;
    return copied;
  } catch (...) {
    metadata_.erase(result);
    (void)heap_->release(result);
    throw;
  }
}

void SystemVerilogUvmObjectService::copy(
    const SystemVerilogClassHandle destination,
    const SystemVerilogClassHandle source) {
  (void)copy_detailed(object_handle(destination), object_handle(source));
}

SystemVerilogUvmCopyResult SystemVerilogUvmObjectService::copy_detailed(
    const SystemVerilogUvmObjectHandle& destination,
    const SystemVerilogUvmObjectHandle& source,
    const SystemVerilogUvmCopierPolicy policy) {
  if (!valid_recursion(policy.recursion)) {
    throw SystemVerilogUvmObjectPolicyError{
        "FSIM-UVM-POLICY-001", "unknown UVM copier recursion policy"};
  }
  if (!destination.valid() || !source.valid()) {
    fail(kInvalidHandle, "UVM copy handle is empty or stale");
  }
  if (destination.owner_ != owner_ || source.owner_ != owner_) {
    fail(kForeignHandle, "UVM copy handle belongs to another object service");
  }
  if (!contains(destination.object_) || !contains(source.object_)) {
    fail(kInvalidHandle, "UVM copy handle is empty or stale");
  }
  if (heap_->object(destination.object_).specialization_identity
      != heap_->object(source.object_).specialization_identity) {
    fail(kIncompatible, "UVM copy requires matching object types");
  }

  SystemVerilogUvmCopyResult result{destination};
  if (destination.object_ == source.object_) return result;

  struct Context {
    std::map<SystemVerilogClassHandle, SystemVerilogClassHandle> mapped;
    std::map<SystemVerilogClassHandle, SystemVerilogClassObject> originals;
    std::vector<SystemVerilogClassHandle> created;
  } context;
  context.mapped.emplace(source.object_, destination.object_);

  const auto account_object = [&](const std::size_t depth) {
    if (depth >= limits_.maximum_depth) {
      fail(kResourceLimit, "UVM copy recursion depth ceiling exceeded");
    }
    if (++result.copied_objects > limits_.maximum_objects) {
      fail(kResourceLimit, "UVM copy object ceiling exceeded");
    }
  };
  const auto account_field = [&] {
    if (++result.copied_fields > limits_.maximum_fields) {
      fail(kResourceLimit, "UVM copy field ceiling exceeded");
    }
  };

  std::function<void(
      SystemVerilogClassHandle,
      SystemVerilogClassHandle,
      std::size_t,
      SystemVerilogUvmRecursionPolicy)> copy_object;
  std::function<SystemVerilogClassHandle(
      SystemVerilogClassHandle,
      std::string_view,
      std::size_t,
      SystemVerilogUvmRecursionPolicy)> copy_handle;

  copy_handle = [&](const SystemVerilogClassHandle source_handle,
                    const std::string_view child_name,
                    const std::size_t depth,
                    const SystemVerilogUvmRecursionPolicy recursion) {
    if (source_handle == 0) return SystemVerilogClassHandle{};
    if (!contains(source_handle)) {
      fail(kInvalidHandle, "UVM copy field contains an empty or stale object");
    }
    if (recursion == SystemVerilogUvmRecursionPolicy::Reference) {
      return source_handle;
    }
    if (const auto found = context.mapped.find(source_handle);
        found != context.mapped.end()) {
      return found->second;
    }
    SystemVerilogClassHandle created{};
    try {
      created = create(source_handle, std::string{child_name});
    } catch (const std::length_error&) {
      fail(kResourceLimit, "UVM recursive object creation exceeded a ceiling");
    } catch (const std::exception&) {
      fail(kCallbackFailure, "UVM recursive object creation failed");
    } catch (...) {
      fail(kCallbackFailure, "UVM recursive object creation failed");
    }
    context.created.push_back(created);
    context.mapped.emplace(source_handle, created);
    copy_object(
        created, source_handle, depth,
        recursion == SystemVerilogUvmRecursionPolicy::Shallow
            ? SystemVerilogUvmRecursionPolicy::Reference
            : SystemVerilogUvmRecursionPolicy::Deep);
    return created;
  };

  copy_object = [&](const SystemVerilogClassHandle target_handle,
                    const SystemVerilogClassHandle source_handle,
                    const std::size_t depth,
                    const SystemVerilogUvmRecursionPolicy recursion) {
    account_object(depth);
    if (heap_->object(target_handle).specialization_identity
        != heap_->object(source_handle).specialization_identity) {
      fail(kIncompatible, "UVM recursive copy encountered mismatched types");
    }
    context.originals.try_emplace(
        target_handle, heap_->object(target_handle));
    const auto& type = descriptor(source_handle);
    for (const auto& field : type.fields) {
      if (!field_enabled(field, policy)) continue;
      account_field();
      const auto source_value =
          heap_->property(source_handle, field.property);
      const auto& target_value =
          heap_->property(target_handle, field.property);
      if (!same_shape(target_value, source_value)) {
        fail(
            kIncompatible,
            "UVM copy field property shapes do not match for '" +
                field.property + "' (target kind=" +
                std::to_string(static_cast<unsigned>(target_value.kind)) +
                ", width=" + std::to_string(target_value.packed.width()) +
                ", container=" +
                std::to_string(target_value.handle_container.has_value()) +
                "; source kind=" +
                std::to_string(static_cast<unsigned>(source_value.kind)) +
                ", width=" + std::to_string(source_value.packed.width()) +
                ", container=" +
                std::to_string(source_value.handle_container.has_value()) +
                ")");
      }
      if (source_value.packed.width() != 0) {
        heap_->property(target_handle, field.property).packed =
            source_value.packed;
        continue;
      }
      if (source_value.kind == SystemVerilogClassPropertyKind::String) {
        heap_->property(target_handle, field.property).string =
            source_value.string;
        continue;
      }
      const auto nested = field_recursion(field, recursion);
      if (source_value.kind ==
          SystemVerilogClassPropertyKind::ClassHandle) {
        heap_->property(target_handle, field.property).handle = copy_handle(
            source_value.handle, field.property, depth + 1U, nested);
        continue;
      }
      if (source_value.handle_container) {
        auto copied_container = *source_value.handle_container;
        const auto sequential = source_value.handle_container->sequential_values();
        for (std::size_t index = 0; index < sequential.size(); ++index) {
          account_field();
          copied_container.set(
              *heap_, index,
              copy_handle(
                  sequential[index],
                  field.property + "[" + std::to_string(index) + "]",
                  depth + 1U, nested));
        }
        for (const auto& [key, handle] :
             source_value.handle_container->keyed_values()) {
          account_field();
          copied_container.set(
              *heap_, key,
              copy_handle(
                  handle, field.property + "[" + key + "]", depth + 1U,
                  nested));
        }
        heap_->property(target_handle, field.property).handle_container =
            std::move(copied_container);
        continue;
      }
      std::vector<SystemVerilogClassHandle> copied_handles;
      copied_handles.reserve(source_value.handles.size());
      for (std::size_t index = 0; index < source_value.handles.size(); ++index) {
        account_field();
        copied_handles.push_back(copy_handle(
            source_value.handles[index],
            field.property + "[" + std::to_string(index) + "]",
            depth + 1U, nested));
      }
      heap_->property(target_handle, field.property).handles =
          std::move(copied_handles);
    }
    if (type.do_copy) {
      try {
        type.do_copy(target_handle, source_handle);
      } catch (const std::exception&) {
        fail(kCallbackFailure, "UVM do_copy automation hook failed");
      } catch (...) {
        fail(kCallbackFailure, "UVM do_copy automation hook failed");
      }
    }
  };

  try {
    copy_object(
        destination.object_, source.object_, 0, policy.recursion);
  } catch (...) {
    for (auto& [handle, original] : context.originals) {
      if (heap_->contains(handle)) heap_->object(handle) = std::move(original);
    }
    for (auto handle = context.created.rbegin();
         handle != context.created.rend(); ++handle) {
      metadata_.erase(*handle);
      (void)heap_->release(*handle);
    }
    throw;
  }
  result.created_objects = context.created.size();
  return result;
}

}  // namespace fsim::runtime
