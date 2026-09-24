// SPDX-License-Identifier: Apache-2.0
#pragma once

// The shared SystemC launcher resolves sc_main dynamically. Without an
// exported C symbol, its fallback entry point can make a test exit green
// without executing any of the test body's assertions.
#if defined(_WIN32)
#define FSIM_TEST_SC_MAIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define FSIM_TEST_SC_MAIN_EXPORT __attribute__((visibility("default")))
#else
#define FSIM_TEST_SC_MAIN_EXPORT
#endif

// SystemC 3.0.2 declares sc_main without dllexport. On Windows Clang the
// required exported definition therefore triggers this one redeclaration
// warning even though the export is intentional.
#if (defined(_WIN32) || defined(WIN32)) && defined(__clang__)
#define FSIM_TEST_SC_MAIN_DIAGNOSTIC_PUSH \
    _Pragma("clang diagnostic push")      \
        _Pragma("clang diagnostic ignored \"-Wdll-attribute-on-redeclaration\"")
#define FSIM_TEST_SC_MAIN_DIAGNOSTIC_POP \
    _Pragma("clang diagnostic pop")
#else
#define FSIM_TEST_SC_MAIN_DIAGNOSTIC_PUSH
#define FSIM_TEST_SC_MAIN_DIAGNOSTIC_POP
#endif
