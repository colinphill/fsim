// fsim: top=sv_copies_and_constructors std=2017
// IEEE 1800-2017 18.16: randcase (zero weights make the selection
// deterministic); 11.3.6: assignments
// within expressions; 8.8: typed constructors; 8.12: shallow copies; 8.17:
// base constructor arguments in the extends clause; 8.3: empty class items;
// 12.5: a default case item without a colon; 5.7.1: white space after the
// base of a literal; 12.7.1 and 13.3: `var` loop variables and formals;
// 26.2: an event declared in a package.
package sv_sequences_events;
  event go;
endpackage

module sv_copies_and_constructors;
  import sv_sequences_events::*;

  class base;
    int s = 2;
    function new(int d = 99);
      s = d;
    endfunction
  endclass

  class implicit_args extends base(5);
    ; ;
  endclass

  class explicit_args extends base(7);
    int a;
    function new(int x = 1);
      a = x;
    endfunction
  endclass

  class zero_formals extends base;
    int k;
    function new();
      super.new(6);
      k = 3;
    endfunction
  endclass

  class item;
    int v;
    string s = "item";
  endclass

  int failures = 0;

  task check(input int got, input int expected, input string what);
    if (got !== expected) begin
      $display("FAIL %s: got %0d expected %0d", what, got, expected);
      failures++;
    end
  endtask

  function automatic int weighted(input int selector);
    int r = 0;
    randcase
      0 : r = 1;
      selector : r = 2;
      0 : r = 3;
    endcase
    return r;
  endfunction

  function automatic int doubled(var int v);
    return v * 2;
  endfunction

  implicit_args first_object;
  explicit_args second_object;
  zero_formals third_object;
  base typed;
  item original, copy;
  int a, b, c, total;
  logic [3:0] spaced;

  initial begin
    first_object = new;
    second_object = new(9);
    third_object = new;
    check(first_object.s, 5, "extends-clause constructor arguments");
    check(second_object.s, 7, "extends-clause arguments with a constructor");
    check(second_object.a, 9, "derived constructor argument");
    check(third_object.s, 6, "super.new in a constructor without formals");
    check(third_object.k, 3, "constructor body after super.new");
    typed = zero_formals::new;
    check(typed.s, 6, "typed constructor");

    original = new;
    original.v = 5;
    copy = new original;
    copy.v = 7;
    check(original.v, 5, "shallow copy leaves the original");
    check(copy.v, 7, "shallow copy property write");
    if (copy.s != "item") begin
      $display("FAIL shallow copy string property: %s", copy.s);
      failures++;
    end

    check(weighted(4), 2, "randcase");

    a = (b = (c = 5));
    check(a + b + c, 15, "chained assignment expressions");
    c = 9;
    total = (c -= 2) + 1;
    check(total, 8, "compound assignment expression");
    check(c, 7, "compound assignment expression target");

    for (var int i = 0; i < 3; i++) total = i;
    check(total, 2, "var loop variable");
    check(doubled(21), 42, "var formal");

    spaced = 'b 1010;
    check(spaced, 4'b1010, "white space after the base");
    case (spaced)
      4'b0000: total = 0;
      default total = 1;
    endcase
    check(total, 1, "default case item without a colon");

    fork
      begin
        @(go);
        total = 77;
      end
      #1 -> go;
    join
    check(total, 77, "package event");

    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
