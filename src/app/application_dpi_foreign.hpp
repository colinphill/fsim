// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir.hpp"

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

    /// A call site bound once: the resolved C symbol and call interface, the
    /// per-formal marshalling plan and the caller's scope, file and line
    /// (a context import's `|<instance>|<file>|<line>`), so that a call
    /// parses and looks up nothing. The service owns it for its lifetime.
    using Site = const void*;

    /// Bind the call `identity` names (as `call` takes it). Null when the
    /// service does not serve the identity as an import call or its start
    /// or finish; it throws as `call` does for an identity with no loaded
    /// C implementation.
    [[nodiscard]] virtual Site bind(std::string_view identity) = 0;

    /// Whether `site` is a context import (see `context_import`).
    [[nodiscard]] virtual bool context_site(Site site) const noexcept = 0;

    /// Call a bound site; the same as `call` with the identity it was bound
    /// from.
    [[nodiscard]] virtual runtime::PackedLogic4 call_site(Site site,
        std::vector<runtime::PackedLogic4>& actuals,
        std::vector<std::string>& strings)
        = 0;

    /// Bind a call site of a kernel image (the plan of the import call): as
    /// `bind` with the plan's identity; a site whose plan agrees with the
    /// loaded import's C signature and has native marshalling (`planned_site`)
    /// is then called with `call_plan`.
    [[nodiscard]] virtual Site bind_plan(
        const runtime::simir::DpiSitePlan& plan)
        = 0;

    /// Whether `site` calls through its plan.
    [[nodiscard]] virtual bool planned_site(Site site) const noexcept = 0;

    /// Call a planned site: the arguments are read from the frame's registers
    /// straight into the C argument buffer, and the copy-out values and a
    /// string result are written back to it; the result is returned.
    [[nodiscard]] virtual runtime::PackedLogic4 call_plan(Site site,
        const runtime::simir::DpiSitePlan& plan, runtime::simir::DpiFrame& frame)
        = 0;

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
