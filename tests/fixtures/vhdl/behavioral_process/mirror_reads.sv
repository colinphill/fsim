// SPDX-License-Identifier: Apache-2.0
//
// Kernel processes that read host signals through the kernel's mirrors of
// them, after host writes: FSIM_KERNEL_VERIFY checks each mirror against the
// host's own value (resolution and all) when the write is made, when the host
// commits it and at the end of each verified activation.
//   bus: 72 bits (two words) with several drivers: an array of cmos switches
//      (28.3.6: one instance per bit; each writes its bit of bus) and a
//      continuous assignment of the part-select bus[67:56], which crosses
//      the 64-bit word boundary. A cmos is an nmos and a pmos on one output
//      (28.9, 28.7): on_n = 1 passes data; on_n = 0 with on_p = 1 leaves it
//      z. A cmos passes its data input's strong strength (28.13), so the
//      drivers of a bit resolve as a wire's of equal strengths (28.12.1, 6.6.1
//      Table 6-2): z gives way to the other driver, 0 against 1 gives x. The
//      thread reads bus through its mirror:
//        t1: drive is z, so bus is data: a50123456789abcdef.
//        t3: drive = 5a3 against data[67:56] = 501: 5 agrees, a against 0
//            and 3 against 1 give x bits: a5XX23456789abcdef (21.2.1.3: a
//            digit with some x bits shows X).
//        t4: the switches are off: z, then 5a3, then z: z5a3zzzzzzzzzzzzzz.
//   solo: 72 bits with one driver, the continuous assignment of the
//      part-select solo[67:56] (the other bits have no driver: z, 6.6). The
//      $strobe keeps it a host signal (printed at the end of times 1 and 3,
//      21.2.2), and the kernel mirrors its driver's updates as they are made:
//      only always_comb reads the mirror, solo[71:60], z then the top 8 bits
//      of solo_drive (t1: z000zzzzzzzzzzzzzz, high z00; t3:
//      zc3czzzzzzzzzzzzzz, high zc3).
`timescale 1ns/1ns
module top;
  wire [71:0] bus;
  reg [71:0] data = 72'ha5_0123456789abcdef;
  reg on_n = 1'b1, on_p = 1'b1;
  reg [11:0] drive = 12'hzzz;
  cmos sw [71:0] (bus, data, on_n, on_p);
  assign bus[67:56] = drive;

  wire [71:0] solo;
  reg [11:0] solo_drive = 12'h000;
  logic [11:0] solo_high;
  assign solo[67:56] = solo_drive;
  always_comb solo_high = solo[71:60];

  initial begin
    #1 $display("t1 bus=%h high=%h", bus, solo_high);
    #1 drive = 12'h5a3;
    solo_drive = 12'hc3c;
    #1 $display("t3 bus=%h high=%h", bus, solo_high);
    on_n = 1'b0;
    #1 $display("t4 bus=%h", bus);
  end

  initial begin
    #1 $strobe("t1 solo=%h", solo);
    #2 $strobe("t3 solo=%h", solo);
  end
endmodule
