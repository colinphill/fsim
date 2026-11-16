// fsim: top=sv_stream_unpack_resize std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.4.14.4: a variable-size array or queue as the target
// of a streaming concatenation is resized to take all the bits left after
// the fixed-size targets that follow it; a fixed-size target before it
// takes its bits first. Module and block variables, queues and dynamic
// arrays, either stream direction.
module sv_stream_unpack_resize;
  int failures;
  bit [3:0] m[$];
  int dm[];

  task automatic check(input string what, input bit ok);
    if (!ok) begin
      $display("FAIL %s", what);
      failures++;
    end
  endtask

  initial begin : main
    bit [3:0] n[$];
    int d[];
    byte b[$];
    bit [7:0] a;
    {>>{n}} = 16'hBEEF;
    check("queue of nibbles", n.size() == 4 && n[0] == 4'hB && n[3] == 4'hF);
    {>>{d}} = 64'h11112222_33334444;
    check("dynamic array of ints",
      d.size() == 2 && d[0] == 32'h11112222 && d[1] == 32'h33334444);
    {>>{m}} = 12'hABC;
    check("module queue", m.size() == 3 && m[2] == 4'hC);
    {>>{dm}} = 96'h1_00000002_00000003;
    check("module dynamic array", dm.size() == 3 && dm[2] == 3);
    {>>{a, b}} = 32'h01020304;
    check("fixed target first", a == 8'h01 && b.size() == 3 && b[2] == 8'h04);
    {<<8{b}} = 24'hAABBCC;
    check("byte reversal", b.size() == 3 && b[0] == 8'hCC && b[2] == 8'hAA);
    #1;
    {>>{d}} = 32'd5;
    check("after a wait", d.size() == 1 && d[0] == 5);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
