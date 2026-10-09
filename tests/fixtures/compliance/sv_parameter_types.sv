// fsim: top=sv_parameter_types std=2017
// IEEE 1800-2017 6.20.2: a value parameter without a type or range takes
// the width and signedness of its value; with a range it has that range and
// its value converts to it. 5.7.1: an unsized based literal is 32 bits and
// an unbased unsized literal is one bit when self-determined.
module sv_parameter_types;
  parameter [3:0] P = 5;
  parameter Q = 3'd5;
  parameter R = 'b1;
  parameter S = '1;
  localparam [7:0] L = 300;
  localparam [15:0] W = 8'd7;
  localparam integer TWENTYONE = 21;
  typedef struct packed {
    logic [2:0] foo;
    logic [2:0] bar;
  } pair_t;
  pair_t pair;
  assign pair = '{foo: TWENTYONE % 8 + 1, bar: (TWENTYONE / 8) + 1};

  initial begin
    int failures = 0;
    logic [15:0] widened;
    if ($bits(P) != 4 || P != 5) failures++;
    if ($bits(Q) != 3 || Q != 5 || Q < 0) failures++;
    if ($bits(R) != 32 || R != 1) failures++;
    if ($bits(S) != 1 || S != 1) failures++;
    if ($bits(L) != 8 || L != 44) failures++;
    widened = L + 16'd1;
    if (widened != 45) failures++;
    if ($bits(W) != 16 || W != 7) failures++;
    #1;
    if (pair != 6'b110_011) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
