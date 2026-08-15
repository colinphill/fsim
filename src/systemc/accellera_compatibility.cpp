// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/accellera.hpp"

#include <string>
#include <string_view>

#if defined(_WIN32)
#define FSIM_SYSTEMC_COMPATIBILITY_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FSIM_SYSTEMC_COMPATIBILITY_EXPORT \
    __attribute__((visibility("default")))
#else
#define FSIM_SYSTEMC_COMPATIBILITY_EXPORT
#endif

#define FSIM_SYSTEMC_STRINGIFY_INNER(value) #value
#define FSIM_SYSTEMC_STRINGIFY(value) FSIM_SYSTEMC_STRINGIFY_INNER(value)

namespace {

std::string standard_library_identity()
{
#if defined(_LIBCPP_VERSION)
    return "libc++-" FSIM_SYSTEMC_STRINGIFY(_LIBCPP_VERSION);
#elif defined(_MSVC_STL_VERSION)
    std::string result = "msvc-stl-" FSIM_SYSTEMC_STRINGIFY(_MSVC_STL_VERSION);
#if defined(_MSVC_STL_UPDATE)
    result += "-" FSIM_SYSTEMC_STRINGIFY(_MSVC_STL_UPDATE);
#endif
    return result;
#elif defined(__GLIBCXX__)
    std::string result = "libstdc++-" FSIM_SYSTEMC_STRINGIFY(__GLIBCXX__);
#if defined(_GLIBCXX_RELEASE)
    result += "-release" FSIM_SYSTEMC_STRINGIFY(_GLIBCXX_RELEASE);
#endif
    return result;
#else
    return "unknown-stdlib";
#endif
}

const std::string& compatibility_identity()
{
    static const std::string identity = std::string { fsim_systemc_accellera_runtime_identity() }
        + "|bridge=" + std::to_string(fsim::systemc::accellera_bridge_revision)
        + "|stdlib=" + standard_library_identity();
    return identity;
}

} // namespace

extern "C" FSIM_SYSTEMC_COMPATIBILITY_EXPORT const char*
fsim_systemc_accellera_compatibility_identity() noexcept
{
    return compatibility_identity().c_str();
}

extern "C" FSIM_SYSTEMC_COMPATIBILITY_EXPORT bool
fsim_systemc_accellera_accepts_compatibility_identity(
    const char* candidate) noexcept
{
    return candidate != nullptr
        && std::string_view { candidate } == compatibility_identity();
}
