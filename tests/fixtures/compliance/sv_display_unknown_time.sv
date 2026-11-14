// fsim: top=sv_display_unknown_time
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.11, 21.2.1.3: time is a 4-state integer type, so a time
// value with unknown bits displays as an integral decimal value does: all
// bits x is x, all bits z is z, some bits x is X and some bits z (none x) is
// Z, under %d and under %t alike; %t right-justifies it in the $timeformat
// minimum field width (20 by default, here then 8), without the unit suffix
// (owner decision 2026-11-13: 21.2.1.3 gives no %t rule).
module sv_display_unknown_time;
  time unset, all_z, some_x, some_z, known;
  reg [63:0] wide_x;
  initial begin
    all_z = 'z;
    some_x = 64'h0000_0000_0000_000x;
    some_z = 64'h0000_0000_0000_000z;
    known = 42;
    wide_x = 'x;
    if ($sformatf("%0d", unset) == "x" && $sformatf("%0t", unset) == "x"
        && $sformatf("%0d", all_z) == "z" && $sformatf("%0t", all_z) == "z"
        && $sformatf("%0d", some_x) == "X" && $sformatf("%0t", some_x) == "X"
        && $sformatf("%0d", some_z) == "Z" && $sformatf("%0t", some_z) == "Z"
        && $sformatf("%0t", wide_x) == "x" && $sformatf("%0t", known) == "42"
        && $sformatf("%t", unset) == "                   x") begin
      $timeformat(0, 0, " s", 8);
      if ($sformatf("%t", unset) == "       x")
        $display("PASS");
      else
        $display("FAIL [%t]", unset);
    end
    else
      $display("FAIL [%0d|%0t] [%0d|%0t] [%0d|%0t] [%0d|%0t] [%0t] [%0t]",
        unset, unset, all_z, all_z, some_x, some_x, some_z, some_z, wide_x,
        known);
  end
endmodule
