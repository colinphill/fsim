// fsim: top=sv_block_typedef_patterns std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.18: a typedef declared in a procedural block or a
// function names one type; every variable declared with it has that type,
// so a typed assignment pattern of it (10.9) assigns to the variable, and
// the literals of a block enumeration typedef are declared once (6.19).
// $bits of a typed assignment pattern is the width of its type (20.6.2).
module sv_block_typedef_patterns;
  typedef logic [7:0] byte_t;
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
  int failures;

  function automatic int bump(input int v);
    typedef int unsigned count_t;
    count_t c;
    c = count_t'(v) + 1;
    return c;
  endfunction

  initial begin
    typedef enum logic [1:0] { RED, GREEN, BLUE } color_t;
    typedef struct packed { logic [2:0] x; logic y; } nib_t;
    typedef struct { int i; byte b; } rec_t;
    typedef logic [3:0] n_t;
    color_t color;
    nib_t nib;
    rec_t rec;
    n_t n;
    byte_t widened;

    color = GREEN;
    nib = nib_t'{3'd5, 1'b1};
    rec = rec_t'{i: 9, b: 8'd10};
    n = 4'hF;
    widened = byte_t'(nib);
    if (color.name() != "GREEN" || color != 2'd1) failures++;
    if (nib != 4'hB || widened != 8'h0B) failures++;
    if (rec.i != 9 || rec.b != 8'd10) failures++;
    if ($bits(n) != 4 || $bits(nib) != 4) failures++;
    if ($bits(nib_t'{3'd1, 1'b0}) != 4) failures++;
    if ($bits(pair_t'{4'h1, 4'h2}) != 8) failures++;
    if (bump(4) != 5) failures++;
    begin
      typedef logic [1:0] n_t;
      n_t inner;
      inner = 2'b11;
      if ($bits(inner) != 2 || inner != 2'd3) failures++;
    end

    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
