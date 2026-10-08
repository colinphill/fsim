// fsim: top=sv_compilation_unit std=2017
// IEEE 1800-2017 3.12.1: parameters, typedefs, variables and functions
// declared in the compilation-unit scope are visible to later design units.
parameter int WIDTH = 8;
localparam int DEPTH = WIDTH * 2;
typedef logic [WIDTH-1:0] word_t;
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
typedef enum logic [1:0] { IDLE, RUN, DONE } state_t;
typedef struct fwd_t;
typedef struct packed { logic [1:0] tag; } fwd_t;
int shared_counter = 5;

function automatic int twice(int x);
  return 2 * x;
endfunction

module cu_helper(output word_t value);
  assign value = word_t'(DEPTH);
endmodule

module sv_compilation_unit;
  word_t w;
  pair_t p;
  fwd_t f;
  state_t s = RUN;
  int failures = 0;
  cu_helper helper(.value(w));

  initial begin
    #1;
    p = 8'hA5;
    f = 2'b10;
    if (w !== 8'd16) begin
      $display("FAIL: parameter through port w=%0d", w);
      failures++;
    end
    if (p.hi !== 4'hA || p.lo !== 4'h5) begin
      $display("FAIL: packed struct typedef");
      failures++;
    end
    if (f.tag !== 2'b10) begin
      $display("FAIL: forward typedef");
      failures++;
    end
    if (s != RUN || $bits(word_t) != 8) begin
      $display("FAIL: enum typedef");
      failures++;
    end
    shared_counter++;
    if (shared_counter != 6) begin
      $display("FAIL: compilation-unit variable %0d", shared_counter);
      failures++;
    end
    if (twice(DEPTH) != 32) begin
      $display("FAIL: compilation-unit function");
      failures++;
    end
    if (failures == 0)
      $display("PASS");
    $finish;
  end
endmodule
