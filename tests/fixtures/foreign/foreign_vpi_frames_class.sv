// SPDX-License-Identifier: Apache-2.0
// The frames of static methods of a class (IEEE 1800-2023 37.43, 38.36.1),
// lines as in foreign_vpi_frames_host.sv: the statement each frame runs
// (vpiStmt), the calling statement of a nested frame (vpiOrigin) and cbStmt
// on the statements of a method body (37.31 vpiMethods, 37.41 vpiStmt).
//
//
// (no UVM)
package p;
  // (no import)
  class pkt;
    // (no utilities)
    function new(string name = "pkt");
      name = name;
    endfunction
    static function int twice(int x);
      int y;
      pkt cs;
      cs = null;
      y = x * 2;
      return y;
    endfunction
    static function int quad(int x, string who);
      int half;
      logic [7:0] code;
      string tag;
      code = 8'h5a;
      tag = {who, "!"};
      half = twice(x);
      $display("#c quad %s %0d %h %0d", tag, half, code,
        1);
      return twice(half);
    endfunction
  endclass
endpackage
module top;
  import p::*;
  int result;
  initial begin
    result = pkt::quad(3, "q");
    $display("#c result %0d", result);
  end
endmodule
