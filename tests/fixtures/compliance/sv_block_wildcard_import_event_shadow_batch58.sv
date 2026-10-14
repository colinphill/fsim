// fsim: top=b58_block_import_event_shadow std=2017
// SPDX-License-Identifier: Apache-2.0
package b58_block_import_event_pkg;
  event wake;
endpackage

module b58_block_import_event_shadow;
  event wake;
  time imported_wake_time;
  time local_wake_time;

  initial begin : imported_waiter
    import b58_block_import_event_pkg::*;
    @wake;
    imported_wake_time = $time;
  end

  initial begin : local_waiter
    @wake;
    local_wake_time = $time;
  end

  initial begin
    #1 -> b58_block_import_event_pkg::wake;
    #2 -> wake;
    #1;
    if (imported_wake_time == 1 && local_wake_time == 3)
      $display("PASS");
    else
      $fatal(1, "block imported event shadowing failed: imported=%0t local=%0t",
             imported_wake_time, local_wake_time);
    $finish;
  end
endmodule
