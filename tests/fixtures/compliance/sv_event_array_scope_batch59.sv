// fsim: top=sv_event_array_scope_batch59 std=2017
// SPDX-License-Identifier: Apache-2.0
module b59_event_scope_leaf #(parameter integer ACTIVE = 0);
  event ev[2];
  integer parent_hits = 0;

  for (genvar i = 0; i < 2; ++i) begin : parent_waiters
    initial begin
      @(ev[i]);
      parent_hits = parent_hits + 1;
    end
  end

  for (genvar g = 0; g < 2; ++g) begin : shadowed
    event ev[2];
    integer local_hits = 0;
    for (genvar j = 0; j < 2; ++j) begin : nested_waiters
      initial begin
        @(ev[j]);
        local_hits = local_hits + 1;
      end
    end
    initial begin
      #2;
      if (g == ACTIVE)
        ->ev[0];
      #1;
      if (local_hits != (g == ACTIVE ? 1 : 0))
        $fatal(1, "generated event arrays crossed sibling or instance scopes");
    end
  end

  initial begin
    #1;
    ->ev[ACTIVE];
    #1;
    if (parent_hits != 1)
      $fatal(1, "ancestor event array did not preserve instance identity");
    ->ev[1 - ACTIVE];
    #1;
    if (parent_hits != 2)
      $fatal(1, "ancestor event array elements did not wake independently");
  end
endmodule

module sv_event_array_scope_batch59;
  b59_event_scope_leaf #(.ACTIVE(0)) first();
  b59_event_scope_leaf #(.ACTIVE(1)) second();
  initial begin
    #5;
    $display("PASS");
    $finish;
  end
endmodule
