// SPDX-License-Identifier: Apache-2.0
// FSIM-VITAL-BOUNDARY: systemc
#include <systemc>

SC_MODULE(fsim_vital_systemc_boundary)
{
    sc_core::sc_in<sc_dt::sc_logic> a;
    sc_core::sc_out<sc_dt::sc_logic> z;
};
