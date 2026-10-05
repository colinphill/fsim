// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace fsim::compiler::llvm_detail {

/// LLVM command-line options are process-global. Initialize them before any
/// compiler worker starts, and reject subsequent environment changes rather
/// than compiling objects with an identity that differs from their options.
[[nodiscard]] std::string_view initialize_llvm_arguments();

} // namespace fsim::compiler::llvm_detail
