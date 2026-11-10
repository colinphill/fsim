// SPDX-License-Identifier: Apache-2.0
// Words of unpacked arrays through VPI (IEEE 1800-2023 37.17, 38.34,
// 38.36.1), the same under every engine, the compiled kernel holding the
// arrays: a vpi_put_value on a word of a variable array or of a net array
// is what the next read of the word returns, wakes the processes that wait
// on the word or read it, and is announced by a value change callback on
// the word, as a change a process makes is. A deposit on a word of a net
// array holds until its driver next changes (38.34).
module top;
  logic [7:0] mem [0:3];
  wire [7:0] w [0:3];
  logic [7:0] drv [0:3];
  logic [7:0] c;
  logic [7:0] cw;
  time mem_woken;
  time w_woken;
  assign w[0] = drv[0];
  assign w[1] = drv[1];
  assign w[2] = drv[2];
  assign w[3] = drv[3];
  always_comb c = mem[2];
  always_comb cw = w[2];
  always @(mem[1]) mem_woken = $time;
  always @(w[1]) w_woken = $time;
  initial begin
    for (int i = 0; i < 4; i++) begin
      mem[i] = 8'(i);
      drv[i] = 8'(i);
    end
    // t=5, 6, 7: the plug-in's puts (see the plug-in).
    #10 $display("#c t=%0t c=%0d cw=%0d mem[1]=%0d w[1]=%0d mem[3]=%0d woken mem=%0t w=%0t",
      $time, c, cw, mem[1], w[1], mem[3], mem_woken, w_woken);
    // t=12: a process writes a watched word; the driver of w[1] changes.
    #2 mem[3] = 8'd33;
    drv[1] = 8'd11;
    #2 $display("#c t=%0t mem[3]=%0d w[1]=%0d w[2]=%0d woken w=%0t",
      $time, mem[3], w[1], w[2], w_woken);
    $finish;
  end
endmodule
