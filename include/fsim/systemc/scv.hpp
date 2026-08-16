// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

#define FSIM_SCV_VERSION "2.0.1"
#define FSIM_SCV_HEADER_VERSION "2.0.0-20140417"
#define FSIM_SCV_SOURCE_SHA256 \
    "7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881"
#define FSIM_SCV_PATCH_SHA256 \
    "b5954d8b0dc9f26f4e02c2a24063e742bbdcaa094c3c34fba8f2f7b1935ef57b"
#define FSIM_SCV_PATCHED_TREE_SHA256 \
    "4dea71f4e320539aae40a12209445519193da9cd237512f79a1377a5174cb3ea"
#define FSIM_SCV_ADAPTER_ABI_VERSION 1u
#define FSIM_SCV_PLUGIN_ABI_VERSION 1u
#define FSIM_SCV_ARTIFACT_SCHEMA_VERSION 1u
#define FSIM_SCV_CACHE_SCHEMA_VERSION 1u

#if defined(_WIN32)
#if defined(scv_EXPORTS)
#define FSIM_SCV_API __declspec(dllexport)
#else
#define FSIM_SCV_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define FSIM_SCV_API __attribute__((visibility("default")))
#else
#define FSIM_SCV_API
#endif

extern "C" FSIM_SCV_API const char* fsim_scv_compatibility_identity() noexcept;
extern "C" FSIM_SCV_API bool fsim_scv_accepts_compatibility_identity(
    const char* candidate) noexcept;
extern "C" FSIM_SCV_API const char* fsim_scv_compatibility_diagnostic(
    const char* candidate) noexcept;

namespace fsim::systemc {

inline constexpr std::string_view scv_version = FSIM_SCV_VERSION;
inline constexpr std::string_view scv_header_version = FSIM_SCV_HEADER_VERSION;
inline constexpr std::string_view scv_source_sha256 = FSIM_SCV_SOURCE_SHA256;
inline constexpr std::string_view scv_patch_sha256 = FSIM_SCV_PATCH_SHA256;
inline constexpr std::string_view scv_patched_tree_sha256 =
    FSIM_SCV_PATCHED_TREE_SHA256;
inline constexpr std::uint32_t scv_adapter_abi_version =
    FSIM_SCV_ADAPTER_ABI_VERSION;
inline constexpr std::uint32_t scv_plugin_abi_version =
    FSIM_SCV_PLUGIN_ABI_VERSION;
inline constexpr std::uint32_t scv_artifact_schema_version =
    FSIM_SCV_ARTIFACT_SCHEMA_VERSION;
inline constexpr std::uint32_t scv_cache_schema_version =
    FSIM_SCV_CACHE_SCHEMA_VERSION;

} // namespace fsim::systemc
