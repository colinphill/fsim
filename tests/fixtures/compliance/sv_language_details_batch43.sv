// fsim: top=sv_language_details_batch43
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 5.6.1: an escaped identifier names the simple identifier
// with the same characters; A.1.11: empty items; 28.10: an unnamed pullup
// whose terminal is a net; 6.16: writing a zero byte to a string element
// leaves the string unchanged; 13.4.1: `return;` ends a void function;
// 20.6.1: $typename of a built-in type; 18.5.2: a derived class inherits
// its base class's constraints.
`timescale 1ns / 1ns
class base_item;
  rand bit [7:0] b;
  constraint c { b == 8'd5; }
endclass

class derived_item extends base_item;
  rand bit [7:0] b2;
  constraint c2 { b2 == b; }
endclass

;

module sv_language_details_batch43;
  wire pulled;
  pullup (pulled);
  reg \plain ;
  string s;
  int calls;
  derived_item item;
  string type_name;

  function void bump(input int value);
    calls++;
    if (value > 2)
      return;
    calls++;
  endfunction

  initial begin
    item = new;
    plain = 1'b1;
    s = "Test";
    s[1] = 8'h00;
    bump(1);
    bump(5);
    type_name = $typename(logic);
    #1;
    if (!item.randomize()) $display("FAIL randomize");
    else if (item.b != 8'd5 || item.b2 != 8'd5)
      $display("FAIL constraints %0d %0d", item.b, item.b2);
    else if (\plain !== 1'b1) $display("FAIL escaped identifier");
    else if (pulled !== 1'b1) $display("FAIL pullup %b", pulled);
    else if (s != "Test") $display("FAIL string %s", s);
    else if (calls != 3) $display("FAIL calls %0d", calls);
    else if (type_name != "logic") $display("FAIL typename");
    else $display("PASS");
  end;
endmodule
