// fsim: top=b59_block_imported_typedef_visibility std=2017
// SPDX-License-Identifier: Apache-2.0
package b59_unit_type_pkg;
  typedef logic [7:0] value_t;
endpackage

package b59_outer_block_type_pkg;
  typedef logic signed [3:0] value_t;
endpackage

package b59_inner_block_type_pkg;
  typedef logic [1:0] value_t;
endpackage

package b59_packed_alias_shape_pkg;
  typedef logic signed [3:0] nibble_t;
  typedef nibble_t [1:0] pair_t;
endpackage

module b59_block_imported_typedef_visibility;
  import b59_unit_type_pkg::*;
  import b59_packed_alias_shape_pkg::*;
  typedef value_t unit_import_t;
  unit_import_t unit_value;

  initial begin : outer_scope
    import b59_outer_block_type_pkg::*;
    typedef value_t outer_t;
    outer_t outer_value;
    nibble_t signed_alias_value;
    pair_t pair_value;
    nibble_t [2:0] packed_value;
    logic signed [5:2] explicit_signed_value;
    logic [5:2] explicit_unsigned_value;
    unit_value = 8'hFF;
    signed_alias_value = 4'hF;
    pair_value = 8'hB2;
    packed_value = 12'hB2F;
    explicit_signed_value = 4'hF;
    explicit_unsigned_value = 4'hF;
    outer_value = 4'hB;
    if ($bits(outer_value) != 4 || outer_value !== 4'hB
        || !(outer_value < 0))
      $fatal(1, "current block import did not shadow the unit import");
    begin : inherited_scope
      typedef value_t inherited_t;
      inherited_t inherited_value;
      inherited_value = 4'hA;
      if ($bits(inherited_value) != 4 || inherited_value !== 4'hA
          || !(inherited_value < 0))
        $fatal(1, "nearest block import did not shadow the unit import");
    end
    begin : nearest_scope
      import b59_inner_block_type_pkg::*;
      typedef value_t nearest_t;
      nearest_t nearest_value;
      nearest_value = 2'h2;
      if ($bits(nearest_value) != 2 || nearest_value !== 2'h2
          || nearest_value < 0)
        $fatal(1, "nearest nested block import was not selected");
      if ($bits(outer_value) !== 32'd4 || (outer_value < 0) !== 1'b1)
        $fatal(1, "outer declaration type changed under nested import");
    end
    if ($bits(unit_value) != 8 || unit_value < 0)
      $fatal(1, "unit-level type import control failed");
    if ($bits(signed_alias_value) != 4 || !(signed_alias_value < 0))
      $fatal(1, "selected typedef signedness was not retained");
    if ($bits(pair_value) != 8 || pair_value[1] !== 4'hB
        || pair_value[0] !== 4'h2)
      $fatal(1, "typedef packed dimensions were not retained");
    if ($bits(packed_value) != 12 || packed_value[2] !== 4'hB
        || packed_value[0] !== 4'hF)
      $fatal(1, "use-site packed dimensions were not retained");
    if ($bits(explicit_signed_value) != 4
        || !(explicit_signed_value < 0)
        || $bits(explicit_unsigned_value) != 4
        || explicit_unsigned_value < 0)
      $fatal(1, "explicit built-in range or signedness was not retained");
    $display("PASS");
  end
endmodule
