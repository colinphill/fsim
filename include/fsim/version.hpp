// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

namespace fsim {

inline constexpr std::string_view version = "2.0.0";
inline constexpr std::uint32_t manifest_schema_version = 1;
inline constexpr std::uint32_t native_abi_version = 1;
inline constexpr std::uint32_t runtime_abi_version = 1;
inline constexpr std::string_view production_llvm_version = "22.1.8";
// Bump when bundled HDL declarations/bodies can affect elaboration or
// generated code. The pinned IEEE sources are inventoried but not yet loaded;
// the explicit marker prevents activating one without invalidating native
// objects.
inline constexpr std::string_view standard_library_cache_version =
    "ieee-1076-2019-16a01232-vhdl-psl-wide-v2";

} // namespace fsim
