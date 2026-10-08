// fsim: top=sv_port_width_mismatch std=2017
// IEEE 1800-2017 23.3.3: each port connection is a continuous assignment
// from source to sink under the assignment-compatibility rules of 6.22.3, so
// an actual of a different packed width is zero-extended or truncated.
module narrow_leaf(input logic [3:0] a, output logic [3:0] y);
  assign y = a + 4'd1;
endmodule

module sv_port_width_mismatch;
  logic [7:0] wide_in = 8'hA7;
  logic [1:0] narrow_in = 2'b10;
  wire [7:0] wide_out;
  wire [1:0] narrow_out;
  narrow_leaf truncating (.a(wide_in), .y(wide_out));
  narrow_leaf padding (.a(narrow_in), .y(narrow_out));
  initial begin
    #1;
    // a = 4'h7 (truncated); y = 4'h8 zero-extended to 8 bits.
    // a = 4'b0010 (zero-extended); y = 4'b0011 truncated to 2 bits.
    if (wide_out === 8'h08 && narrow_out === 2'b11) $display("PASS");
    else $display("FAIL wide_out=%h narrow_out=%b", wide_out, narrow_out);
  end
endmodule
