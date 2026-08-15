// SPDX-License-Identifier: Apache-2.0
#include <systemc>

SC_MODULE(MixedBridge) {
  sc_core::sc_in<sc_dt::sc_logic> source{"source"};
  sc_core::sc_out<sc_dt::sc_logic> result{"result"};

  SC_CTOR(MixedBridge) {
    SC_METHOD(invert);
    sensitive << source;
  }

  void invert() {
    const auto value = source.read();
    if (value == sc_dt::sc_logic{'0'}) {
      result.write(sc_dt::sc_logic{'1'});
    } else if (value == sc_dt::sc_logic{'1'}) {
      result.write(sc_dt::sc_logic{'0'});
    } else {
      result.write(sc_dt::sc_logic{'X'});
    }
  }
};

SC_FSIM_EXPORT_AS(MixedBridge, "mixed_bridge");
