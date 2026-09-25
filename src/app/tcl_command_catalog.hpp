// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "tcl_internal.hpp"

#include <span>
#include <string_view>

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

enum class TclCommandCapability {
    workspace,
    loaded_design,
    simulation,
    debugger,
};

enum class TclCompletionDomain {
    literal,
    source_path,
    directory_path,
    library,
    snapshot,
    compiled_definition,
    design_object,
    debugger_entity,
};

enum class TclResultShape {
    scalar,
    list,
    dictionary,
    reference,
    variant,
};

struct TclCommandArgument {
    std::string_view name;
    TclCompletionDomain completion { TclCompletionDomain::literal };
    bool optional { false };
    bool repeatable { false };
    std::span<const std::string_view> choices { };
};

struct TclSubcommandSpec {
    std::string_view name;
    std::string_view usage;
    std::string_view help;
    std::span<const TclCommandArgument> arguments;
    TclCommandCapability capability { TclCommandCapability::workspace };
};

using TclCommandHandler = int (*)(
    TclContext&, Tcl_Interp*, Tcl_Size, Tcl_Obj* const[]);

struct TclCommandSpec {
    std::string_view name;
    std::string_view usage;
    std::string_view help;
    TclCommandCapability capability { TclCommandCapability::workspace };
    bool callback_safe { false };
    std::span<const TclCommandArgument> arguments;
    TclCommandHandler handler { nullptr };
    TclResultShape result_shape { TclResultShape::variant };
    std::string_view diagnostic_domain { };
    std::span<const TclSubcommandSpec> subcommands { };
};

// Each module owns its command implementation and metadata. The common Tcl
// session registers the combined catalog and uses it for help/completion.
std::span<const TclCommandSpec> workspace_command_specs();
std::span<const TclCommandSpec> object_command_specs();
std::span<const TclCommandSpec> debugger_command_specs();
std::span<const TclCommandSpec> existing_command_specs();

// The canonical catalog lives in tcl_command_catalog.cpp. Callers must not
// retain its span after a process teardown, but it is stable within a session.
std::span<const TclCommandSpec> command_specs();
const TclCommandSpec* find_command_spec(std::string_view name);
// Tcl's cd command changes the workspace observed by subsequent fsim calls.
bool synchronize_workspace(TclContext&, Tcl_Interp*);
int invoke_catalog_command(
    void*, Tcl_Interp*, Tcl_Size, Tcl_Obj* const[]) noexcept;

#endif

} // namespace fsim::app::tcl_detail
