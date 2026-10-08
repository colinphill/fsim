// fsim: top=sv_streaming_unpack std=2017
// IEEE 1800-2017 11.4.14.3: a streaming concatenation as an assignment
// target unpacks the source into its operands.
module sv_streaming_unpack;
  logic [7:0] a, b;
  logic [3:0] n0, n1, n2, n3;
  logic [15:0] word;
  int failures = 0;

  initial begin
    {>>{a, b}} = 16'h1234;
    if (a !== 8'h12 || b !== 8'h34) begin
      $display("FAIL: left-to-right unpack a=%h b=%h", a, b);
      failures++;
    end
    {<<8{a, b}} = 16'h1234;
    if (a !== 8'h34 || b !== 8'h12) begin
      $display("FAIL: byte-reversed unpack a=%h b=%h", a, b);
      failures++;
    end
    word = 16'hABCD;
    {<<4{n0, n1, n2, n3}} = word;
    if ({n0, n1, n2, n3} !== 16'hDCBA) begin
      $display("FAIL: nibble-reversed unpack %h%h%h%h", n0, n1, n2, n3);
      failures++;
    end
    if (failures == 0)
      $display("PASS");
    $finish;
  end
endmodule
