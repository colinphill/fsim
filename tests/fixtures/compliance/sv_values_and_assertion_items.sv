// fsim: top=sv_values_and_assertion_items std=2017
// IEEE 1800-2017 11.4.5: a known differing bit decides == and != even when
// other bits are unknown; 21.2.1.4: %d of partly unknown values; 7.4.5: a
// variable index under a constant one in a multidimensional packed array;
// 13.3 and 6.24.1: a 2-state formal receives X and Z bits as 0; 10.3.3: a
// delayed continuous assignment drives X until its first value matures;
// 10.10.1: replication in assignment patterns; 12.4.2: unique and priority
// if; 16.3 and 16.4: immediate assume and cover, and a deferred assertion
// as a module item; 14.14: a global clocking block; 18.5.1: out-of-block
// constraints; 22.5.1: a conditional directive whose name pastes a macro
// argument.
`timescale 1ns / 1ps
`define READY_YES
`define pick(tag) `ifdef READY_``tag 1 `else 0 `endif

module sv_values_and_assertion_items;
  class item;
    rand bit [3:0] value;
    constraint fixed;
    extern constraint bounded;
  endclass

  constraint item::fixed { value > 2; }
  constraint item::bounded { value < 4; }

  function bit [11:0] widen(input bit [11:0] value);
    return value;
  endfunction

  logic clk = 0;
  global clocking @(posedge clk); endclocking

  logic [7:0] a = 8'b1101x001, b = 8'b1101x000, c = 8'b1101x001;
  logic [1:0][3:0][7:0] packed_word;
  logic [3:0] index;
  logic outer;
  wire [2:0] delayed;
  assign #1 delayed = 3'd5;
  int grid[1:2][1:6] = '{2{'{3{4, 5}}}};
  int covered;
  int failures;
  item draw;

  assert #0 (grid[2][6] == 5);

  initial begin
    if ((a == b) !== 1'b0 || (a != b) !== 1'b1 || (a == c) !== 1'bx)
      failures++;
    if ($sformatf("%0d|%0d|%0d", 4'bzzxx, 4'bzzzz, 4'b00zz) != "X|z|Z")
      failures++;

    packed_word = '0;
    index = 2;
    outer = 1;
    packed_word[1][index] = 8'hab;
    packed_word[outer][index - 1] = 8'hcd;
    packed_word[0][index] = 8'h12;
    if (packed_word !== 64'h00abcd0000120000) failures++;

    if (widen(12'b0000x0z00111) !== 12'b000000000111) failures++;

    #0.5;
    if (delayed !== 3'bx) failures++;
    #1;
    if (delayed !== 3'd5) failures++;

    if (grid[1][1] != 4 || grid[1][2] != 5 || grid[2][5] != 4) failures++;

    unique if (index == 2) covered += 1;
    else if (index == 3) failures++;
    priority if (index == 7) failures++;
    else covered += 10;

    assume (covered == 11);
    cover (covered == 11) covered += 100;
    if (covered != 111) failures++;

    draw = new;
    if (!draw.randomize() || draw.value != 3) failures++;

    if (`pick(YES) != 1 || `pick(NO) != 0) failures++;

    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
