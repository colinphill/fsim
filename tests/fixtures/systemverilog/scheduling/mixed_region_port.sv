// SPDX-License-Identifier: Apache-2.0
//
// SystemVerilog values reach a VHDL signal through a port only once the
// delta's SystemVerilog regions are done (fsim's cross-language rule,
// docs/cross-language-semantics.md "Time and phase lattice"). The leaf
// writes result 00 (Active), 01 after #0 (Inactive, IEEE 1800-2023
// 4.4.2.3), then 10 and 11 as nonblocking updates (NBA region, 4.4.2.4,
// 10.4.2). VHDL reads the port through region_mid's signal, whose update is
// the generic update phase of the delta (IEEE 1076-2019 14.7.5.3) after all
// of that work: the watch process sees its initial UUUU, then 0011 only,
// never the in-between 0001. At 1 ps the same writes start from 11 and end
// there: region does not change, so the watch process does not run.
timeunit 1ps / 1ps;
module mixed_region_leaf(output logic [1:0] result);
  initial begin
    result = 2'b00;
    #0 result = 2'b01;
    result <= 2'b10;
    result <= 2'b11;
    $strobe("MIX sv strobe result=%b", result);
    // The same at a later time step, from 11: the delta leaves result at
    // 11, so region keeps 0011 and the watch process does not run.
    #1 result = 2'b00;
    #0 result = 2'b01;
    result <= 2'b10;
    result <= 2'b11;
    $strobe("MIX sv strobe result=%b", result);
    // A change that stands reaches region.
    #1 result = 2'b10;
    #1 $finish;
  end
endmodule

module mixed_region_port;
  logic [3:0] seen;
  mixed_region_mid mid(.seen(seen));
endmodule
