// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string_view>

namespace fsim::app {
struct BuiltProject;
}

namespace fsim::tests::app {

struct UvmConformanceInventorySummary {
  std::size_t families{};
  std::size_t governed_classes{};
  std::size_t project_classes{};
};

[[nodiscard]] UvmConformanceInventorySummary
require_uvm_conformance_inventory(const fsim::app::BuiltProject &project,
                                  std::string_view release);

void require_uvm_release_closure(std::string_view release);

} // namespace fsim::tests::app
