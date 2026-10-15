// fsim: top=b59_typed_packed_pattern_context std=2017
// SPDX-License-Identifier: Apache-2.0
typedef struct packed {
  logic [7:0] high;
  logic [7:0] low;
} packed_value_t;

module b59_typed_packed_pattern_context;
  packed_value_t value;

  initial begin
    value = '{8'h12, 8'h34};
    if (value !== 16'h1234)
      $fatal(1, "packed struct member order changed");
    if (value !== packed_value_t'{high: 8'h12, low: 8'h34})
      $fatal(1, "typed assignment pattern did not use its declared type");
    if (value === packed_value_t'{high: 8'h12, default: '0})
      $fatal(1, "typed default pattern comparison matched incorrectly");
    if (!(value == 16'h1234))
      $fatal(1, "ordinary packed equality control failed");
    $display("PASS");
  end
endmodule
