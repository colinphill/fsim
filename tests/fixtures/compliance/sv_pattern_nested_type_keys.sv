// fsim: top=sv_pattern_nested_type_keys std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 10.9.2: a `type: value` key of a structure assignment
// pattern also sets the members of that type inside the structures and
// arrays its members hold, below the members named or typed at an outer
// level, and `default` sets the rest.
package sv_pattern_nested_type_keys_pkg;
  typedef enum logic [1:0] { BLACK, RED, GREEN } color_t;
  typedef struct { color_t pixels[4]; } line_t;
  typedef struct { line_t line[2]; int count; } screen_t;
endpackage
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_pattern_nested_type_keys;
  import sv_pattern_nested_type_keys_pkg::*;
  typedef struct {
    int A;
    struct { int B; shortint G; } BC1;
  } nested_t;
  nested_t value;
  screen_t screen;
  int failures;

  initial begin
    value = '{shortint: 10, default: 5};
    `CHECK("nested type key", value.A == 5 && value.BC1.B == 5
      && value.BC1.G == 10)
    screen = '{default: '0, color_t: RED};
    `CHECK("type key in an array of structures", screen.line[1].pixels[3] == RED
      && screen.line[0].pixels[0] == RED && screen.count == 0)
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
