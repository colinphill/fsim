// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] PackedLogic4 resize_class_value(
    const PackedLogic4& value, std::size_t width);
[[nodiscard]] SimulationTick normalized_dynamic_wait_delay(
    const WaitFor& wait, const PackedLogic4& payload);
[[nodiscard]] bool is_class_execution_boundary(const Operation& operation);
[[nodiscard]] bool is_immediate_process_boundary(const Operation& operation);
[[nodiscard]] bool is_synchronization_boundary(const Operation& operation);
[[nodiscard]] bool is_dynamic_callable_boundary(const Operation& operation);
[[nodiscard]] double known_real(const PackedLogic4& value, const char* name);
[[nodiscard]] PackedLogic4 real_value(double value);
[[nodiscard]] bool vhdl_path_below_root(
    const std::filesystem::path& root, const std::filesystem::path& path);
[[nodiscard]] std::optional<std::filesystem::path> vhdl_directory_path(
    const std::filesystem::path& root,
    const std::filesystem::path& working,
    std::string_view text,
    std::error_code& error);
[[nodiscard]] bool vhdl_access_denied(const std::error_code& error);

} // namespace fsim::runtime::simir
