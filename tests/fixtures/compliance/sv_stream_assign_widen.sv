// fsim: top=sv_stream_assign_widen std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.4.14.3: a streaming concatenation assigned to a queue
// or dynamic array resizes it to the fewest elements as wide as the
// stream, and the stream is widened with zero bits on the right to fill
// the last element.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_stream_assign_widen;
  bit source_bits[];
  int unsigned words[];
  byte bytes[$];
  int failures;

  initial begin
    source_bits = new[37];
    source_bits[0] = 1;
    source_bits[2] = 1;
    source_bits[5] = 1;
    source_bits[31] = 1;
    source_bits[36] = 1;
    words = {>>{source_bits}};
    `CHECK("left stream size", words.size() == 2)
    `CHECK("left stream words", words[0] == 32'ha4000001
      && words[1] == 32'h08000000)
    words = {<<{source_bits}};
    `CHECK("right stream words", words.size() == 2
      && words[0] == 32'h84000001 && words[1] == 32'h28000000)
    bytes = {>>{12'habc}};
    `CHECK("queue of bytes", bytes.size() == 2 && bytes[0] == 8'hab
      && bytes[1] == 8'hc0)
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
