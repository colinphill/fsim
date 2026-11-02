// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 26.3 and 27.4: processes in loop generate scopes write
// package variables through the package scope: a counter, an unpacked array
// element, a queue, a string and an associative array.
package q;
  int passed = 0;
  int arr[4];
  int dq[$];
  string s = "";
  int assoc[string];
endpackage

module top;
  for (genvar i = 0; i < 3; i++) begin : g
    initial begin
      #(i + 1);
      q::passed++;
      q::arr[i] = i * 10 + 1;
      q::dq.push_back(i);
      q::s = {q::s, "x"};
      q::assoc[$sformatf("k%0d", i)] = i;
    end
  end
  initial begin
    #5;
    if (q::passed == 3 && q::arr[0] == 1 && q::arr[1] == 11
        && q::arr[2] == 21 && q::arr[3] == 0 && q::dq.size() == 3
        && q::dq[2] == 2 && q::s == "xxx" && q::assoc.num() == 3
        && q::assoc["k1"] == 1)
      $display("PASS");
    else
      $display("FAIL passed=%0d arr=%0d,%0d,%0d dq=%0d s=%s assoc=%0d",
        q::passed, q::arr[0], q::arr[1], q::arr[2], q::dq.size(), q::s,
        q::assoc.num());
  end
endmodule
