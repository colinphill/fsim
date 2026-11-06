// SPDX-License-Identifier: Apache-2.0
// The C declarations `fsim compile --dpi-header` writes for exports declared
// in generate blocks (IEEE 1800-2023 35.5.4, 35.7, H.9): an export of a
// block's function is listed under its C name, once for a loop generate.
module host;
  if (1) begin : blk
    export "DPI-C" function dpi_gen_note;
    function void dpi_gen_note(input int value);
    endfunction
  end
  for (genvar i = 0; i < 2; i++) begin : lp
    export "DPI-C" task dpi_gen_wait;
    task dpi_gen_wait(input int delay);
      #(delay);
    endtask
  end
endmodule

module top;
  host h ();
endmodule
