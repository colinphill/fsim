// fsim: top=b59_typed_packed_pattern_edges std=2017
// SPDX-License-Identifier: Apache-2.0
typedef union packed {
  logic [7:0] octet;
  logic [7:0] raw;
} choice_t;

typedef struct packed signed {
  logic [7:0] payload;
} signed_byte_t;

typedef struct packed {
  logic [7:0] payload;
} shadow_t;

module b59_typed_packed_pattern_edges;
  initial begin
    if (choice_t'{octet: 8'hA5} !== 8'hA5)
      $fatal(1, "typed packed-union member pattern failed");
    if (signed_byte_t'{payload: 8'h80} !== 16'shFF80)
      $fatal(1, "signed packed-structure pattern did not sign-extend");
    if (shadow_t'{payload: 8'h5A} !== 8'h5A)
      $fatal(1, "module-scope typed pattern control failed");
    begin : local_type_scope
      typedef struct packed {
        logic [3:0] payload;
      } shadow_t;
      if (shadow_t'{payload: 4'hD} !== 4'hD)
        $fatal(1, "local typedef did not shadow the module typedef");
    end
    $display("PASS");
    $finish;
  end
endmodule
