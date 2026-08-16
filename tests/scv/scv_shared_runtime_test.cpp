// SPDX-License-Identifier: Apache-2.0

#include <scv.h>

#include <cassert>

#if defined(__GNUC__) || defined(__clang__)
#define FSIM_SCV_TEST_EXPORT __attribute__((visibility("default")))
#else
#define FSIM_SCV_TEST_EXPORT
#endif

extern "C" FSIM_SCV_TEST_EXPORT int sc_main(int, char*[])
{
    static_assert(SCV_VERSION_MAJOR == 2);
    static_assert(SCV_VERSION_MINOR == 0);
    static_assert(SCV_VERSION_PATCH == 0);

    scv_bag<int> values { "values", 173 };
    values.add(42);
    const auto& const_values = values;
    assert(const_values.peekRandom() == 42);
    assert(sc_core::sc_get_curr_simcontext() != nullptr);
    return 0;
}
