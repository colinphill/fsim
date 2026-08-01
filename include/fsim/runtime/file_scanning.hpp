// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/file_operations.hpp"
#include "fsim/runtime/packed_value.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime::simir {

struct InputScanValue {
  PackedLogic4 packed;
  std::string text;
  bool string{};
};
struct InputScanResult {
  std::int32_t assignments{};
  std::vector<std::optional<InputScanValue>> values;
};

[[nodiscard]] InputScanResult scan_formatted_input(
    const FileScan& operation,
    const std::function<std::int32_t()>& read,
    const std::function<void(std::int32_t)>& unread);
[[nodiscard]] InputScanResult scan_formatted_string(
    const FileScan& operation, std::string_view input);

}  // namespace fsim::runtime::simir
