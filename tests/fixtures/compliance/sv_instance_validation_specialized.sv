// fsim: top=sv_instance_validation_specialized std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 25.10, 27.5, 6.22.3: each instance is checked with its own
// parameters. A parameter read through an interface port takes the
// connected instance's value; the generate block a decided condition
// leaves out is not elaborated, so its selects and its recursive
// instantiation are not checked.
interface sv_instance_validation_specialized_if #(parameter int W = 4);
  logic [W-1:0] data;
endinterface

module sv_instance_validation_specialized_leaf(
    sv_instance_validation_specialized_if port);
  localparam int N = port.W;
  typedef struct { logic [N-1:0] f; } field_t;
  field_t s;
  logic [N-1:0] copy;
  initial begin
    s = '{f: '1};
    copy = s.f;
  end
endmodule

module sv_instance_validation_specialized_rec #(parameter int D = 3)
    (output int depth);
  if (D > 0) begin : more
    sv_instance_validation_specialized_rec #(D - 1) child(depth);
  end else begin : stop
    assign depth = 7;
  end
endmodule

module sv_instance_validation_specialized_chain #(parameter int DEPTH = 1);
  logic [DEPTH-1:0] r = '1;
  if (DEPTH > 1) begin : wide
    always_comb r[DEPTH-1:1] = r[DEPTH-2:0];
  end
endmodule

module sv_instance_validation_specialized;
  sv_instance_validation_specialized_if #(8) ifc();
  sv_instance_validation_specialized_leaf leaf(ifc);
  int depth;
  sv_instance_validation_specialized_rec #(3) rec(depth);
  sv_instance_validation_specialized_chain #(1) chain();
  int failures;

  initial begin
    #1;
    if ($bits(leaf.copy) != 8 || leaf.copy !== 8'hff) begin
      $display("FAIL interface port parameter %0d %h", $bits(leaf.copy), leaf.copy);
      failures++;
    end
    if (depth != 7) begin
      $display("FAIL recursion depth=%0d", depth);
      failures++;
    end
    if (chain.r !== 1'b1) begin
      $display("FAIL chain");
      failures++;
    end
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
