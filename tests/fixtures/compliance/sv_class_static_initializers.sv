// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 8.9 and 8.25: a static property's initializer runs once,
// in the class specialization it belongs to, so it reads that
// specialization's parameters, constants and earlier static properties.
package static_pkg;
  int base = 40;
  class counter #(int P = 1);
    static int start = base + P;
  endclass
endpackage

class sized #(int P = 4);
  localparam int Q = P + 1;
  static int from_parameter = P;
  static int doubled = P * 2;
  static int from_constant = Q;
  static int from_static = doubled + 1;
  static logic [7:0] pattern = 8'hA5;
  static string name = {"w", "x"};
endclass

class plain;
  localparam int L = 11;
  static int from_local = L;
  static int chained = from_local * 2;
endclass

module top;
  initial begin
    if (sized#(4)::from_parameter == 4 && sized#(7)::from_parameter == 7
        && sized#(4)::doubled == 8 && sized#(7)::doubled == 14
        && sized#(4)::from_constant == 5 && sized#(7)::from_constant == 8
        && sized#(4)::from_static == 9 && sized#(7)::from_static == 15
        && sized#(4)::pattern === 8'hA5 && sized#(4)::name == "wx"
        && plain::from_local == 11 && plain::chained == 22
        && static_pkg::counter#(2)::start == 42)
      $display("PASS");
    else
      $display("FAIL %0d %0d %0d %0d %0d %0d %0d %0d %h %s %0d %0d %0d",
        sized#(4)::from_parameter, sized#(7)::from_parameter,
        sized#(4)::doubled, sized#(7)::doubled,
        sized#(4)::from_constant, sized#(7)::from_constant,
        sized#(4)::from_static, sized#(7)::from_static,
        sized#(4)::pattern, sized#(4)::name,
        plain::from_local, plain::chained,
        static_pkg::counter#(2)::start);
  end
endmodule
