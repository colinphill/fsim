// SPDX-License-Identifier: Apache-2.0

#include <exception>
#include <iostream>

namespace fsim::tests::runtime {
void test_prepared_output_allocation_failure_retry();
}

int main()
{
    try {
        fsim::tests::runtime::test_prepared_output_allocation_failure_retry();
        std::cout << "prepared output allocation failure retry passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "prepared output allocation failure retry failed: "
                  << error.what() << '\n';
        return 1;
    }
}
