// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/cli/driver.hpp"

namespace fsim::app::application_detail {

int handle_workspace_systemc_compile(
    const cli::Invocation&, const project::Config&, diagnostic::Engine&,
    std::ostream&, std::ostream&);

int handle_workspace_systemc_link(
    const cli::Invocation&, const project::Config&, diagnostic::Engine&,
    std::ostream&, std::ostream&);

} // namespace fsim::app::application_detail
