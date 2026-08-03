// SPDX-License-Identifier: Apache-2.0
#include <systemc>

SC_FSIM_HDL_MODULE(LogicStage) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> result{"result"};

  SC_CTOR(LogicStage) {}
};

SC_MODULE(MixedBridge) {
  sc_core::sc_in<sc_dt::sc_logic> source{"source"};
  sc_core::sc_out<sc_dt::sc_logic> result{"result"};
  sc_core::sc_signal<sc_dt::sc_logic> to_vhdl{"to_vhdl"};
  LogicStage u_vhdl{"u_vhdl"};

  SC_CTOR(MixedBridge) {
    SC_METHOD(invert_for_vhdl);
    sensitive << source;

    u_vhdl.value(to_vhdl);
    u_vhdl.result(result);
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

SC_FSIM_EXPORT_AS(MixedBridge, "mixed_bridge");
