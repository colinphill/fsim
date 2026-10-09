// fsim: top=sv_packed_multidim std=2017
// IEEE 1800-2017 7.4.5: element, range and indexed part-selects of the
// outer dimension of a multidimensional packed array select whole elements;
// an element can itself be part-selected. Reads and writes, constant and
// dynamic indices, blocking, nonblocking and continuous assignments.
module sv_packed_multidim;
  logic [3:0][7:0] w;
  logic [3:0][7:0] n;
  logic [2:0][1:0] in;
  logic [1:0][1:0] out;
  wire [1:0][3:0] c;
  logic [3:0] p = 4'h9, q = 4'h6;
  assign c[1] = p;
  assign c[0] = q;

  initial begin
    int failures = 0;
    int i;
    logic [1:0] j;
    w = 32'h44332211;
    if (w[2] !== 8'h33 || w[3-:2] !== 16'h4433 || w[0+:3] !== 24'h332211
        || w[2:1] !== 16'h3322 || w[1][3:0] !== 4'h2) failures++;
    i = 1;
    j = 2'd3;
    if (w[i] !== 8'h22 || w[j] !== 8'h44) failures++;
    in = 6'b11_10_01;
    out = in[2-:2];
    if (out !== 4'b1110) failures++;
    w[2] = 8'haa;
    i = 3;
    w[i] = 8'hbb;
    w[1][3:0] = 4'h5;
    if (w !== 32'hbbaa2511) failures++;
    w[1:0] = 16'hcdef;
    if (w !== 32'hbbaacdef) failures++;
    n = '0;
    n[i] <= 8'h77;
    n[0+:2] <= 16'h1234;
    #1;
    if (n !== 32'h77001234 || c !== 8'h96) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
