// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::tests::app {

inline constexpr std::uint64_t uvm_process_address_space_ceiling =
    6ULL * 1024ULL * 1024ULL * 1024ULL;

void install_uvm_process_address_space_ceiling();

} // namespace fsim::tests::app
