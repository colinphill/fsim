// fsim: top=sv_operand_signedness std=2017
// IEEE 1800-2017 11.8.1/11.8.2: an operation is signed only when all of its
// operands are, and its context-determined operands extend by that
// signedness; 23.3.3.7: a port read keeps its declared signedness;
// 23.2.2.1: `int i; output i;` is one port with the earlier data type;
// 6.20.2: a typed parameter converts a name default; 5.7.1: a leading x/z
// digit fills a parameter value; 13.4.1: an unassigned 4-state function
// result is x; 6.21: a module lifetime is its tasks' default lifetime;
// 5.9: a string literal assigns eight bits per character; 11.6.1: the
// exponent of ** does not change how its base extends.
module sv_operand_signedness_child(a, b, w, o1, o2, wbits);
  input [7:0] a;
  input signed [7:0] b;
  int w;
  output w;
  output [15:0] o1, o2;
  output [7:0] wbits;
  wire signed [7:0] a;
  assign o1 = a;
  assign o2 = b + 16'sd0;
  assign wbits = $bits(w);
  initial w = 7;
endmodule

module automatic sv_operand_signedness;
  parameter A_ONE = '1;
  parameter [3:0] A_W4 = A_ONE;
  parameter int A_INT = A_ONE;
  parameter [1:0] TWO_Z = 2'b?;
  localparam X4 = 4'bx;
  struct packed { logic e0; logic [1:0] e1; logic [3:0] e2; } fields;
  int failures = 0;
  reg [7:0] u;
  reg signed [3:0] s, s2;
  reg [7:0] r8;
  reg [15:0] r16;
  reg [7:0] actual = 8'hff;
  bit [8*14:1] text;
  reg [31:0] pair;
  reg signed [128:0] power;
  reg [31:0] exponent = 63;
  integer base = -2;
  wire [15:0] o1, o2;
  wire [31:0] w;
  wire [7:0] wbits;

  sv_operand_signedness_child child(actual, actual, w, o1, o2, wbits);

  function integer no_result();
  endfunction

  function logic [3:0] maybe(input x);
    if (x) maybe = 1;
  endfunction

  task accumulate(input integer value, output integer result);
    int total = 1;
    total = total + value;
    result = total;
  endtask

  task check(input [63:0] got, input [63:0] expected, input string what);
    if (got !== expected) begin
      $display("FAIL %s: got %h expected %h", what, got, expected);
      failures++;
    end
  endtask

  initial begin
    integer result;
    integer seed;
    u = 8'd3;
    s = -4'sd1;
    s2 = -4'sd2;
    r8 = u + s;
    check(r8, 8'h12, "mixed add");
    r8 = u + (s + s2);
    check(r8, 8'h20, "nested mixed add");
    r8 = s + s2;
    check(r8, 8'hfd, "signed add");
    check(u == s, 0, "mixed equality");
    check(s < u, 0, "mixed compare");
    r8 = u + -s;
    check(r8, 8'hf4, "unary minus");
    r16 = u + (s <<< 1);
    check(r16, 16'h0021, "shift operand");
    r16 = u * s;
    check(r16, 16'h002d, "mixed multiply");
    check($bits(A_W4), 4, "ranged parameter width");
    check(A_W4, 4'b0001, "ranged parameter value");
    check($bits(A_INT), 32, "int parameter width");
    check(TWO_Z === 2'bzz, 1, "z parameter fill");
    check($bits(X4), 4, "x parameter width");
    check(X4 === 4'bxxxx, 1, "x parameter value");
    check($bits(fields.e1), 2, "member bits");
    check($bits(fields.e2), 4, "member bits");
    check(no_result() === 32'bx, 1, "empty function result");
    check(maybe(0) === 4'bx, 1, "unassigned function result");
    accumulate(2, result);
    accumulate(3, result);
    check(result, 4, "automatic task in automatic module");
    check($dist_uniform(seed, 10, 0), 10, "dist_uniform start >= end");
    text = "Test";
    check(text[32:1], 32'h54657374, "string literal");
    check(text[64:33], 0, "string literal zero fill");
    pair = "ab";
    check(pair, 32'h00006162, "two-character literal");
    power = base ** exponent;
    check(power[63:0], 64'h8000000000000000, "power base sign");
    check(power[128:64], {65{1'b1}}, "power base sign extension");
    #1;
    check(o1, 16'hffff, "redeclared signed port");
    check(o2, 16'hffff, "signed input port");
    check(wbits, 32, "port declared after type");
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
