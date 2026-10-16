// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>

int main()
{
    fsim::test::ApplicationTestFixture fixture;
    fixture.test_class_hir_report_exceptions();
    std::cout << "class HIR report and exception tests passed\n";
    return 0;
}
