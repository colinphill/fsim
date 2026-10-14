// fsim: top=sv_real_to_time_assignment_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.12.2: real assignments to integral types round to the
// nearest integer, with exact ties rounded away from zero.
module sv_real_to_time_assignment_batch58;
  timeunit 1ns;
  timeprecision 10ps;

  real real_source;
  realtime realtime_source;
  shortreal shortreal_source;
  time time_value;
  longint signed signed_value;
  longint unsigned integral_source;
  integer matched;

  initial begin
    // Exercise the real result produced by the scanf2 %t witness before its
    // assignment to the unsigned 64-bit time type.
    $timeformat(-3, 2, "ms", 10);
    matched = $sscanf("8.125", "%t", real_source);
    if (matched != 1 || real_source != 8130000.0)
      $fatal(1, "the %t source value must be scaled before assignment");
    time_value = real_source;
    if (time_value !== 64'd8130000)
      $fatal(1, "real-to-time assignment must convert the numeric value");

    realtime_source = 8130000.0;
    time_value = realtime_source;
    if (time_value !== 64'd8130000)
      $fatal(1, "realtime-to-time assignment");

    shortreal_source = 8130000.0;
    time_value = shortreal_source;
    if (time_value !== 64'd8130000)
      $fatal(1, "shortreal-to-time assignment");

    real_source = 12.49;
    time_value = real_source;
    if (time_value !== 64'd12)
      $fatal(1, "positive real fractional rounding below half");
    real_source = 12.5;
    time_value = real_source;
    if (time_value !== 64'd13)
      $fatal(1, "positive real tie rounds away from zero");

    real_source = -2.49;
    signed_value = real_source;
    if (signed_value !== -64'sd2)
      $fatal(1, "negative real fractional rounding above negative half");
    real_source = -2.5;
    signed_value = real_source;
    if (signed_value !== -64'sd3
        || signed_value[63:0] !== 64'hfffffffffffffffd)
      $fatal(1, "negative real tie rounds away from zero");

    integral_source = 64'h0123456789abcdef;
    time_value = integral_source;
    if (time_value !== 64'h0123456789abcdef)
      $fatal(1, "integral-to-time assignment preserves all 64 bits");

    $display("PASS");
    $finish;
  end
endmodule
