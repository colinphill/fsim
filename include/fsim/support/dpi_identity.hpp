// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

// The role of a hidden DPI import the parser generates for a unit's export
// service (IEEE 1800-2023 35.5.3, 35.9). The runtime serves these imports
// itself; no C code is loaded for them.
enum class DpiHiddenImportRole : std::uint8_t {
    // An import the user declared.
    none,
    // Serves the pending export call when the call chain is in the scope
    // the step names.
    serve,
    // Waits for an export call of the running context import, or for its C
    // code to return.
    next,
    // The foreign arm of a service loop: lets the steps of the other scopes
    // serve the pending export call.
    foreign,
    // Delivers the actuals of an export call to the export's wrapper.
    arguments,
    // Hands the result and output actuals of an export back to the C code.
    results,
    last = results,
};

// The direction of one formal of an export, as its wrapper's hidden imports
// pass it: `input` is read by the export from the C value, `output` is read
// by the C code from the export's value, `inout` is both.
enum class DpiExportFlow : std::uint8_t {
    input,
    output,
    inout,
    last = inout,
};

// The role of a callable the parser generates for a unit's export service.
enum class DpiExportCallableRole : std::uint8_t {
    // A callable of the design.
    none,
    // The wrapper that calls one export with the pending C call's actuals.
    call,
    // The loop that serves the export calls of a running context import.
    service,
    // Serves the pending export call if the call chain is in this scope.
    step,
    // Runs the step of this scope from a foreign scope's service.
    step_hook,
    // The hook a context import's call site lowers: it runs the service.
    hook,
    // The hook of a scope without exports of its own: the foreign arm only.
    hook_foreign,
    // The step of a generate block that declares exports.
    block_step,
    // The hook of a generate block that declares exports.
    block_hook,
    last = block_hook,
};

} // namespace fsim::support
