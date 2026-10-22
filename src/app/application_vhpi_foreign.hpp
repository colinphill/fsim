// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace fsim::diagnostic {
class Engine;
}

namespace fsim::app {
class Simulation;
}

namespace fsim::app::application_detail {

/// The live VHPI service of one simulation: it loads the modules, owns the
/// handles the standard routines return, and calls the registered callbacks
/// (IEEE 1076-2019 clauses 17 to 23).
class VhpiForeignService {
public:
    using PrintSink = std::function<void(std::string_view)>;

    virtual ~VhpiForeignService() = default;
    VhpiForeignService() = default;
    VhpiForeignService(const VhpiForeignService&) = delete;
    VhpiForeignService& operator=(const VhpiForeignService&) = delete;

    /// Run the end-of-tool callbacks once the simulation has ended.
    virtual void finish() = 0;
    /// Whether a VHPI program reported an error or failure (vhpi_assert)
    /// or a module could not be used.
    [[nodiscard]] virtual bool failed() const noexcept = 0;
};

/// Load the modules (`lib[:bootstrap]`) into a constructed, not yet started
/// simulation and run their registration routines (IEEE 1076-2019 20.2).
/// `arguments` are the simulator arguments the tool object reports.
[[nodiscard]] std::unique_ptr<VhpiForeignService> attach_vhpi_modules(
    Simulation& simulation,
    std::span<const std::string> modules,
    VhpiForeignService::PrintSink print,
    std::span<const std::string> arguments,
    diagnostic::Engine& diagnostics);

} // namespace fsim::app::application_detail
