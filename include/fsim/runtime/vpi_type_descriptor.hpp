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

}  // namespace fsim::runtime
