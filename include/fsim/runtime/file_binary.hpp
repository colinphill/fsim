// SPDX-License-Identifier: Apache-2.0
#pragma once


#include "fsim/runtime/file_operations.hpp"
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir_container_value.hpp"
#include <cstdint>
#include <functional>
#include <optional>

namespace fsim::runtime::simir {

struct FileBinaryReadResult {
  std::uint32_t bytes{};
  PackedLogic4 packed{1, Logic4::zero};
  std::optional<ContainerValue> container;
};

[[nodiscard]] FileBinaryReadResult read_binary_file(
    const FileBinaryRead& operation,
    std::optional<ContainerValue> container,
    std::optional<std::int32_t> start,
    std::optional<std::int32_t> count,
    const std::function<std::int32_t()>& read);

}  // namespace fsim::runtime::simir
