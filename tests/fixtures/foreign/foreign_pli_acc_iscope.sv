// SPDX-License-Identifier: Apache-2.0
// The interactive scope (IEEE 1364-2001 23.46, 23.94): before any setting it
// is the first top-level module, not the scope of the system task that asks;
// acc_set_interactive_scope changes it and acc_handle_interactive_scope
// reports the change.
//
// $scope (IEEE 1800-2023 D.11) sets the same interactive scope: a task, a
// named block and an instance in turn, then the top module. Each change is
// heard by the misctf routine as reason_scope (1364-2001 misctf reasons;
// 1800-2023 38.36.3 cbInteractiveScopeChange) and seen by ACC at once.
module sub;
  reg [3:0] r;
  task tk;
    begin
    end
  endtask
  initial begin : blk
    r = 4'd5;
  end
  initial #1 $pli_acc_iscope;
endmodule

module top;
  sub sub ();
  initial begin
    #2 $scope(sub.blk);
    $pli_acc_report;
    #1 $scope(sub.tk);
    $pli_acc_report;
    #1 $scope(sub);
    $pli_acc_report;
    #1 $scope(top);
    $pli_acc_report;
  end
endmodule
