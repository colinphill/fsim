// fsim: top=sv_language_details_batch52
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 13.4.3: constant functions returning strings, with
// enumeration methods (6.19.5) written without parentheses;
// 25.9: virtual-interface tasks and functions dispatched to the selected
// instance, and bit- and part-selects of a member written through the
// handle; 25.5.4: modport expressions naming signals of a nested interface;
// 12.5: a case statement with a string selector; 6.16: string methods
// without parentheses and casts between strings and vectors; 8.10: a static
// class function named without parentheses; 8.7: `super.new` with a call
// argument; 18.13.3: `srandom` of an object; 7.10.2.4: `void'` of a queue
// pop; 6.18: a typedef local to a block; 6.19.5: `name` of a structure
// member; 10.3: continuous assignment to a member of an unpacked structure;
// 6.20.2: a member of a structure-typed parameter; 11.4.2: increment of an
// array element inside an expression.
`timescale 1ns / 1ns
interface b52_bus;
  logic [7:0] data;
  int calls;
  task automatic put(input logic [7:0] value);
    data = value;
    calls++;
  endtask
  function automatic logic [7:0] doubled();
    return data * 2;
  endfunction
endinterface

interface b52_inner;
  logic [7:0] val;
endinterface

interface b52_outer;
  b52_inner inner ();
  modport mp(output .w(inner.val), input .r(inner.val));
endinterface

module b52_writer(b52_outer.mp port);
  assign port.w = 8'h5a;
endmodule

typedef enum logic [1:0] { B52_IDLE, B52_RUN, B52_DONE } b52_state_t;
typedef struct packed { int a; int b; } b52_pair_t;
typedef struct { logic [3:0] lo; logic [3:0] hi; } b52_halves_t;
typedef struct { b52_state_t state; int count; } b52_status_t;

class b52_base;
  int value;
  function new(int v);
    value = v;
  endfunction
endclass

class b52_derived extends b52_base;
  static function int seed();
    return 7;
  endfunction
  function new(int v);
    super.new(seed() + v);
  endfunction
endclass

class b52_random;
  rand bit [15:0] value;
endclass

module sv_language_details_batch52;
  parameter b52_pair_t b52_pair = '{a: 3, b: 4};
  localparam int b52_sum = b52_pair.a + b52_pair.b;

  function automatic string b52_label(int count);
    string text = "n";
    for (int i = 0; i < count; i++) text = {text, "x"};
    return text;
  endfunction
  function automatic b52_state_t b52_after_two();
    b52_state_t state;
    state = B52_IDLE;
    return state.next.next;
  endfunction
  function automatic string b52_state_name(b52_state_t state);
    return state.name;
  endfunction

  localparam string b52_text = b52_label(3);
  localparam b52_state_t b52_last = b52_after_two();
  localparam string b52_run_name = b52_state_name(B52_RUN);

  b52_bus bus0 ();
  b52_bus bus1 ();
  b52_outer outer ();
  b52_writer writer (.port(outer));
  b52_halves_t halves;
  logic [7:0] b52_source = 8'h3c;
  assign halves.lo = b52_source[3:0];
  assign halves.hi = b52_source[7:4];

  initial begin
    virtual b52_bus handle;
    b52_derived derived;
    b52_random first, second;
    b52_status_t status;
    string word;
    bit [23:0] packed_word;
    int queue[$];
    int counts[2];
    int failures;
    int result;
    failures = 0;
    if (b52_text != "nxxx") failures++;
    if (b52_last != B52_DONE || b52_run_name != "B52_RUN") failures++;
    if (b52_sum != 7) failures++;

    handle = bus1;
    handle.put(8'h21);
    handle = bus0;
    handle.put(8'h10);
    if (bus0.data != 8'h10 || bus1.data != 8'h21 || bus1.calls != 1) failures++;
    handle = bus1;
    if (handle.doubled() != 8'h42) failures++;
    handle.data[3:0] = 4'hf;
    handle.data[7] = 1'b1;
    if (bus1.data !== 8'haf) failures++;

    word = "run";
    case (word)
      "idle": result = 1;
      "run": result = 2;
      default: result = 3;
    endcase
    if (result != 2 || word.toupper != "RUN" || word.len != 3) failures++;
    packed_word = 24'(word);
    if (packed_word != 24'h72756e) failures++;
    word = string'(packed_word);
    if (word != "run") failures++;
    if (b52_derived::seed != 7) failures++;

    derived = new(1);
    if (derived.value != 8) failures++;

    first = new;
    second = new;
    first.srandom(52);
    second.srandom(52);
    if (!first.randomize() || !second.randomize()) failures++;
    if (first.value != second.value) failures++;

    queue = '{1, 2, 3};
    void'(queue.pop_front());
    if (queue.size() != 2 || queue[0] != 2) failures++;

    begin
      typedef logic [5:0] b52_local_t;
      b52_local_t local_value;
      local_value = '1;
      if ($bits(local_value) != 6 || local_value != 6'h3f) failures++;
    end

    status.state = B52_RUN;
    if (status.state.name() != "B52_RUN") failures++;

    counts = '{0, 0};
    result = counts[1]++ + 10;
    if (result != 10 || counts[1] != 1) failures++;

    #1;
    if (halves.lo != 4'hc || halves.hi != 4'h3) failures++;
    if (outer.inner.val != 8'h5a) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
