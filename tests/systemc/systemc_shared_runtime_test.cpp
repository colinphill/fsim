// SPDX-License-Identifier: Apache-2.0

#include <sysc/kernel/sc_simcontext.h>
#include <sysc/kernel/sc_ver.h>

#include <cassert>
#include <cstring>
#include <string>

extern "C" const char* fsim_systemc_accellera_version() noexcept;
extern "C" const char* fsim_systemc_accellera_runtime_identity() noexcept;
extern "C" const void* fsim_systemc_accellera_context() noexcept;
extern "C" bool
fsim_systemc_accellera_accepts_identity(const char* candidate) noexcept;

#if defined(_WIN32)
#define FSIM_SYSTEMC_TEST_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FSIM_SYSTEMC_TEST_EXPORT __attribute__((visibility("default")))
#else
#define FSIM_SYSTEMC_TEST_EXPORT
#endif

extern "C" FSIM_SYSTEMC_TEST_EXPORT int sc_main(int, char*[])
{
    const auto* const version = fsim_systemc_accellera_version();
    const auto* const identity = fsim_systemc_accellera_runtime_identity();
    assert(version != nullptr);
    assert(identity != nullptr);
    assert(std::strstr(version, "3.0.2-Accellera") != nullptr);
    assert(std::strcmp(identity, FSIM_EXPECTED_SYSTEMC_RUNTIME_IDENTITY) == 0);
    assert(fsim_systemc_accellera_accepts_identity(identity));
    assert(fsim_systemc_accellera_accepts_identity(
        FSIM_EXPECTED_SYSTEMC_RUNTIME_IDENTITY));
    assert(!fsim_systemc_accellera_accepts_identity(nullptr));
    assert(!fsim_systemc_accellera_accepts_identity("systemc-3.0.1"));
    assert(!fsim_systemc_accellera_accepts_identity(
        "systemc-3.0.2-wrong-compiler-runtime"));

    const auto* const bridge_context = fsim_systemc_accellera_context();
    const auto* const direct_context = sc_core::sc_get_curr_simcontext();
    assert(bridge_context != nullptr);
    assert(bridge_context == direct_context);
    assert(bridge_context == fsim_systemc_accellera_context());
    assert(direct_context == sc_core::sc_get_curr_simcontext());

    const std::string identity_text { identity };
    assert(identity_text.find(
               "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4")
        != std::string::npos);
    assert(identity_text.ends_with("-cxx20"));
    return 0;
}
