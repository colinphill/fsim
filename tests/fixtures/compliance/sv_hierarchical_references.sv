// fsim: top=sv_hierarchical_references std=2017
// IEEE 1800-2017 23.6-23.8: downward, upward, and $root hierarchical
// references for reads and writes.
module sv_hierarchical_references_leaf(input logic [31:0] value);
  wire [31:0] doubled = value * 2;
  logic [7:0] scratch = 8'd3;
  logic [31:0] seen_parent;
  initial #1 seen_parent = sv_hierarchical_references.base;
endmodule

module sv_hierarchical_references;
  logic [31:0] base = 32'd7;
  sv_hierarchical_references_leaf leaf (.value(base));
  initial begin
    #2;
    if (leaf.doubled !== 32'd14) $display("FAIL doubled=%0d", leaf.doubled);
    else if ($root.sv_hierarchical_references.leaf.scratch !== 8'd3)
      $display("FAIL scratch");
    else if (leaf.seen_parent !== 32'd7) $display("FAIL upward");
    else begin
      leaf.scratch = 8'd42;
      #1;
      if (leaf.scratch === 8'd42) $display("PASS");
      else $display("FAIL write");
    end
  end
endmodule
