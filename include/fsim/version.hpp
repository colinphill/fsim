// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

namespace fsim {

inline constexpr std::string_view version = "2026.0";
inline constexpr std::uint32_t manifest_schema_version = 1;
inline constexpr std::uint32_t native_abi_version = 2;
inline constexpr std::uint32_t runtime_abi_version = 1;
inline constexpr std::string_view production_llvm_version = "22.1.8";
// Bump when bundled HDL declarations/bodies can affect elaboration or
// generated code. It keys the precompiled standard library artifacts
// (share/fsim/vhdl/stdlib/<this>/vhdl-<edition>.fsimstd) and native objects.
inline constexpr std::string_view standard_library_cache_version =
    "vhdl-stdlib-artifacts-v1";

} // namespace fsim
