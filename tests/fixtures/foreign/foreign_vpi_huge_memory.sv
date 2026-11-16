// SPDX-License-Identifier: Apache-2.0
// Words of memories with millions of words through VPI (IEEE 1800-2023
// 37.17, 38.34, 38.36.1), the same under every engine: a word reached by
// index, by name or by iteration reads, puts and is watched as any word,
// two handles to one word compare equal, and iteration yields every word.
module top;
  logic [7:0] mem [0:16777215];
  int down [16777215:0];
  logic signed [11:0] two [-2:-5][1023:0];
  initial begin
    mem[16777215] = 8'hA5;
    mem[5] = 8'd3;
    down[1] = -7;
    two[-3][1000] = -12'sd99;
    // t=1: the plug-in reads, puts and watches (see the plug-in).
    #2 mem[7] = 8'd9;
    down[2] = 11;
    #2 $display("#c t=%0t mem[12345678]=%0d mem[8]=%0d down[16000000]=%0d two[-2][3]=%0d",
      $time, mem[12345678], mem[8], down[16000000], two[-2][3]);
    $finish;
  end
endmodule
