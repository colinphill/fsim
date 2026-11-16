// fsim-expect-run: top=sv_stream_unpack_partial_element std=2017
// fsim-expect-text: a bit stream does not fill a whole number of elements of its variable-size target
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.4.14.4, 6.24.3: a variable-size target of a streaming
// unpack is resized as in a cast to take the bits left; when those are not
// a whole number of its elements the sizes differ, an error. Here the
// source is a queue, so the size is known only at run time: 24 bits into
// 5-bit elements.
module sv_stream_unpack_partial_element;
  byte source[$];
  bit [4:0] n[$];

  initial begin
    source = '{8'h12, 8'h34, 8'h56};
    {>>{n}} = {>>{source}};
    $display("PASS");
    $finish;
  end
endmodule
