// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv.hpp"

#include "fsim/systemc/accellera.hpp"

#include <scv.h>

#include <array>
#include <string>
#include <string_view>

#define FSIM_SCV_STRINGIFY_INNER(value) #value
#define FSIM_SCV_STRINGIFY(value) FSIM_SCV_STRINGIFY_INNER(value)

namespace {

std::string compiler_identity()
{
#if defined(__clang__)
    std::string result = "clang-" FSIM_SCV_STRINGIFY(__clang_major__) "."
        FSIM_SCV_STRINGIFY(__clang_minor__) "."
        FSIM_SCV_STRINGIFY(__clang_patchlevel__);
#if defined(_MSC_VER)
    result += "-msvc" FSIM_SCV_STRINGIFY(_MSC_VER);
#endif
    return result;
#elif defined(_MSC_VER)
    return "msvc-" FSIM_SCV_STRINGIFY(_MSC_FULL_VER);
#elif defined(__GNUC__)
    return "gcc-" FSIM_SCV_STRINGIFY(__GNUC__) "."
        FSIM_SCV_STRINGIFY(__GNUC_MINOR__) "."
        FSIM_SCV_STRINGIFY(__GNUC_PATCHLEVEL__);
#else
    return "unknown-compiler";
#endif
}

std::string standard_library_identity()
{
#if defined(_LIBCPP_VERSION)
    return "libc++-" FSIM_SCV_STRINGIFY(_LIBCPP_VERSION);
#elif defined(_MSVC_STL_VERSION)
    std::string result = "msvc-stl-" FSIM_SCV_STRINGIFY(_MSVC_STL_VERSION);
#if defined(_MSVC_STL_UPDATE)
    result += "-" FSIM_SCV_STRINGIFY(_MSVC_STL_UPDATE);
#endif
    return result;
#elif defined(__GLIBCXX__)
    std::string result = "libstdc++-" FSIM_SCV_STRINGIFY(__GLIBCXX__);
#if defined(_GLIBCXX_RELEASE)
    result += "-release" FSIM_SCV_STRINGIFY(_GLIBCXX_RELEASE);
#endif
    return result;
#else
    return "unknown-stdlib";
#endif
}

const std::string& compatibility_identity()
{
    static_assert(SCV_VERSION_MAJOR == 2);
    static_assert(SCV_VERSION_MINOR == 0);
    static_assert(SCV_VERSION_PATCH == 0);
    static const std::string identity =
        std::string { "schema=fsim-scv-compatibility-v1" }
        + "|scv=" FSIM_SCV_VERSION
        + "|scv-header=" FSIM_SCV_HEADER_VERSION
        + "|scv-source=" FSIM_SCV_SOURCE_SHA256
        + "|scv-patch=" FSIM_SCV_PATCH_SHA256
        + "|scv-tree=" FSIM_SCV_PATCHED_TREE_SHA256
        + "|systemc-runtime=" FSIM_SCV_SYSTEMC_RUNTIME_IDENTITY
        + "|systemc-source=" FSIM_SYSTEMC_ACCELERA_SOURCE_SHA256
        + "|systemc-bridge="
        + std::to_string(fsim::systemc::accellera_bridge_revision)
        + "|tlm=2.0.6.20191203"
        + "|compiler=" + compiler_identity()
        + "|stdlib=" + standard_library_identity()
        + "|adapter-abi=" + std::to_string(FSIM_SCV_ADAPTER_ABI_VERSION)
        + "|plugin-abi=" + std::to_string(FSIM_SCV_PLUGIN_ABI_VERSION)
        + "|artifact-schema="
        + std::to_string(FSIM_SCV_ARTIFACT_SCHEMA_VERSION)
        + "|cache-schema=" + std::to_string(FSIM_SCV_CACHE_SCHEMA_VERSION);
    return identity;
}

constexpr std::array<std::string_view, 16> field_names {
    "schema", "scv", "scv-header", "scv-source", "scv-patch", "scv-tree",
    "systemc-runtime", "systemc-source", "systemc-bridge", "tlm", "compiler",
    "stdlib", "adapter-abi", "plugin-abi", "artifact-schema", "cache-schema"
};

constexpr std::array<const char*, 16> mismatch_diagnostics {
    "FSIM-SCV-C001 schema mismatch",
    "FSIM-SCV-C001 SCV version mismatch",
    "FSIM-SCV-C001 SCV header version mismatch",
    "FSIM-SCV-C001 SCV source mismatch",
    "FSIM-SCV-C001 SCV patch mismatch",
    "FSIM-SCV-C001 SCV patched tree mismatch",
    "FSIM-SCV-C001 SystemC runtime mismatch",
    "FSIM-SCV-C001 SystemC source mismatch",
    "FSIM-SCV-C001 SystemC bridge mismatch",
    "FSIM-SCV-C001 TLM version mismatch",
    "FSIM-SCV-C001 compiler mismatch",
    "FSIM-SCV-C001 standard library mismatch",
    "FSIM-SCV-C001 adapter ABI mismatch",
    "FSIM-SCV-C001 plug-in ABI mismatch",
    "FSIM-SCV-C001 artifact schema mismatch",
    "FSIM-SCV-C001 cache schema mismatch"
};

std::string_view next_field(std::string_view& identity) noexcept
{
    const auto delimiter = identity.find('|');
    const auto field = identity.substr(0, delimiter);
    identity = delimiter == std::string_view::npos
        ? std::string_view { }
        : identity.substr(delimiter + 1);
    return field;
}

const char* compatibility_diagnostic(const char* candidate) noexcept
{
    if (candidate == nullptr || *candidate == '\0') {
        return "FSIM-SCV-C001 missing compatibility identity";
    }
    std::string_view expected { compatibility_identity() };
    std::string_view actual { candidate };
    for (std::size_t index = 0; index < field_names.size(); ++index) {
        const auto expected_field = next_field(expected);
        const auto actual_field = next_field(actual);
        const auto separator = actual_field.find('=');
        if (separator == std::string_view::npos
            || actual_field.substr(0, separator) != field_names[index]
            || actual_field != expected_field) {
            return mismatch_diagnostics[index];
        }
    }
    if (!expected.empty() || !actual.empty()) {
        return "FSIM-SCV-C001 trailing compatibility identity data";
    }
    return "";
}

} // namespace

extern "C" FSIM_SCV_API const char* fsim_scv_compatibility_identity() noexcept
{
    return compatibility_identity().c_str();
}

extern "C" FSIM_SCV_API bool fsim_scv_accepts_compatibility_identity(
    const char* candidate) noexcept
{
    return *compatibility_diagnostic(candidate) == '\0';
}

extern "C" FSIM_SCV_API const char* fsim_scv_compatibility_diagnostic(
    const char* candidate) noexcept
{
    return compatibility_diagnostic(candidate);
}
