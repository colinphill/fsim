// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <iostream>

int main()
{
    fsim::test::ApplicationTestFixture fixture;
    fixture.test_specialization_and_packages();
    std::cout << "application specialization tests passed\n";
    return 0;
}
