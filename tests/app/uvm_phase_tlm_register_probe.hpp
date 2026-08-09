// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"

#include <string>

namespace fsim::tests::app {

[[nodiscard]] std::string exercise_exact_uvm_register_environment(
    fsim::app::Simulation &simulation,
    fsim::runtime::SystemVerilogUvmRootHandle root);

} // namespace fsim::tests::app
