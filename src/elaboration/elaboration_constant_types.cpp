// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

ConstantTypeInfo::ConstantTypeInfo() = default;

ConstantTypeInfo::ConstantTypeInfo(
    const frontend::ValueDomain value)
    : domain(value)
{
}

ConstantTypeInfo::ConstantTypeInfo(
    const frontend::ValueDomain value,
    const bool enumeration,
    std::string nominal)
    : domain(value)
    , vhdl_enumeration(enumeration)
    , nominal_type(std::move(nominal))
{
}

} // namespace fsim::elaboration::elaboration_detail
