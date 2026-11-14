// SPDX-License-Identifier: Apache-2.0
// A class value parameter bound to a module parameter that is a four-state
// vector or a string reads the instance's value (IEEE 1800-2023 8.25, 6.20.2,
// 23.2.2), and equal values are one specialization.
class V #(logic [3:0] B = 4'b0000);
  function logic [3:0] get(); return B; endfunction
endclass

class S #(string T = "none");
  function string get(); return T; endfunction
endclass

module sub #(parameter logic [3:0] X = 4'b1x01, parameter string N = "dflt");
  V #(X) v;
  S #(N) s;
  initial begin
    v = new;
    s = new;
    $display("%m x=%b s=%s", v.get(), s.get());
  end
endmodule

module top;
  sub a();
  sub #(.X(4'b10z1), .N("alpha")) b();
  sub #(.X(4'b1x01), .N("dflt")) c();
endmodule
