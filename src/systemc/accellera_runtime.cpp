// SPDX-License-Identifier: Apache-2.0

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <sysc/kernel/sc_simcontext.h>
#include <sysc/kernel/sc_ver.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <string_view>

#if defined(_WIN32)
#define FSIM_SYSTEMC_ACCELERA_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FSIM_SYSTEMC_ACCELERA_EXPORT __attribute__((visibility("default")))
#else
#define FSIM_SYSTEMC_ACCELERA_EXPORT
#endif

// On ELF, the official shared library retains the standard sc_main reference,
// so provide one process-wide fallback while fsim owns process startup. The
// Windows shared core has no such reference; exporting sc_main there can steal
// resolution from a native sc_main executable through its import library.
#if !defined(_WIN32)
extern "C" FSIM_SYSTEMC_ACCELERA_EXPORT int
sc_main(int, char*[])
{
    return 0;
}
#endif

extern "C" FSIM_SYSTEMC_ACCELERA_EXPORT const char*
fsim_systemc_accellera_version() noexcept
{
    return sc_core::sc_version();
}

extern "C" FSIM_SYSTEMC_ACCELERA_EXPORT const char*
fsim_systemc_accellera_runtime_identity() noexcept
{
    return FSIM_SYSTEMC_RUNTIME_IDENTITY;
}

extern "C" FSIM_SYSTEMC_ACCELERA_EXPORT const void*
fsim_systemc_accellera_context() noexcept
{
    return sc_core::sc_get_curr_simcontext();
}

extern "C" FSIM_SYSTEMC_ACCELERA_EXPORT bool
fsim_systemc_accellera_accepts_identity(const char* candidate) noexcept
{
    return candidate != nullptr
        && std::string_view { candidate } == FSIM_SYSTEMC_RUNTIME_IDENTITY;
}
