// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

namespace fsim::frontend {

/// Records on the declarations of `package`, the package `uvm_pkg` of a
/// source set compiled with a governed UVM release (`--uvm-release`), their
/// roles in the library (support::UvmRole): its classes and their methods,
/// and its package functions, tasks, variables and types. This is the one
/// place a library declaration is identified by its declared name (IEEE
/// 1800.2-2017, IEEE 1800.2-2020); every later stage reads the recorded
/// role. A declaration of the package without a role of its own is a
/// `member` of the library.
void record_uvm_library_roles(DesignUnit& package);

} // namespace fsim::frontend
