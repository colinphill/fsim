// fsim: top=sv_language_details_batch50
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 8.20: virtual and non-virtual class functions, super.;
// 8.7: constructor chains, property initializer order and omitted
// constructor arguments; 8.10: static class functions; 7.5, 7.10: dynamic
// array and queue properties used in methods and through a handle;
// 18.5: randomization of a 32-bit property bounded by its constraints;
// 5.7.1: an unsized fill literal compared with a sized value and filling
// an input port; 11.6.1: a constant shift evaluated at its context width;
// 18.4, 18.6.1, 18.7, 18.11: enumerated random properties, enumeration
// literals in constraints, `randomize()` with inline constraints inside a
// method and a listed non-rand property; 8.23: a nested class reading a
// static property of its enclosing class; 27.4: genvar steps with `*=`;
// 6.20.2: a signed parameter without a range; 21.2.1.3: `%d` of a real;
// 13.3: copy-out to a two-state actual; 11.4.2: `x++` in a condition of a
// constant-argument function; 9.4.1: an unknown delay is zero.
`timescale 1ns / 1ns
class batch50_shape;
  int sides = 0;
  int scale = sides + 10;
  function new(int s = 3);
    sides = s;
  endfunction
  virtual function int area();
    return sides;
  endfunction
  function int kind();
    return 1;
  endfunction
endclass

class batch50_square extends batch50_shape;
  int doubled = sides * 2;
  function new(int side);
    super.new(4);
    doubled = doubled + side;
  endfunction
  virtual function int area();
    return 100 + super.area();
  endfunction
  function int kind();
    return 2;
  endfunction
endclass

class batch50_bag;
  int items[];
  int pending[$];
  int total;
  function new(int count, int first = 7, int step = 1);
    items = new[count];
    foreach (items[i]) items[i] = first + i * step;
    pending.push_back(first);
  endfunction
  function int sum();
    int result = 0;
    foreach (items[i]) result += items[i];
    return result + pending.size();
  endfunction
  static function int twice(int value);
    return value * 2;
  endfunction
endclass

typedef enum bit [2:0] { B50_RED, B50_GREEN, B50_BLUE, B50_BLACK = 6 } b50_color_t;

class batch50_painter;
  rand b50_color_t color;
  rand int level;
  int fixed_level;
  constraint not_black_c { color != B50_BLACK; }
  function int roll();
    return this.randomize() with { level > 2 && level < 5; };
  endfunction
  function int fix();
    return randomize(fixed_level) with { fixed_level == 9; };
  endfunction
endclass

class batch50_outer;
  static int count = 40;
  class inner;
    static function int bump();
      ++count;
      return count;
    endfunction
  endclass
endclass

module batch50_port_fill(input [3:0] x, output [3:0] y);
  assign y = x;
endmodule

class batch50_bounded;
  rand int value;
  constraint range_c { value > 1000; value < 1010; }
endclass

module sv_language_details_batch50;
  parameter signed b50_narrow = 4'd15;
  wire [3:0] b50_filled;
  int b50_genvar_sum;
  bit [11:0] b50_two_state;
  batch50_port_fill b50_fill_u('1, b50_filled);
  for (genvar g = 1; g < 16; g *= 2) begin : b50_steps
    initial b50_genvar_sum += g;
  end

  task automatic b50_copy(output logic [7:0] value);
    value = 8'b0x0z0111;
  endtask

  function automatic int b50_after_update(int x);
    int r;
    r = (x++ > 0);
    return x;
  endfunction

  initial begin
    batch50_shape shape;
    batch50_square square;
    batch50_bag bag;
    batch50_bounded bounded;
    reg [7:0] shifted;
    int failures;
    failures = 0;
    square = new(5);
    shape = square;
    if (shape.area() != 104 || shape.kind() != 1 || square.kind() != 2)
      failures++;
    if (square.sides != 4 || square.scale != 10 || square.doubled != 13)
      failures++;
    bag = new(3, , 2);
    if (bag.sum() != 28 || bag.items.size() != 3 || bag.items[2] != 11)
      failures++;
    bag.items[0] = 1;
    bag.pending.push_back(5);
    if (bag.sum() != 23 || bag.pending.pop_front() != 7) failures++;
    if (batch50_bag::twice(21) != 42) failures++;
    bounded = new;
    repeat (5) begin
      if (!bounded.randomize()) failures++;
      if (bounded.value <= 1000 || bounded.value >= 1010) failures++;
    end
    if (4'hf !== '1 || !(8'hff === '1)) failures++;
    shifted = 1'b1 << 3;
    if (shifted != 8) failures++;
    begin
      batch50_painter painter = new;
      string text;
      integer unknown_delay;
      repeat (8) begin
        if (painter.roll() != 1) failures++;
        if (painter.color == B50_BLACK || painter.color > B50_BLUE
            || painter.level < 3 || painter.level > 4) failures++;
      end
      if (painter.fix() != 1 || painter.fixed_level != 9) failures++;
      if (batch50_outer::inner::bump() != 41) failures++;
      if ($bits(b50_narrow) != 4 || b50_narrow != -1) failures++;
      text = $sformatf("%0d", 2.6);
      if (text != "3") failures++;
      b50_copy(b50_two_state);
      if (b50_two_state !== 12'b000000000111) failures++;
      if (b50_after_update(5) != 6) failures++;
      unknown_delay = 'x;
      #(unknown_delay);
      #1;
      if (b50_filled !== 4'b1111 || b50_genvar_sum != 15) failures++;
    end
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
