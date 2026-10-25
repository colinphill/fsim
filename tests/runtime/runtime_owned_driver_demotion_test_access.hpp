// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "../../src/runtime/simir_internal.hpp"

namespace fsim::runtime::simir {

/// Friend of Interpreter and its process table for tests that inspect the
/// implementation directly.
struct OwnedDriverDemotionTestAccess {
    using Implementation = Interpreter::Impl;

    static auto& implementation(Interpreter& interpreter)
    {
        return *interpreter.impl_;
    }
};

} // namespace fsim::runtime::simir
