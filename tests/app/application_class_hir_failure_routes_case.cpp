// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>

int main()
{
    fsim::test::ApplicationTestFixture fixture;
    fixture.test_class_hir_failure_routes();
    std::cout << "class HIR failure-route tests passed\n";
    return 0;
}
