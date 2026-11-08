// SPDX-License-Identifier: Apache-2.0
// A relative name resolves from the task or named block that holds the call
// outward (IEEE 1800-2023 23.8). `deep` is a child of the block outer inside
// task ta only, so $scope(deep) finds top.ta.outer.deep from there; the
// $showscopes after it shows an empty level. The generate block g is not a
// scope: $showscopes at top lists the task ta and the instance u, not g.
module leaf;
endmodule

module top;
  if (1) begin : g
    leaf gl ();
  end
  leaf u ();
  task ta;
    begin : outer
      begin : deep
      end
      $scope(deep);
      $showscopes;
    end
  endtask
  initial begin
    $showscopes;
    ta;
    $showscopes;
  end
endmodule
