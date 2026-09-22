// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace fsim::library {

// Non-owning relocation metadata shared by compiled-HIR object, cache, and
// mapped-library publication. Syntax trees are never part of this interface.
struct SourceNameMapping {
    std::string producer_name;
    std::string logical_name;
};

} // namespace fsim::library
