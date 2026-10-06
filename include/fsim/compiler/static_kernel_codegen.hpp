// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

namespace fsim::runtime::simir {
class StaticKernelCodegen;
}

namespace fsim::compiler {

/// LLVM native code generator for engine v4 static kernel templates.
[[nodiscard]] std::shared_ptr<runtime::simir::StaticKernelCodegen>
make_static_kernel_codegen();

} // namespace fsim::compiler
