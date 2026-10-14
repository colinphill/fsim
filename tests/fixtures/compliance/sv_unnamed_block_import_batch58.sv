// fsim: top=b58_unnamed_block_import std=2017
// SPDX-License-Identifier: Apache-2.0
package b58_unnamed_block_import_pkg;
  localparam int imported_value = 11;
endpackage

package b58_if_block_import_pkg;
  localparam int if_imported_value = 13;
endpackage

package b58_loop_block_import_pkg;
  localparam int loop_imported_value = 17;
endpackage

module b58_unnamed_block_import;
  initial begin
    import b58_unnamed_block_import_pkg::*;
    if (imported_value != 11)
      $fatal(1, "unnamed initial-block import was lost");
    if (1) begin
      import b58_if_block_import_pkg::*;
      if (if_imported_value != 13)
        $fatal(1, "unnamed if-block import was lost");
    end
    repeat (1) begin
      import b58_loop_block_import_pkg::*;
      if (loop_imported_value != 17)
        $fatal(1, "unnamed loop-block import was lost");
    end
    $display("PASS");
    $finish;
  end
endmodule
