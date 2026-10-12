// fsim: top=sv_language_details_batch49
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.12.2: real and integral values converted in conditional
// alternatives, assignment patterns, port connections and compound
// assignments; 11.4.11: a real conditional with an unknown condition;
// 11.4.2: `++`/`--` of a real as an expression; 11.8.1: a signed operand of
// an unsigned compound assignment; 6.11: bitwise operators on time;
// 6.20.2: range queries of an untyped localparam; 6.20.3: a type parameter
// bound to real; 6.18: a typedef of string; 8.20: virtual task dispatch;
// 13.5: a string formal passed on between class methods; 15.4: a module
// semaphore initialized by new.
`timescale 1ns / 1ns
module batch49_to_int(input logic [3:0] value, output real doubled);
  assign doubled = value * 2;
endmodule

module batch49_typed #(parameter type T = int) (output real seen);
  T held = 1.5;
  assign seen = held;
endmodule

class batch49_base;
  int hits;
  virtual task touch();
    hits = 1;
  endtask
endclass

class batch49_derived extends batch49_base;
  string label;
  task touch();
    hits = 2;
  endtask
  function void name(string text);
    store(text);
  endfunction
  function void store(string text);
    label = text;
  endfunction
endclass

module sv_language_details_batch49;
  typedef struct packed { shortint a; byte b; } pair_t;
  typedef string text_t;
  localparam narrow = 16'b1010;
  real from_port, typed_value, chosen, unknown_same, stepped;
  real reals[];
  int ints[2];
  pair_t pair;
  integer scaled;
  reg [7:0] widened;
  reg signed [3:0] minus_one;
  reg unknown;
  time stamp;
  text_t text = "Yo";
  semaphore lock = new(1);
  batch49_to_int convert (.value(7.4), .doubled(from_port));
  batch49_typed #(.T(real)) typed (.seen(typed_value));

  initial begin
    batch49_base handle;
    batch49_derived derived;
    int failures;
    derived = new;
    handle = derived;
    handle.touch();
    derived.name("abc");
    unknown = 1'bx;
    chosen = 1'b1 ? 6 : 0.5;
    unknown_same = unknown ? 3 : 3.0;
    reals = '{1, 2, 3};
    ints = '{1.7, -2.5};
    pair = '{1, 2.0};
    scaled = 5;
    scaled -= 2.0;
    widened = 4;
    minus_one = -1;
    widened += minus_one;
    stepped = 19.5;
    stepped = --stepped + stepped++;
    stamp = 64'd12;
    lock.get(1);
    #1;
    failures = 0;
    if (chosen != 6.0 || unknown_same != 3.0) failures++;
    if (reals[2] != 3.0 || ints[0] != 2 || ints[1] != -3) failures++;
    if (pair.a != 1 || pair.b != 2) failures++;
    if (scaled != 3 || widened != 19) failures++;
    if (stepped != 37.0) failures++;
    if ((stamp & 64'd4) != 4 || (stamp % 5) != 2) failures++;
    if ($left(narrow) != 15 || $high(narrow) != 15) failures++;
    if (typed_value != 1.5 || from_port != 14.0) failures++;
    if (text.len() != 2 || derived.label != "abc") failures++;
    if (handle.hits != 2) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
