// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/vpi_types.hpp"

#include <cstddef>

namespace fsim::runtime {

enum class SystemVerilogVpiDescriptorError {
  None,
  InvalidKind,
  InvalidShape,
  InvalidCategory,
  InvalidWidth,
  InvalidName,
  InvalidRange,
  InvalidEnum,
  DepthLimit,
  NodeLimit,
  ElementLimit,
  BitLimit,
  ArithmeticOverflow,
};

struct SystemVerilogVpiDescriptorLayout {
  std::size_t node_count{};
  std::size_t fixed_element_count{};
  std::size_t fixed_bits{};
  std::size_t maximum_depth{};
  bool dynamic{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return node_count != 0U;
  }
};

struct SystemVerilogVpiDescriptorResult {
  SystemVerilogVpiDescriptorLayout value;
  SystemVerilogVpiDescriptorError error{
      SystemVerilogVpiDescriptorError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiDescriptorError::None;
  }
};

[[nodiscard]] SystemVerilogVpiDescriptorResult
validate_systemverilog_vpi_descriptor(
    const SystemVerilogVpiTypeDescriptor& descriptor);

// The bits of a declared type stored as one packed value: a structure
// sums its members, a union takes its widest, a packed array repeats its
// element (IEEE 1800-2023 7.2.1, 7.3.1, 7.4.1); zero for other kinds.
[[nodiscard]] std::uint64_t systemverilog_vpi_declared_bits(
    const SystemVerilogVpiTypeDescriptor& descriptor) noexcept;

}  // namespace fsim::runtime
