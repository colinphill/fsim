// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::diagnostic {
class Engine;
}

namespace fsim::semantic {
class CompiledDesign;
}

namespace fsim::app::application_detail {

// Match separately compiled VHDL package bodies against their declarations
// and install deferred-constant completion identities in the linked HIR.
// Source compilation may retain a declaration before its body is available;
// elaboration requires every selected package to be complete.
[[nodiscard]] bool validate_and_link_vhdl_compiled_environment(
    semantic::CompiledDesign&, diagnostic::Engine&,
    bool allow_missing_package_bodies);

} // namespace fsim::app::application_detail
