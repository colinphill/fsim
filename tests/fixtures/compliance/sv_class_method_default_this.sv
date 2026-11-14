// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 13.5.3: a default argument is evaluated in the scope that
// declares the subroutine each time a call uses it. For a class method that
// scope is the class, so a default that calls a method or reads a property
// of the class (`T data = get_default_data()`, as uvm_event#(T)::trigger
// declares it) uses the object the method is called on: each call through
// a handle, a function call in an expression, a task call and a call of
// the overriding class all see their own object's value.
module top;
  class C;
    int dflt;
    int last;
    function new(int d); dflt = d; endfunction
    virtual function int get_default(); return dflt; endfunction
    virtual function void trigger(int data = get_default());
      last = data;
    endfunction
    function int twice(int data = dflt + get_default());
      return data;
    endfunction
    task wait_and_set(int delay = dflt);
      #delay last = delay;
    endtask
  endclass

  class D extends C;
    function new(int d); super.new(d); endfunction
    virtual function int get_default(); return dflt * 10; endfunction
  endclass

  int failures;

  initial begin
    C a = new(7);
    C b = new(9);
    D d = new(4);
    C as_c = d;
    a.trigger();
    b.trigger();
    if (a.last != 7 || b.last != 9) failures++;
    a.trigger(3);
    if (a.last != 3) failures++;
    as_c.trigger();
    if (d.last != 40) failures++;
    if (a.twice() != 14 || b.twice(1) != 1 || d.twice() != 44) failures++;
    b.wait_and_set();
    if (b.last != 9 || $time != 9) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
