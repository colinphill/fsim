// SPDX-License-Identifier: Apache-2.0
// System tasks and functions in post_randomize, which randomize()
// calls, read and change the run's state as they do in a module: $time,
// $stime and $realtime in the time unit of the class's scope (IEEE 1800-2023
// 20.3); $urandom and $urandom_range from the calling process's stream
// (18.13, 18.14, 20.15); $test$plusargs and $value$plusargs of the run's
// plusargs (21.6); $fopen, $fdisplay, $fwrite, $fflush and $fclose (21.3);
// $monitoroff (21.2.3); $assertoff (20.11); the trigger of an event property
// that a module process waits on (15.5.1); and $sampled of an automatic
// variable and, at time 0, of a property (16.5.1, 16.9.3).
`timescale 1ns / 1ps
class item;
  rand bit [1:0] x;
  int mode;
  int e;
  logic [63:0] w;
  real rt;
  event ev;
  logic [3:0] b = 4'b0110;
  int fd;
  string arg;
  function void post_randomize();
    int n;
    if (mode == 0) begin
      n = 9;
      $display("sampled %0d %b %b", $sampled(n), $sampled(b), $sampled(n + 1));
      $display("plusargs %0d %0d", $test$plusargs("fsim_bb5"), $test$plusargs("absent"));
      n = 3;
      e = $value$plusargs("count=%d", n);
      $display("value %0d %0d", e, n);
      e = $value$plusargs("name=%s", arg);
      $display("value %0d %s", e, arg);
      e = $value$plusargs("absent=%d", n);
      $display("value %0d %0d", e, n);
      e = $urandom_range(7, 7);
      n = $urandom_range(1, 5);
      $display("random %0d %b %0d", e, n inside {[1:5]}, $urandom_range(3, 9) inside {[3:9]});
      return;
    end
    w = $time;
    e = $stime;
    rt = $realtime;
    $display("time %0d %0d %f", w, e, rt);
    fd = $fopen("class_run_services.txt", "w");
    $display("open %0d", fd != 0);
    $fdisplay(fd, "line %0d", b);
    $fwrite(fd, "tail");
    $fflush(fd);
    $fclose(fd);
    $monitoroff;
    $assertoff;
    -> ev;
  endfunction
endclass

module top;
  item it = new;
  int fd;
  string text;
  logic [3:0] watch = 0;
  logic clk = 0;
  initial $monitor("monitor watch=%0d", watch);
  initial begin
    @(it.ev);
    $display("event %0d", $time);
  end
  always @(posedge clk) assert (watch == 0) else $error("assert watch=%0d", watch);
  initial begin
    $display("result=%0d", it.randomize());
    #5.5;
    it.mode = 1;
    $display("result=%0d", it.randomize());
    watch = 3;
    clk = 1;
    #1;
    fd = $fopen("class_run_services.txt", "r");
    while ($fgets(text, fd))
      $write("file %s", text);
    $display("");
    $fclose(fd);
  end
endmodule
