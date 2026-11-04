// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::diagnostic {
class Engine;
}

namespace fsim::app::application_detail {

/// The DPI-C import service of one simulation: it loads the `--sv-lib`
/// libraries and calls imported functions and tasks through their C
/// signatures (IEEE 1800-2023 clause 35, Annex H).
class DpiForeignService {
public:
    virtual ~DpiForeignService() = default;
    DpiForeignService() = default;
    DpiForeignService(const DpiForeignService&) = delete;
    DpiForeignService& operator=(const DpiForeignService&) = delete;

    /// The simulation time in ticks, the exponents of the simulation time
    /// unit and of one tick, and the instance scopes of the design that
    /// svGetTime, svGetTimeUnit, svGetTimePrecision and svGetScopeFromName
    /// answer from (IEEE 1800-2023 H.9.3, H.13).
    struct Clock {
        std::function<std::uint64_t()> ticks;
        int precision { -12 };
        int unit { -9 };
        /// The hierarchical name of the module, program, interface or
        /// generate scope instance `name` designates, if there is one.
        std::function<std::optional<std::string>(std::string_view name)>
            instance;
        /// The time unit and precision exponents of the scope instance at
        /// `path` (those of the module that encloses a generate scope).
        std::function<std::optional<std::pair<int, int>>(
            std::string_view path)>
            time_scale;
    };

    /// Whether `identity` (`<linkage>#<arguments>:<result>[@c|<instance>|
    /// <file>|<line>]`) is a context import, which runs with the caller's
    /// VPI and svdpi context.
    [[nodiscard]] static bool context_import(std::string_view identity);

    virtual void set_clock(Clock clock) = 0;

    /// Runs the C code of a context import that serves DPI exports on a
    /// helper thread; the entry wraps that code in the thread's caller
    /// context (as the VPI context of the calling simulation).
    using ThreadEntry = std::function<void(const std::function<void()>&)>;
    virtual void set_thread_entry(ThreadEntry entry) = 0;

    /// Call one import. Output and inout actuals are written back in place.
    [[nodiscard]] virtual runtime::PackedLogic4 call(
        std::string_view identity,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& strings)
        = 0;
};

/// Load the DPI libraries; a library named without extension may be a
/// `.so` file. Symbols resolve in the libraries, then in the program and
/// the globally loaded VPI modules.
[[nodiscard]] std::unique_ptr<DpiForeignService> load_dpi_libraries(
    std::span<const std::string> libraries,
    diagnostic::Engine& diagnostics);

} // namespace fsim::app::application_detail
