// SPDX-License-Identifier: Apache-2.0
// Multichannel descriptors shared between $fopen and the VPI mcd routines
// (IEEE 1800-2023 38.24-38.29, 21.3.1). The library opens mcd_out.txt and
// writes its descriptor into mcd; the design writes to it with $fdisplay
// between the library's own writes.
module top;
  integer mcd;
  integer fd;
  integer step;
  initial begin
    step = 0;
    #1;
    fd = $fopen("fd_out.txt", "w");
    $fdisplay(mcd, "design one");
    $fdisplay(fd, "design fd");
    step = 1;
    #1;
    $fdisplay(mcd, "design two");
    step = 2;
    #1;
    $finish;
  end
endmodule
