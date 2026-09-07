// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_registration.hpp"

#include "fsim/runtime/tf_containment.hpp"
#include "fsim/runtime/tf_plugin.hpp"

#include <cstddef>
#include <new>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {
namespace {

[[nodiscard]] const fsim_tf_registration_v3* registration_at(
    const fsim_tf_registration_table_v3& table,
    const std::uint32_t index) noexcept {
  const auto* const bytes =
      static_cast<const std::byte*>(table.entries);
  return reinterpret_cast<const fsim_tf_registration_v3*>(
      bytes + static_cast<std::size_t>(index) * table.entry_stride);
}

[[nodiscard]] bool valid_registration_name(
    const PLI_BYTE8* const name, const std::uint32_t size) noexcept {
  if (name == nullptr || size < 2 ||
      size > FSIM_TF_MAX_REGISTRATION_NAME_SIZE ||
      validate_tf_native_pointer(
          name, size, TfNativePointerAccess::Read) !=
          TfContainmentError::None ||
      name[0] != '$') {
    return false;
  }
  const auto first = static_cast<unsigned char>(name[1]);
  const bool first_alpha = (first >= 'A' && first <= 'Z') ||
                           (first >= 'a' && first <= 'z');
  if (!first_alpha && first != '_') {
    return false;
  }
  for (std::uint32_t index = 2; index < size; ++index) {
    const auto byte = static_cast<unsigned char>(name[index]);
    const bool alpha = (byte >= 'A' && byte <= 'Z') ||
                       (byte >= 'a' && byte <= 'z');
    const bool digit = byte >= '0' && byte <= '9';
    if (!alpha && !digit && byte != '_' && byte != '$') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool valid_callback_addresses(
    const fsim_tf_registration_v3& registration) noexcept {
  const auto valid = [](const auto callback) {
    return callback == nullptr ||
           validate_tf_callback_pointer(callback) == TfContainmentError::None;
  };
  return valid(registration.checktf) && valid(registration.sizetf) &&
         valid(registration.calltf) && valid(registration.misctf);
}

[[nodiscard]] bool valid_callbacks(
    const fsim_tf_registration_v3& registration) noexcept {
  if (registration.calltf == nullptr) {
    return false;
  }
  switch (registration.kind) {
    case FSIM_TF_REGISTRATION_TASK:
    case FSIM_TF_REGISTRATION_REAL_FUNCTION:
      return registration.sizetf == nullptr;
    case FSIM_TF_REGISTRATION_FUNCTION:
      return registration.sizetf != nullptr;
    default:
      return false;
  }
}

[[nodiscard]] TfRegistrationResult failure(
    const TfRegistrationError error, const std::uint32_t index) noexcept {
  return {.value = {}, .error = error, .entry_index = index};
}

}  // namespace

TfRegistrationResult validate_and_copy_tf_registrations(
    const fsim_tf_registration_table_v3& table) noexcept {
  if (validate_tf_registration_table(table) !=
      TfRegistrationTableError::None) {
    return failure(TfRegistrationError::Table, kNoTfRegistrationIndex);
  }

  try {
    std::vector<TfRegistration> registrations;
    registrations.reserve(table.entry_count);
    std::unordered_set<std::string_view> names;
    names.reserve(table.entry_count);

    for (std::uint32_t index = 0; index < table.entry_count; ++index) {
      const auto& registration = *registration_at(table, index);
      if (registration.struct_size < sizeof(fsim_tf_registration_v3) ||
          registration.struct_size > table.entry_stride) {
        return failure(TfRegistrationError::StructSize, index);
      }
      if (registration.kind < FSIM_TF_REGISTRATION_TASK ||
          registration.kind > FSIM_TF_REGISTRATION_REAL_FUNCTION) {
        return failure(TfRegistrationError::Kind, index);
      }
      if (registration.flags != 0 || registration.reserved != 0) {
        return failure(TfRegistrationError::Flags, index);
      }
      if (!valid_callbacks(registration)) {
        return failure(TfRegistrationError::Callbacks, index);
      }
      if (!valid_callback_addresses(registration)) {
        return failure(TfRegistrationError::Pointer, index);
      }
      if (registration.name != nullptr && registration.name_size != 0 &&
          validate_tf_native_pointer(
              registration.name, registration.name_size,
              TfNativePointerAccess::Read) != TfContainmentError::None) {
        return failure(TfRegistrationError::Pointer, index);
      }
      if (!valid_registration_name(registration.name,
                                   registration.name_size)) {
        return failure(TfRegistrationError::Name, index);
      }
      const std::string_view name{registration.name, registration.name_size};
      if (!names.insert(name).second) {
        return failure(TfRegistrationError::DuplicateName, index);
      }

      registrations.push_back(TfRegistration{
          .kind = static_cast<TfRegistrationKind>(registration.kind),
          .user_data = registration.user_data,
          .checktf = registration.checktf,
          .sizetf = registration.sizetf,
          .calltf = registration.calltf,
          .misctf = registration.misctf,
          .name = std::string{name},
      });
    }
    return {.value = std::move(registrations),
            .error = TfRegistrationError::None,
            .entry_index = kNoTfRegistrationIndex};
  } catch (const std::bad_alloc&) {
    return failure(TfRegistrationError::Allocation, kNoTfRegistrationIndex);
  } catch (...) {
    return failure(TfRegistrationError::Table, kNoTfRegistrationIndex);
  }
}

}  // namespace fsim::runtime
