// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fsim::runtime {

enum class TfNativePointerAccess {
  Read,
  Write,
  Execute,
};

enum class TfContainmentError {
  None,
  Null,
  Empty,
  Overflow,
  Unmapped,
  Access,
  Inspection,
};

[[nodiscard]] TfContainmentError validate_tf_native_pointer(
    const void* pointer, std::size_t size,
    TfNativePointerAccess access) noexcept;

template <typename Function>
[[nodiscard]] TfContainmentError validate_tf_callback_pointer(
    Function pointer) noexcept {
  static_assert(std::is_pointer_v<Function>);
  static_assert(sizeof(Function) == sizeof(std::uintptr_t));
  if (pointer == nullptr) return TfContainmentError::Null;
  const auto address = std::bit_cast<std::uintptr_t>(pointer);
  return validate_tf_native_pointer(
      reinterpret_cast<const void*>(address), 1,
      TfNativePointerAccess::Execute);
}

}  // namespace fsim::runtime
