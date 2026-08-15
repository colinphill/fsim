// SPDX-License-Identifier: Apache-2.0
#include <systemc>

SC_MODULE(ParameterizedModule) {
    inline static constexpr auto fsim_factory_parameters =
        fsim::systemc::make_factory_parameters(
            fsim::systemc::factory_parameter{
                "WIDTH",
                FSIM_SC_CONSTRUCTION_POSITIVE,
                true,
                12});

    SC_CTOR(ParameterizedModule) {}
};

SC_MODULE(AliasedModule) {
    SC_CTOR(AliasedModule) {}
};

SC_FSIM_EXPORT_AS(ParameterizedModule, "a_parameterized");
SC_FSIM_EXPORT_AS(AliasedModule, "m_alias_one");
SC_FSIM_EXPORT_AS(AliasedModule, "m_alias_two");
