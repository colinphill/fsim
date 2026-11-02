// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.22, 8.26, 18.5: each instance of a module has classes of
// its own; their objects still dispatch through implemented interface
// classes and randomize under their class's constraints.
module m #(int LIMIT = 4);
  interface class Courier;
    pure virtual function int deliver();
  endclass
  class Car implements Courier;
    virtual function int deliver();
      return LIMIT;
    endfunction
  endclass
  class Item;
    rand bit [3:0] mode;
    rand bit [7:0] data[3];
    constraint mode_c { mode inside {[1 : 3]}; }
    constraint data_c {
      foreach (data[i]) {
        solve mode before data[i];
        data[i] == mode;
      }
    }
  endclass
  function automatic int send(Courier courier);
    return courier.deliver();
  endfunction
  int sent;
  int ok;
  initial begin
    static Car car = new;
    static Item item = new;
    sent = send(car);
    ok = item.randomize() with { mode <= 2; };
    if (ok == 1 && item.mode inside {[1 : 2]} && item.data[2] == item.mode)
      ok = 2;
  end
endmodule
module top;
  m #(.LIMIT(5)) a ();
  m #(.LIMIT(7)) b ();
  initial begin
    #1;
    if (a.sent == 5 && b.sent == 7 && a.ok == 2 && b.ok == 2)
      $display("PASS");
    else
      $display("FAIL %0d %0d %0d %0d", a.sent, b.sent, a.ok, b.ok);
  end
endmodule
