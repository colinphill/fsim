// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/built_project.hpp"

namespace fsim::app {

bool code_coverage_enabled(const project::Config& config) noexcept
{
    return config.coverage.enabled;
}

} // namespace fsim::app
