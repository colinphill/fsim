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

SC_FSIM_HDL_MODULE(TypedProxy) {
    sc_core::sc_in<sc_dt::sc_logic> scalar_input{"scalar_input"};
    sc_core::sc_out<sc_dt::sc_uint<8>> vector_output{
        "vector_output"};
    sc_core::sc_inout<sc_dt::sc_bv<4>> bus{"bus"};

    SC_CTOR(TypedProxy) {
        set_actual("WIDTH", 8);
    }
};

SC_MODULE(ProxyRoot) {
    sc_core::sc_in<sc_dt::sc_logic> source{"source"};
    sc_core::sc_out<sc_dt::sc_uint<8>> result{"result"};
    sc_core::sc_inout<sc_dt::sc_bv<4>> bus{"bus"};
    TypedProxy proxy{"proxy"};

    SC_CTOR(ProxyRoot) {
        proxy.scalar_input(source);
        proxy.vector_output(result);
        proxy.bus(bus);
    }
};

SC_MODULE(UnboundProxyRoot) {
    sc_core::sc_signal<sc_dt::sc_logic> source{"source"};
    TypedProxy proxy{"proxy"};

    SC_CTOR(UnboundProxyRoot) {
        proxy.scalar_input(source);
    }
};

SC_FSIM_HDL_MODULE(ContentProxy) {
    sc_core::sc_signal<sc_dt::sc_logic> illegal_signal{
        "illegal_signal"};

    SC_CTOR(ContentProxy) {}
};

SC_MODULE(ContentProxyRoot) {
    ContentProxy proxy{"proxy"};

    SC_CTOR(ContentProxyRoot) {}
};

SC_FSIM_HDL_MODULE(ProcessProxy) {
    SC_CTOR(ProcessProxy) {
        SC_METHOD(run);
    }

    void run() {}
};

SC_MODULE(ProcessProxyRoot) {
    ProcessProxy proxy{"proxy"};

    SC_CTOR(ProcessProxyRoot) {}
};

SC_FSIM_EXPORT_AS(ParameterizedModule, "a_parameterized");
SC_FSIM_EXPORT_AS(AliasedModule, "m_alias_one");
SC_FSIM_EXPORT_AS(AliasedModule, "m_alias_two");
SC_FSIM_EXPORT_AS(ProxyRoot, "proxy_root");
SC_FSIM_EXPORT_AS(UnboundProxyRoot, "unbound_proxy_root");
SC_FSIM_EXPORT_AS(ContentProxyRoot, "content_proxy_root");
SC_FSIM_EXPORT_AS(ProcessProxyRoot, "process_proxy_root");
