// SPDX-License-Identifier: Apache-2.0
`include "../inc/pkg.svh"
module child;
  import shared_pkg::*;
  initial $display("shared B=%0d", VALUE);
endmodule
