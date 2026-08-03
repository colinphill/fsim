// SPDX-License-Identifier: Apache-2.0
#include <systemc>

SC_MODULE(PlainModule) {
    SC_CTOR(PlainModule) {}
};

SC_FSIM_EXPORT(PlainModule);

#if defined(FSIM_TEST_DUPLICATE_EXPORT)
SC_FSIM_EXPORT_AS(PlainModule, "m_alias_one");
#endif
