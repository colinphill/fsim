// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {

void add_systemverilog_standard_package_identities(
    compiler::CacheKeyBuilder& key,
    const CheckedProject& checked)
{
    std::vector<std::string> identities;
    for (const auto& unit : checked.parsed.units) {
        if (!unit.systemverilog_standard_package) {
            continue;
        }
        const auto identity
            = unit.systemverilog_standard_package->revision + ":"
            + unit.systemverilog_standard_package->declaration_identity;
        if (std::ranges::find(identities, identity) == identities.end()) {
            identities.push_back(identity);
        }
    }
    std::ranges::sort(identities);
    for (const auto& identity : identities) {
        key.add("systemverilog-standard-package", identity);
    }
}

} // namespace fsim::app::application_detail
