// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

#define FSIM_SCV_VERSION "2.0.1"
#define FSIM_SCV_HEADER_VERSION "2.0.0-20140417"
#define FSIM_SCV_SOURCE_SHA256 \
    "7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881"
#define FSIM_SCV_PATCH_SHA256 \
    "94647538a091b0bf56790d096beb4e0be0a9c82097a973469914ec62f4d748c0"
#define FSIM_SCV_PATCHED_TREE_SHA256 \
    "3a7720ec1356eaa8e58226c58256f7c3e8e7d055dddfbf57c5f97c8564c23f42"
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
