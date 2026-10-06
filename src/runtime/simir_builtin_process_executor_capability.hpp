// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::app::application_detail {
class LlvmProcessExecutor;
}

namespace fsim::runtime::simir::detail {

// A sealed RTTI marker for the in-tree executor whose cohort queries are
// known to be side-effect-free. The private constructor prevents unrelated
// ProcessExecutor implementations from attesting themselves.
class BuiltinProcessExecutorCapability {
public:
    BuiltinProcessExecutorCapability(
        const BuiltinProcessExecutorCapability&) = delete;
    BuiltinProcessExecutorCapability& operator=(
        const BuiltinProcessExecutorCapability&) = delete;
    BuiltinProcessExecutorCapability(
        BuiltinProcessExecutorCapability&&) = delete;
    BuiltinProcessExecutorCapability& operator=(
        BuiltinProcessExecutorCapability&&) = delete;

protected:
    ~BuiltinProcessExecutorCapability() = default;

private:
    BuiltinProcessExecutorCapability() = default;
    friend class fsim::app::application_detail::LlvmProcessExecutor;
};

} // namespace fsim::runtime::simir::detail
