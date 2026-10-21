// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/elaborator.hpp"

#include <functional>
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

/// Run the startup routines of each VPI module (`lib[:bootstrap]`) against a
/// registration-only host and report the system tasks and functions they
/// register (IEEE 1800-2023 38.36). Elaboration lowers calls of these names
/// to foreign call boundaries.
[[nodiscard]] std::optional<std::vector<elaboration::UserSystemTask>>
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
    diagnostic::Engine& diagnostics);

} // namespace fsim::app::application_detail
