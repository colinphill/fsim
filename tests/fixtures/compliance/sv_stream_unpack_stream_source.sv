// fsim: top=sv_stream_unpack_stream_source std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.4.14.3, 11.4.14.4: the source of a streaming unpack may
// itself be a streaming concatenation of queues and arrays; its bit stream
// holds every operand's bits, however many elements they have, and a
// variable-size target takes all of them.
module sv_stream_unpack_stream_source;
  int failures;
  byte source[$];
  bit [3:0] head;

  task automatic check(input string what, input bit ok);
    if (!ok) begin
      $display("FAIL %s", what);
      failures++;
    end
  endtask

  initial begin : main
    bit [3:0] n[$];
    byte b[];
    shortint s[$];
    source = '{8'h12, 8'h34, 8'h56};
    {>>{n}} = {>>{source}};
    check("queue of nibbles", n.size() == 6 && n[0] == 4'h1 && n[5] == 4'h6);
    {>>{b}} = {<<8{source}};
    check("reversed bytes", b.size() == 3 && b[0] == 8'h56 && b[2] == 8'h12);
    {>>{head, n}} = {>>{source, 4'hF}};
    check("fixed target first", head == 4'h1 && n.size() == 6
      && n[0] == 4'h2 && n[5] == 4'hF);
    source.push_back(8'h78);
    {>>{s}} = {>>{source}};
    check("shortints", s.size() == 2 && s[0] == 16'h1234 && s[1] == 16'h5678);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
