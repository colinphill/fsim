// SPDX-License-Identifier: Apache-2.0
#include "fsim/diagnostic/compiled_hir_compatibility_codes.hpp"

namespace fsim::diagnostic {
namespace {

    constexpr std::string_view kCompiledHirCompatibilityCodes[] = {
#define FSIM_COMPILED_HIR_COMPATIBILITY_CODE(code) code,
#include "compiled_hir_compatibility_codes.inc"
#undef FSIM_COMPILED_HIR_COMPATIBILITY_CODE
    };

} // namespace

std::span<const std::string_view>
compiled_hir_compatibility_codes() noexcept
{
    return { kCompiledHirCompatibilityCodes };
}

} // namespace fsim::diagnostic
