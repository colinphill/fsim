// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::diagnostic {
class Engine;
}

namespace fsim::app {
class Simulation;
}

namespace fsim::app::application_detail {

class DpiForeignService;

/// The other interface's service in a run that serves VPI and VHPI: a
/// mixed-language program (cocotb's) calls both from either one's
/// callbacks, so while one service's routines or callbacks run the other
/// answers calls too. `enter` and `leave` bracket such a call on the
/// running thread.
struct ForeignCompanion {
    std::function<void()> enter;
    std::function<void()> leave;
};

/// Run the startup routines of each VPI module (`lib[:bootstrap]`) against a
/// registration-only host and report the system tasks and functions they
/// register (IEEE 1800-2023 38.36). Elaboration lowers calls of these names
/// to foreign call boundaries.
class VpiElaborationModules {
public:
    virtual ~VpiElaborationModules() = default;
    [[nodiscard]] virtual std::span<const elaboration::UserSystemTask>
    tasks() const noexcept = 0;
    /// Run the cbUnresolvedSystf callbacks for a `$name` elaboration does
    /// not know (IEEE 1800-2023 38.36.2); true when they registered it.
    [[nodiscard]] virtual bool resolve(const std::string& name) = 0;
};

/// The modules stay loaded until the result is destroyed, so their
/// callbacks can run while elaboration resolves system calls.
[[nodiscard]] std::unique_ptr<VpiElaborationModules>
register_vpi_user_system_tasks(
    std::span<const std::string> modules,
    diagnostic::Engine& diagnostics);

/// The live VPI service of one simulation: it loads the modules, owns the
/// handles the standard routines return, and calls the registered calltf,
/// compiletf and callback routines (IEEE 1800-2023 clauses 36 to 38).
class VpiForeignService {
public:
    using PrintSink = std::function<void(std::string_view)>;

    virtual ~VpiForeignService() = default;
    VpiForeignService() = default;
    VpiForeignService(const VpiForeignService&) = delete;
    VpiForeignService& operator=(const VpiForeignService&) = delete;

    /// Report a run-time error that ends the simulation, before its end of
    /// simulation (IEEE 1800-2023 38.36.3 cbError).
    virtual void run_error(std::string_view message) noexcept
    {
        (void)message;
    }
    /// The data vpi_put_data stored during the last save, by save/restart
    /// id, for the save/restart driver to write into the saved state
    /// (IEEE 1800-2023 38.31).
    [[nodiscard]] virtual std::map<std::int32_t, std::string> saved_data()
        const
    {
        return { };
    }
    /// The data sets a restart read back; vpi_get_data returns them during
    /// the restart callbacks (IEEE 1800-2023 38.9).
    virtual void restore_data(std::map<std::int32_t, std::string> data)
    {
        (void)data;
    }
    /// Bracket a call of the other interface's callbacks, during which this
    /// service answers calls too.
    [[nodiscard]] virtual ForeignCompanion calls() = 0;
    virtual void set_companion(ForeignCompanion companion) = 0;
};

/// Load the modules into a constructed, not yet started simulation, run
/// their startup routines and each call site's compiletf, then the
/// end-of-compile callbacks. `arguments` are the simulator arguments that
/// vpi_get_vlog_info reports after the program name. Calls of DPI imports
/// go to `dpi` when set.
[[nodiscard]] std::unique_ptr<VpiForeignService> attach_vpi_modules(
    Simulation& simulation,
    std::span<const std::string> modules,
    std::size_t process_count,
    VpiForeignService::PrintSink print,
    std::span<const std::string> arguments,
    DpiForeignService* dpi,
    diagnostic::Engine& diagnostics,
    ForeignCompanion companion = {});

} // namespace fsim::app::application_detail
