// fsim: top=sv_time_without_timescale std=2017
// IEEE 1800-2017 20.3: $time, $stime and $realtime in a design unit without
// a timescale return time in the unit used by its delays.
module sv_time_without_timescale;
  int failures = 0;
  initial begin
    #10;
    if ($time != 10) failures++;
    if (!($time < 30)) failures++;
    while ($time < 30) #1;
    if ($time != 30) failures++;
    if ($stime != 30) failures++;
    if ($realtime != 30.0) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL failures=%0d", failures);
  end
endmodule
