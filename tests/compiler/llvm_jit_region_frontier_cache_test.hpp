// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::compiler::test {

/// Exercise persistent native-frontier object identity and rejection using
/// the production JIT factory and its generated entry point.
void run_region_frontier_cache_tests();

} // namespace fsim::compiler::test
