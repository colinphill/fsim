// SPDX-License-Identifier: Apache-2.0
// Design for the statement-label VPI plug-in (IEEE 1800-2023 9.3.5, 12.7.1,
// 12.7.3, 27.6, 37.12): labeled for and foreach loops, a labeled statement,
// an unlabeled for loop whose implicit block is an unnamed scope, and the
// same scopes in the procedures of a generate block.
module top;
  int arr[3];
  int a2[2][3];
  initial begin
    lbl: for (int k = 0; k < 3; k++) begin : lb
      static int w = 0;
      w = w + k;
    end
    fl: foreach (arr[i]) begin : blk
      static int v = 0;
      v++;
    end
    m2: foreach (a2[i, j]) begin : b2
      static int c = 0;
      c++;
      if (i == 1) break;
    end
    st: $display("labeled statement");
    for (int u = 0; u < 1; u++) begin : inner
      static int h = 0;
      h++;
    end
  end
  if (1) begin : g
    initial begin
      begin
        static int ub = 1;
      end
      begin : nb
        static int s = 2;
      end
      gl: for (int k = 0; k < 2; k++) begin : glb
        static int gw = 0;
        gw++;
      end
      foreach (arr[i]) begin : gfb
        static int gv = 0;
        gv++;
      end
    end
  end
endmodule
