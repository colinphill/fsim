// SPDX-License-Identifier: Apache-2.0
// $scope, $showscopes and $showvars (IEEE 1800-2023 D.11-D.13). Expected
// output in scope_tasks.expected, derived by hand:
//
// The interactive scope starts at the first top-level module, top, so the
// first $showscopes lists top's one scope, the instance u. With a nonzero
// argument it lists everything in or below top, sorted by hierarchical name.
// $scope(u) selects the instance, so $showscopes then lists its function fn,
// its instance lf, its task tk and its named block blk; $showvars lists the
// reg and net objects of u by name. A bit-select argument names the whole
// vector. $scope(u.tk) selects the task (its named block inner is the only
// scope in it), $scope(u.blk) the named block (none below it), and
// $scope(top) goes back. The generate block u.g is not listed, but the
// instance u.g.gl inside it is, by $showscopes(1) from top.
module leaf;
  reg [3:0] lr;
endmodule

module child;
  reg [7:0] r;
  reg [15:0] cnt;
  wire [3:0] n = 4'b1010;
  real rv;
  leaf lf ();
  // A generate block is not a scope for $scope or $showscopes (D.11, D.12);
  // the instance inside it is below top.u.
  if (1) begin : g
    leaf gl ();
  end
  task tk;
    begin : inner
    end
  endtask
  function fn;
    input x;
    fn = x;
  endfunction
  initial begin : blk
    r = 8'h2a;
    cnt = 16'd7;
    rv = 2.5;
  end
endmodule

module top;
  reg [7:0] a;
  child u ();
  initial begin
    a = 8'b00000101;
    #1;
    $showscopes;
    $showscopes(1);
    $scope(u);
    $showscopes;
    $showvars;
    $showvars(u.r[3], a, u.cnt);
    $scope(u.tk);
    $showscopes;
    $scope(u.blk);
    $showscopes;
    $scope(top);
    $showvars;
    $display("done");
  end
endmodule
