// SPDX-License-Identifier: Apache-2.0

#include <exception>
#include <iostream>

namespace fsim::tests::runtime {
void test_generic_update_region_allocation_failure();
}

int main()
{
    try {
        fsim::tests::runtime::test_generic_update_region_allocation_failure();
        std::cout << "generic Update allocation failure recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << "generic Update allocation failure recovery failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
