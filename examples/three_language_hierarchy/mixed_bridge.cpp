// SPDX-License-Identifier: Apache-2.0
#include <systemc>

SC_MODULE(MixedBridge) {
  sc_core::sc_in<sc_dt::sc_logic> source{"source"};
  sc_core::sc_out<sc_dt::sc_logic> result{"result"};
  sc_core::sc_signal<sc_dt::sc_logic> to_vhdl{"to_vhdl"};
  fsim::systemc::hdl_instance u_vhdl{"u_vhdl"};

  SC_CTOR(MixedBridge) {
    SC_METHOD(invert_for_vhdl);
    sensitive << source;

    u_vhdl.bind_input("value", to_vhdl);
    u_vhdl.bind_output("result", result);
  }

  void invert_for_vhdl() {
    const auto value = source.read();
    if (value == sc_dt::sc_logic{'0'}) {
      to_vhdl.write(sc_dt::sc_logic{'1'});
    } else if (value == sc_dt::sc_logic{'1'}) {
      to_vhdl.write(sc_dt::sc_logic{'0'});
    } else {
      to_vhdl.write(sc_dt::sc_logic{'X'});
    }
  }
};

extern "C" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION) {
    return FSIM_SC_ABI_MISMATCH;
  }
  return fsim::systemc::register_module_factory<MixedBridge>(
      host, registrar, "mixed_bridge");
}
