// fsim: top=b58_bits_anonymous_packed_struct std=2017
// SPDX-License-Identifier: Apache-2.0
module b58_bits_anonymous_packed_struct;
  struct packed { int x; shortint y; } value;

  initial begin
    if ($bits(struct packed { int x; shortint y; }) != 48)
      $fatal(1, "$bits of anonymous packed struct data type");
    if ($bits(value) != 48)
      $fatal(1, "$bits of packed struct expression");
    if ($bits(union packed { byte bytes; logic [7:0] bits; }) != 8)
      $fatal(1, "$bits of ordinary packed union data type");
    if ($bits(value + 48'd1) != 48)
      $fatal(1, "$bits of packed struct expression result");
    if ($bits(int) != 32)
      $fatal(1, "$bits of existing built-in data type argument");
    $display("PASS");
    $finish;
  end
endmodule
