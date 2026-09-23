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
