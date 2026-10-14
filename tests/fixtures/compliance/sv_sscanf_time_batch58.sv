// fsim: top=sv_sscanf_time_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 21.3.4.3 and Table 21-8: %t parses a real field, then
// scales and rounds it using the active (or default) $timeformat profile.
module sv_sscanf_time_batch58;
  timeunit 1ns;
  timeprecision 10ps;

  real local_time;
  realtime realtime_time;
  shortreal short_time;
  integer matched;
  integer value;
  integer file_handle;

  initial begin
    // The default profile's unit is the global 10 ps precision, not this
    // module's 1 ns timeunit. One input unit therefore becomes 0.01 ns.
    matched = $sscanf("012345678925", "%t", local_time);
    if (matched != 1 || local_time != 123456789.25)
      $fatal(1, "default global-precision %t scaling");

    $timeformat(-3, 2, "ms", 10);
    matched = $sscanf("8.125", "%t", local_time);
    if (matched != 1 || local_time != 8130000.0)
      $fatal(1, "$timeformat unit scaling and precision rounding");

    $timeformat(-9, 3, "ns", 20);
    matched = $sscanf("-2.2566789 17", "%6t %d", local_time, value);
    if (matched != 2 || local_time != -2.256 || value != 6789)
      $fatal(1, "%t field width and following conversion");

    matched = $sscanf("+2.e+16789", "%6t %d", local_time, value);
    if (matched != 2 || local_time != 20.0 || value != 6789)
      $fatal(1, "%t exponent and following conversion");

    matched = $sscanf("123 1.0 5.125", "%d %*t %t", value, realtime_time);
    if (matched != 2 || value != 123 || realtime_time != 5.125)
      $fatal(1, "suppressed %t consumes without assignment");

    matched = $sscanf("3.125", "%t", short_time);
    if (matched != 1 || short_time != 3.125)
      $fatal(1, "shortreal %t target");

    file_handle = $fopen("sv_sscanf_time_batch58.txt", "w");
    if (file_handle == 0)
      $fatal(1, "%t file-scan witness could not open output");
    $fwrite(file_handle, "file 4.5 23");
    $fclose(file_handle);
    file_handle = $fopen("sv_sscanf_time_batch58.txt", "r");
    if (file_handle == 0)
      $fatal(1, "%t file-scan witness could not open input");
    matched = $fscanf(file_handle, "file %t %d", realtime_time, value);
    $fclose(file_handle);
    if (matched != 2 || realtime_time != 4.5 || value != 23)
      $fatal(1, "$fscanf %t uses the current time format");

    $display("PASS");
    $finish;
  end
endmodule
