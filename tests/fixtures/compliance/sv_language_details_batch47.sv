// fsim: top=sv_language_details_batch47
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 10.9.2: type-keyed structure patterns; 8.8: a typed
// constructor with a named argument; 8.26.3: an interface class extending
// two interface classes and a typedef inherited from one; 11.5.1: writes
// through out-of-range and run-time selects; 7.5.1: a local dynamic array
// sized by `new[n]`; 9.7: a method of `process::self()`; 23.3.3: an
// unconnected input port net floats.
`timescale 1ns / 1ns
interface class batch47_left;
  typedef int count_t;
  pure virtual function count_t left_count();
endclass

interface class batch47_right;
  pure virtual function int right_count();
endclass

interface class batch47_both extends batch47_left, batch47_right;
  pure virtual function count_t both_count();
endclass

class batch47_counter implements batch47_both;
  virtual function int left_count();
    return 1;
  endfunction
  virtual function int right_count();
    return 2;
  endfunction
  virtual function int both_count();
    return 3;
  endfunction
endclass

class batch47_base;
  int value;
  function new(int start = 1);
    value = start;
  endfunction
endclass

class batch47_derived extends batch47_base;
  function new(int start = 5);
    super.new(start + 10);
  endfunction
endclass

module batch47_floating(input wire in, output logic seen);
  initial #1 seen = (in === 1'bz);
endmodule

module sv_language_details_batch47;
  typedef struct { int a; int b; logic [3:0] c; } triple_t;
  triple_t triple;
  logic [6:0] grid[4];
  logic floating_seen;
  batch47_floating floating(.seen(floating_seen));

  task automatic sized(int count, output int result);
    int values[] = new[count];
    result = values.size();
  endtask

  initial begin
    batch47_counter counter;
    batch47_both both;
    batch47_left left;
    batch47_right right;
    batch47_base built;
    bit [5:0] narrow;
    int row, column, dynamic_size;
    counter = new;
    both = counter;
    left = both;
    right = both;
    built = batch47_derived::new(.start(2));
    triple = '{default: 0, int: 7};
    narrow = '0;
    narrow[8:4] = 5'b10001;
    grid[2] = '0;
    row = 2;
    column = 3;
    grid[row][column] = 1'b1;
    sized(4, dynamic_size);
    process::self().srandom(7);
    #2;
    if (both.both_count() + left.left_count() + right.right_count() != 6)
      $display("FAIL interface classes");
    else if (built.value != 12) $display("FAIL typed constructor");
    else if (triple.a != 7 || triple.b != 7 || triple.c != 4'd0)
      $display("FAIL type-keyed pattern");
    else if (narrow != 6'b010000) $display("FAIL out-of-range write");
    else if (grid[2] != 7'b0001000) $display("FAIL element bit write");
    else if (dynamic_size != 4) $display("FAIL new[n] initializer");
    else if (floating_seen !== 1'b1) $display("FAIL floating input");
    else $display("PASS");
  end
endmodule
