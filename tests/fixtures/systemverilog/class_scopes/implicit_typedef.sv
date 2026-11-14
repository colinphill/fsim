// SPDX-License-Identifier: Apache-2.0
// `implicit` is not a keyword (IEEE 1800-2023 Annex B), so it can name a
// user type (6.18): a variable, a function return and formal, a parameter
// and a class parameter declared with it have the typedef's type, not the
// type of an untyped declaration (6.20.2, 13.4).
typedef logic [3:0] implicit;

function implicit bump(implicit a);
  return a + 1;
endfunction

class C #(implicit P = 4'hc);
  function implicit get();
    return P;
  endfunction
endclass

module top;
  parameter implicit PP = 4'ha;
  localparam implicit LP = 4'h5;
  implicit x;
  C c0;
  C #(4'h3) c1;
  initial begin
    x = 4'hf;
    c0 = new;
    c1 = new;
    $display("var %b bump %b", x, bump(x));
    $display("param %b %b", PP, LP);
    $display("class %b %b", c0.get(), c1.get());
    $display("width %0d %0d", $bits(x), $bits(PP));
  end
endmodule
