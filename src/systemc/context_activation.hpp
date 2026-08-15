// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sysc/kernel/sc_simcontext.h>

#include <mutex>

namespace fsim::systemc::detail {

inline std::recursive_mutex systemc_context_mutex;

class ContextActivation final {
public:
    explicit ContextActivation(sc_core::sc_simcontext* context)
        : lock_ { systemc_context_mutex }
        , previous_ { sc_core::sc_curr_simcontext }
    {
        sc_core::sc_curr_simcontext = context;
    }

    ~ContextActivation()
    {
        sc_core::sc_curr_simcontext = previous_;
    }

    ContextActivation(const ContextActivation&) = delete;
    ContextActivation& operator=(const ContextActivation&) = delete;

private:
    std::unique_lock<std::recursive_mutex> lock_;
    sc_core::sc_simcontext* previous_ { };
};

} // namespace fsim::systemc::detail
