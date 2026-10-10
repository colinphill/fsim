// fsim: top=sv_generate_references
// IEEE 1800-2017 27.4 and 23.6: variables and instance parameters of loop
// generate blocks referenced by constant index (`row[1].col[2].tap`), and
// 23.8/23.10: a defparam whose first name is the enclosing generate block.
module leaf;
  parameter int P = 0;
endmodule

module sv_generate_references;
  logic [7:0] source = 8'b1010_0110;

  for (genvar r = 0; r < 2; r = r + 1) begin : row
    logic [3:0] nibble;
    for (genvar c = 0; c < 4; c = c + 1) begin : col
      wire tap = source[r * 4 + c];
    end
    assign nibble = {col[3].tap, col[2].tap, col[1].tap, col[0].tap};
  end

  for (genvar i = 0; i < 2; i = i + 1) begin : inst
    leaf m();
    defparam inst[i].m.P = 10 + i;
  end

  initial begin
    int failures = 0;
    #1;
    if (row[0].nibble !== 4'b0110) failures++;
    if (row[1].nibble !== 4'b1010) failures++;
    if (row[1].col[3].tap !== 1'b1) failures++;
    if (row[0].col[0].tap !== 1'b0) failures++;
    if (inst[0].m.P != 10) failures++;
    if (inst[1].m.P != 11) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
