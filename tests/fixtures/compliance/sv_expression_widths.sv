// fsim: top=sv_expression_widths std=2017
// IEEE 1800-2017 11.8.2: a value extends to its target by its own
// signedness (assignments and task/function arguments); 6.24.1: a cast
// evaluates its operand in the cast's width; 11.6.1: sized arithmetic in a
// constant condition overflows its width; 6.19: an enumeration literal has
// its enumeration's width even when its value is an unsized integer.
module sv_expression_widths;
  typedef enum logic [2:0] { P = 0, W = 3'd1, E, N, S } dirs_t;
  typedef logic [7:0] byte_t;
  bit [11:0] packed_dirs = {N, E, W, P};
  int failures = 0;
  logic [7:0] seen;

  task automatic note(input signed [7:0] value);
    seen = value;
  endtask

  initial begin
    logic signed [7:0] s;
    logic [7:0] u;
    logic one;
    logic [1:0] amount;
    s = 3'b111;
    if (s !== 8'h07) failures++;
    s = 3'sb111;
    if (s !== 8'hff) failures++;
    u = 3'sb111;
    if (u !== 8'hff) failures++;
    note(3'b111);
    if (seen !== 8'h07) failures++;
    note(3'sb111);
    if (seen !== 8'hff) failures++;
    one = 1'b1;
    amount = 2'd2;
    u = byte_t'(one << amount);
    if (u !== 8'b100) failures++;
    if ((4'd15 + 4'd1) != 4'd0) failures++;
    if ((4'sd2 * 4'sd8) != 4'd0) failures++;
    if ($bits(P) != 3 || packed_dirs !== 12'o3210) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
