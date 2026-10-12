// fsim: top=sv_language_details_batch51
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 7.4.1: packed arrays of packed structures, their members
// through constant and runtime indices, and as ports; 10.9.1: assignment
// patterns for packed arrays (positional, keyed, default, nested) and for a
// multidimensional packed member; 12.7.3: foreach with every loop variable
// omitted; 27.4: a genvar hiding a module variable of its name; 23.8: a
// function of a module instance called as a statement; 23.2.2.3: an output
// net port's initializer; 8.26: a class handle passed as the interface
// class it implements; 6.20.1: an element of an unpacked array parameter;
// 11.8.2: a size cast extended as unsigned in an unsigned expression;
// 9.6.2: disabling the block of a sibling fork branch; 28.9: cmos.
`timescale 1ns / 1ns
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } b51_pair_t;

interface class b51_named;
  pure virtual function int id();
endclass

class b51_item implements b51_named;
  virtual function int id();
    return 51;
  endfunction
endclass

class b51_holder;
  b51_named held;
  function void hold(b51_named value);
    held = value;
  endfunction
endclass

module b51_invert(output b51_pair_t [1:0] o, input b51_pair_t [1:0] i);
  assign o = ~i;
endmodule

module b51_counter(output wire tie = 1'b1);
  parameter int sizes[3:1] = '{10, 20, 30};
  localparam int first = sizes[1];
  int count;
  function void bump(int by);
    count += by;
  endfunction
endmodule

module sv_language_details_batch51;
  b51_pair_t [1:0] pairs, inverted;
  struct packed { int x; bit [2:0][3:0] y; } nested;
  logic [3:0][3:0] keyed;
  integer slot;
  integer seen[0:3];
  localparam [31:0] b51_cast = 4'(6'sh3f) + 'd0;
  wire tie;
  wire cmos_out;
  reg cmos_in = 1'b0, cmos_n = 1'b1, cmos_p = 1'b1;
  cmos (cmos_out, cmos_in, cmos_n, cmos_p);
  b51_invert b51_invert_u(.o(inverted), .i(pairs));
  b51_counter b51_counter_u(.tie(tie));
  for (genvar slot = 0; slot < 4; slot++) begin : b51_slots
    initial seen[slot] = slot * 10;
  end

  initial begin
    int failures;
    int index;
    int iterations;
    b51_item item;
    b51_holder holder;
    failures = 0;
    pairs = '0;
    pairs[1].lo = 4'h5;
    pairs[0].hi = 4'ha;
    index = 1;
    pairs[index].hi = 4'h3;
    if (pairs !== 16'h35a0 || pairs[index].lo !== 4'h5) failures++;
    keyed = '{0: 4'h1, 3: 4'h2, default: 4'hf};
    if (keyed !== 16'h2ff1) failures++;
    nested = '{1, '{4, 5, 6}};
    if (nested !== 44'h00000001456 || nested.y[1] !== 4'h5) failures++;
    iterations = 0;
    foreach (seen[]) iterations++;
    if (iterations != 0) failures++;
    b51_counter_u.bump(3);
    b51_counter_u.bump(4);
    if (b51_counter_u.count != 7) failures++;
    item = new;
    holder = new;
    holder.hold(item);
    if (holder.held.id() != 51) failures++;
    if (b51_counter_u.first != 30 || b51_cast != 32'h0000000f) failures++;
    fork
      begin : b51_victim
        forever #1;
      end
      begin
        #2 disable b51_victim;
      end
    join
    #1;
    if (inverted !== 16'hca5f || tie !== 1'b1 || cmos_out !== 1'b0) failures++;
    if (seen[0] != 0 || seen[3] != 30 || slot !== 'x) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
