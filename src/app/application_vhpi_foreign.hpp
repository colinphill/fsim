// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_vpi_foreign.hpp"

#include <filesystem>
#include <functional>
#include <memory>
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
    /// The save/restart data sets, the highest id issued and the
    /// registrations of the restart callbacks, for the "foreign.vhpi" section
    /// (IEEE 1076-2019 20.7, 20.8).
    [[nodiscard]] virtual ForeignRunState save_run_state() const
    {
        return { };
    }
    /// Takes them back at a restart, before vhpiCbStartOfRestart runs.
    virtual void restore_run_state(ForeignRunState state)
    {
        (void)state;
    }
    /// Appends an FSIM-SAVE-0011 line for each class S member of the service
    /// (docs/run-state-classification.tsv) that is not at rest at the save
    /// point: a routine in flight, a delivery under way, a request that a safe
    /// point should have drained (#128).
    virtual void audit_rest_state(std::vector<std::string>& messages) const
    {
        (void)messages;
    }
    /// Bracket a call of the other interface's callbacks, during which this
    /// service answers calls too.
    [[nodiscard]] virtual ForeignCompanion calls() = 0;
    virtual void set_companion(ForeignCompanion companion) = 0;
};

/// Whether the design has an instance of an architecture decorated with the
/// 'FOREIGN attribute (IEEE 1076-2019 20.2.4): its model needs a VHPI program
/// that registers it, so the run needs the VHPI service.
[[nodiscard]] bool design_has_vhdl_foreign_architecture(
    const Simulation& simulation);

/// Whether the analyzed design declares a foreign VHDL subprogram (IEEE
/// 1076-2019 14.4.1, 20.2.4): a standard direct binding needs the VHPI
/// service to bind its execution function even when no module is given.
/// The answer comes from the HIR, never from a scan of compiled code.
[[nodiscard]] bool design_has_vhdl_foreign_subprogram(
    const Simulation& simulation);

/// Load the modules (`lib[:bootstrap]`) into a constructed, not yet started
/// simulation and run their registration routines, then register what the
/// tabular registry files list (IEEE 1076-2019 20.2).
/// `arguments` are the simulator arguments the tool object reports;
/// `libraries` lists, on first use, the library directories that
/// vhpiLibPhysicalNameP names.
using VhpiLibraryDirectories = std::function<
    std::vector<std::pair<std::string, std::filesystem::path>>()>;
/// One VHDL design unit the workspace library catalog lists for a library
/// (`library.sqlite3`): `kind` is the catalog's kind (`entity`,
/// `architecture`, `package`, `package_body`, `configuration`), and
/// `primary_name` the entity of an architecture or the package of a body.
struct VhpiCatalogUnit {
    std::string kind;
    std::string name;
    std::string primary_name;
};
/// The units analyzed into a library, read from the workspace catalog
/// read-only the first time a VHPI program lists the library's design units
/// (IEEE 1076-2019 19.4.5: `vhpiDesignUnits`).
using VhpiLibraryUnits
    = std::function<std::vector<VhpiCatalogUnit>(std::string_view library)>;
[[nodiscard]] std::unique_ptr<VhpiForeignService> attach_vhpi_modules(
    Simulation& simulation,
    std::span<const std::string> modules,
    std::span<const std::string> registries,
    VhpiForeignService::PrintSink print,
    std::span<const std::string> arguments,
    diagnostic::Engine& diagnostics,
    ForeignCompanion companion = {},
    VhpiLibraryDirectories libraries = {},
    VhpiLibraryUnits library_units = {});

} // namespace fsim::app::application_detail
