// SPDX-License-Identifier: Apache-2.0

#include <cassert>

#if defined(NDEBUG)
#error Release test assertions must remain enabled
#endif

int main()
{
    int assertions_executed = 0;
    assert(++assertions_executed == 1);
    return assertions_executed == 1 ? 0 : 1;
}
