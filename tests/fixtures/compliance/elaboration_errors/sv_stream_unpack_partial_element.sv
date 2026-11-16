// fsim-expect: FSIM-ELAB-SVSTREAM-003 top=top
// fsim-expect-text: are not a whole number of its 4-bit elements
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.4.14.4, 6.24.3: a variable-size target of a streaming
// unpack is resized as in a cast to take the bits the fixed-size targets
// leave; 6 bits are not a whole number of 4-bit elements, a size difference
// found at elaboration because every width is constant.
module top;
  bit [3:0] n[$];

  initial begin
    {>>{n}} = 6'b101011;
    $display("n has %0d elements", n.size());
  end
endmodule
