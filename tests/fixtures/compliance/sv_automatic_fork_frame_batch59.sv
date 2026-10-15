// fsim: top=sv_automatic_fork_frame_batch59 std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_automatic_fork_frame_batch59;
  integer direct4 = 0;
  integer direct5 = 0;
  integer nested4 = 0;
  integer nested5 = 0;
  integer sequential4 = 0;
  integer sequential5 = 0;

  task automatic delayed_direct(input integer index_value);
    #2;
    if (index_value == 4)
      direct4 = direct4 + 1;
    else if (index_value == 5)
      direct5 = direct5 + 1;
    else
      $fatal(1, "direct fork task received an unexpected index");
  endtask

  task automatic delayed_leaf(input integer index_value);
    #2;
    if (index_value == 4)
      nested4 = nested4 + 1;
    else if (index_value == 5)
      nested5 = nested5 + 1;
    else
      $fatal(1, "nested fork task received an unexpected index");
  endtask

  task automatic delayed_outer(input integer index_value);
    delayed_leaf(index_value);
  endtask

  task automatic delayed_sequential(input integer index_value);
    #1;
    if (index_value == 4)
      sequential4 = sequential4 + 1;
    else if (index_value == 5)
      sequential5 = sequential5 + 1;
    else
      $fatal(1, "sequential task received an unexpected index");
  endtask

  initial begin
    fork
      delayed_direct(4);
      delayed_direct(5);
    join_none
  end

  initial begin
    fork
      delayed_outer(4);
      delayed_outer(5);
    join_none
  end

  // This process has no fork and verifies that the existing native-frame
  // elision remains valid for sequential automatic calls.
  initial begin
    delayed_sequential(4);
    delayed_sequential(5);
    if (sequential4 !== 1 || sequential5 !== 1)
      $fatal(1, "sequential automatic task frames were not preserved");
  end

  initial begin
    #5;
    if (direct4 !== 1 || direct5 !== 1)
      $fatal(1, "forked automatic task frames were not isolated");
    if (nested4 !== 1 || nested5 !== 1)
      $fatal(1, "nested automatic task frames were not isolated");
    if (sequential4 !== 1 || sequential5 !== 1)
      $fatal(1, "no-fork sequential automatic task control failed");
    $display("PASS");
    $finish;
  end
endmodule
