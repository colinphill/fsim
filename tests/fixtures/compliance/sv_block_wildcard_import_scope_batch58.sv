// fsim: top=b58_block_wildcard_import_scope std=2017
// SPDX-License-Identifier: Apache-2.0
`define B58_IMPORT_SOURCE_ORDER_ALIAS imported_p
package b58_block_scope_pkg;
  parameter int imported_p = 7;
  parameter int local_override = 99;
  typedef logic [3:0] nibble_t;
  nibble_t imported_v = 0;

  task automatic set_value(input nibble_t value);
    imported_v = value;
  endtask
  function automatic int get_value();
    return imported_v;
  endfunction
endpackage

module b58_block_wildcard_import_scope;
  localparam int imported_p = 100;
  logic [7:0] imported_v;

  initial begin : local_scope
    int before_import = imported_p;
    import b58_block_scope_pkg::*;
    nibble_t local_word;
    int local_override;

    local_override = 21;
    local_word = 4'd2;
    imported_v = 8'd2;
    set_value(local_word);
    if (before_import == 100 && `B58_IMPORT_SOURCE_ORDER_ALIAS == 7
        && $bits(imported_v) == 4
        && imported_v == 2 && get_value() == 2 && local_override == 21)
      $display("PASS");
    else
      $fatal(1, "block wildcard import or local shadowing failed");
  end
endmodule
