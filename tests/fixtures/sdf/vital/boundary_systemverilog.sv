// SPDX-License-Identifier: Apache-2.0
// FSIM-VITAL-BOUNDARY: systemverilog
interface fsim_vital_systemverilog_boundary;
  logic a;
  logic z;
  modport vhdl_side(input a, output z);
endinterface
