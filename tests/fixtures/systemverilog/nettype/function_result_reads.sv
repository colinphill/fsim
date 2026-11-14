// SPDX-License-Identifier: Apache-2.0
// reads of a function's result variable members before the return (13.4.1)
module top;
  typedef struct { real field1; bit field2; } T;
  typedef struct { logic [7:0] a; bit b; } P;
  function automatic T fa(input real x);
    fa.field1 = x;
    $display("  fa read=%f", fa.field1);
    fa.field1 = fa.field1 + 1.0;
  endfunction
  function T fs(input real x);
    fs.field1 = x;
    $display("  fs read=%f", fs.field1);
    fs.field1 = fs.field1 + 1.0;
  endfunction
  function automatic P fp(input logic [7:0] x);
    fp.a = x;
    $display("  fp read=%h", fp.a);
    fp.a = fp.a + 1;
  endfunction
  T t, u; P p;
  initial begin t = fa(1.5); u = fs(1.5); p = fp(8'h41); $display("fa=%f fs=%f fp=%h", t.field1, u.field1, p.a); end
endmodule
