// SPDX-License-Identifier: Apache-2.0
// Immediate assertions in pre_randomize and post_randomize and in the
// methods they call (IEEE 1800-2023 16.3): a failure runs the else action,
// or reports $error with the default message, and the callback goes on, so
// randomize() returns 1 (18.6.1, 18.6.3). An X condition fails. assume and
// cover are checked like assert, cover having no fail action. The assertion
// controls apply (20.11): after $assertoff nothing is checked, after
// $assertcontrol(9) (FailOff) a failure runs no action, and $asserton and
// $assertcontrol(8) restore them.
class item;
  rand bit [3:0] x;
  int pre_count;
  int post_count;
  logic flag;

  function void check(int step);
    assert (step != 2) $display("check %0d passed", step);
    else $display("check %0d failed", step);
  endfunction

  function void pre_randomize();
    assert (1'b0);
    pre_count = pre_count + 1;
  endfunction

  function void post_randomize();
    assert (flag) $display("flag is set");
    assume (pre_count > 0) $display("assume passed");
    cover (pre_count == 1) $display("covered once");
    check(1);
    check(2);
    post_count = post_count + 1;
  endfunction
endclass

module top;
  item it;
  int result;

  initial begin
    it = new;
    result = it.randomize();
    $display("first: result=%0d pre=%0d post=%0d", result, it.pre_count,
             it.post_count);
    $assertoff;
    result = it.randomize();
    $display("off: result=%0d pre=%0d post=%0d", result, it.pre_count,
             it.post_count);
    $asserton;
    $assertcontrol(9);
    result = it.randomize();
    $display("fail off: result=%0d pre=%0d post=%0d", result, it.pre_count,
             it.post_count);
    $assertcontrol(8);
    it.flag = 1'b1;
    result = it.randomize();
    $display("on: result=%0d pre=%0d post=%0d", result, it.pre_count,
             it.post_count);
  end
endmodule
