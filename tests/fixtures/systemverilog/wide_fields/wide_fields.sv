// SPDX-License-Identifier: Apache-2.0
//
// Field reads and writes of wide (65 .. 272 bit) four-state values, computed
// by the v4 kernel's KOp::wide_alu extract, insert and dynamic_insert in a
// thread (the initial block, through automatic functions whose locals are
// registers) and in combinational members (continuous assignments), against
// oracles that compare one bit at a time. The expected output
// (wide_fields.expected) is that every width reports zero mismatches; the
// oracles follow IEEE 1800-2023:
//   11.5.1  a bit-select or part-select write replaces exactly the selected
//           bits and leaves the others; a write through an x or z index, or
//           one out of the declared range, has no effect; a read of
//           part-select bits gives the bits' values, x and z included;
//   11.5.1  an element of a packed array is a part of the packed vector, so
//           an element write is a part-select write at 16 * index.
// The operand table holds zero, all ones, one, alternating bits, a value with
// an x bit in the middle, one with a z bit, the top bit and a pattern with
// bit 64 set (straddling the first chunk boundary); the indexes cover the
// first chunk, the chunk boundary, the last bit, one past the end, -1 and x.

module bit_case #(parameter int W = 65);
  logic [W-1:0] a;
  logic [31:0] idx;
  logic bit_in;
  logic [4:0] s5;
  logic [64:0] s65;
  int bad;
  int checks;
  logic eb [W];

  function automatic logic [W-1:0] val(input int k);
    logic [W-1:0] v;
    v = '0;
    case (k)
      0: v = '0;
      1: v = '1;
      2: v = 1;
      3: for (int i = 0; i < W; i++) v[i] = (i % 2) == 0;
      4: begin v = '1; v[W/2] = 1'bx; end
      5: begin v = 5; v[3] = 1'bz; end
      6: begin v = '0; v[W-1] = 1'b1; end
      default: begin
        for (int i = 0; i < W; i++) v[i] = ((i * 7 + 3) % 5) < 2;
        v[64] = 1'b1;
      end
    endcase
    val = v;
  endfunction

  function automatic logic [31:0] idxval(input int k);
    case (k)
      0: idxval = 0;
      1: idxval = 1;
      2: idxval = 63;
      3: idxval = 64;
      4: idxval = W - 1;
      5: idxval = W;
      6: idxval = 32'hffffffff;
      7: idxval = 32'bx;
      default: idxval = W / 2;
    endcase
  endfunction

  function automatic logic bitval(input int k);
    case (k)
      0: bitval = 1'b0;
      1: bitval = 1'b1;
      2: bitval = 1'bx;
      default: bitval = 1'bz;
    endcase
  endfunction

  // Insert at a constant offset: a narrow source low, at the top, and a
  // 65 bit (wide) source at the top and the bottom.
  function automatic logic [W-1:0] set_low(input logic [W-1:0] t, input logic [4:0] s);
    logic [W-1:0] r;
    r = t;
    r[7:3] = s;
    return r;
  endfunction

  function automatic logic [W-1:0] set_top(input logic [W-1:0] t, input logic [4:0] s);
    logic [W-1:0] r;
    r = t;
    r[W-1:W-5] = s;
    return r;
  endfunction

  function automatic logic [W-1:0] set_wide_top(input logic [W-1:0] t, input logic [64:0] s);
    logic [W-1:0] r;
    r = t;
    r[W-1:W-65] = s;
    return r;
  endfunction

  function automatic logic [W-1:0] set_wide_low(input logic [W-1:0] t, input logic [64:0] s);
    logic [W-1:0] r;
    r = t;
    r[64:0] = s;
    return r;
  endfunction

  // Extract between wide values.
  function automatic logic [64:0] get_top(input logic [W-1:0] t);
    return t[W-1:W-65];
  endfunction

  function automatic logic [64:0] get_low(input logic [W-1:0] t);
    return t[64:0];
  endfunction

  // Insert at a run-time index.
  function automatic logic [W-1:0] set_bit(input logic [W-1:0] t,
      input logic [31:0] i, input logic b);
    logic [W-1:0] r;
    r = t;
    r[i] = b;
    return r;
  endfunction

  // Combinational members.
  wire [W-1:0] m_low = set_low(a, s5);
  wire [W-1:0] m_top = set_top(a, s5);
  wire [W-1:0] m_wide_top = set_wide_top(a, s65);
  wire [W-1:0] m_wide_low = set_wide_low(a, s65);
  wire [64:0] m_get_top = get_top(a);
  wire [64:0] m_get_low = get_low(a);
  wire [W-1:0] m_bit = set_bit(a, idx, bit_in);

  task automatic compare(input string name, input logic [W-1:0] got);
    for (int i = 0; i < W; i++) begin
      checks = checks + 1;
      if (got[i] !== eb[i]) begin
        bad = bad + 1;
        if (bad < 6) $display("  FAIL %s i=%0d a=%h s5=%b s65=%h idx=%h b=%b got=%b exp=%b",
            name, i, a, s5, s65, idx, bit_in, got[i], eb[i]);
      end
    end
  endtask

  task automatic compare65(input string name, input logic [64:0] got);
    for (int i = 0; i < 65; i++) begin
      checks = checks + 1;
      if (got[i] !== eb[i]) begin
        bad = bad + 1;
        if (bad < 6) $display("  FAIL %s i=%0d a=%h got=%b exp=%b", name, i, a, got[i], eb[i]);
      end
    end
  endtask

  task automatic run_fields(input int ka, input int kb);
    logic [W-1:0] b;
    a = val(ka);
    b = val(kb);
    s5 = b[4:0];
    s65 = b[64:0];
    #1;
    for (int i = 0; i < W; i++) eb[i] = (i >= 3 && i <= 7) ? s5[i - 3] : a[i];
    compare("set_low", set_low(a, s5));
    compare("member set_low", m_low);
    for (int i = 0; i < W; i++) eb[i] = (i >= W - 5) ? s5[i - (W - 5)] : a[i];
    compare("set_top", set_top(a, s5));
    compare("member set_top", m_top);
    for (int i = 0; i < W; i++) eb[i] = (i >= W - 65) ? s65[i - (W - 65)] : a[i];
    compare("set_wide_top", set_wide_top(a, s65));
    compare("member set_wide_top", m_wide_top);
    for (int i = 0; i < W; i++) eb[i] = (i <= 64) ? s65[i] : a[i];
    compare("set_wide_low", set_wide_low(a, s65));
    compare("member set_wide_low", m_wide_low);
    for (int i = 0; i < 65; i++) eb[i] = a[W - 65 + i];
    compare65("get_top", get_top(a));
    compare65("member get_top", m_get_top);
    for (int i = 0; i < 65; i++) eb[i] = a[i];
    compare65("get_low", get_low(a));
    compare65("member get_low", m_get_low);
  endtask

  task automatic run_dynamic(input int ka, input int ki, input int kb);
    bit inrange;
    a = val(ka);
    idx = idxval(ki);
    bit_in = bitval(kb);
    #1;
    inrange = ((^idx) !== 1'bx) && (idx < W);
    for (int i = 0; i < W; i++) eb[i] = (inrange && i == idx) ? bit_in : a[i];
    compare("set_bit", set_bit(a, idx, bit_in));
    compare("member set_bit", m_bit);
  endtask

  initial begin
    bad = 0;
    checks = 0;
    s5 = 0;
    s65 = 0;
    idx = 0;
    bit_in = 1'b0;
    for (int i = 0; i < 8; i++)
      for (int j = 0; j < 8; j++) run_fields(i, j);
    for (int i = 0; i < 8; i++)
      for (int k = 0; k < 9; k++)
        for (int b = 0; b < 4; b++) run_dynamic(i, k, b);
    if (checks == 0) bad = bad + 1;
    $display("wide_fields W=%0d mismatches=%0d", W, bad);
  end
endmodule

// A packed array of 16 bit elements: an element write through a run-time
// index inserts 16 bits (a narrow source, W = 16 * N bits).
module element_case #(parameter int N = 5);
  localparam int W = 16 * N;
  logic [W-1:0] a;
  logic [31:0] idx;
  logic [15:0] element;
  int bad;
  int checks;
  logic eb [W];

  function automatic logic [W-1:0] val(input int k);
    logic [W-1:0] v;
    v = '0;
    case (k)
      0: v = '0;
      1: v = '1;
      2: v = 1;
      3: for (int i = 0; i < W; i++) v[i] = (i % 2) == 0;
      4: begin v = '1; v[W/2] = 1'bx; end
      5: begin v = 5; v[3] = 1'bz; end
      6: begin v = '0; v[W-1] = 1'b1; end
      default: begin
        for (int i = 0; i < W; i++) v[i] = ((i * 7 + 3) % 5) < 2;
        v[64] = 1'b1;
      end
    endcase
    val = v;
  endfunction

  function automatic logic [31:0] idxval(input int k);
    case (k)
      0: idxval = 0;
      1: idxval = 1;
      2: idxval = N - 1;
      3: idxval = N;
      4: idxval = 32'hffffffff;
      5: idxval = 32'bx;
      default: idxval = N / 2;
    endcase
  endfunction

  function automatic logic [15:0] elementval(input int k);
    case (k)
      0: elementval = 16'h0000;
      1: elementval = 16'hffff;
      2: elementval = 16'bx;
      default: elementval = 16'b10xz_01xz_1100_0011;
    endcase
  endfunction

  function automatic logic [W-1:0] set_element(input logic [W-1:0] t,
      input logic [31:0] i, input logic [15:0] e);
    logic [N-1:0][15:0] r;
    r = t;
    r[i] = e;
    return r;
  endfunction

  wire [W-1:0] m_element = set_element(a, idx, element);

  task automatic compare(input string name, input logic [W-1:0] got);
    for (int i = 0; i < W; i++) begin
      checks = checks + 1;
      if (got[i] !== eb[i]) begin
        bad = bad + 1;
        if (bad < 6) $display("  FAIL %s i=%0d a=%h idx=%h e=%b got=%b exp=%b",
            name, i, a, idx, element, got[i], eb[i]);
      end
    end
  endtask

  task automatic run_element(input int ka, input int ki, input int ke);
    bit inrange;
    a = val(ka);
    idx = idxval(ki);
    element = elementval(ke);
    #1;
    inrange = ((^idx) !== 1'bx) && (idx < N);
    for (int i = 0; i < W; i++)
      eb[i] = (inrange && (i / 16) == idx) ? element[i % 16] : a[i];
    compare("set_element", set_element(a, idx, element));
    compare("member set_element", m_element);
  endtask

  initial begin
    bad = 0;
    checks = 0;
    idx = 0;
    element = '0;
    for (int i = 0; i < 8; i++)
      for (int k = 0; k < 7; k++)
        for (int e = 0; e < 4; e++) run_element(i, k, e);
    if (checks == 0) bad = bad + 1;
    $display("wide_fields W=%0d mismatches=%0d", W, bad);
  end
endmodule

module top;
  bit_case #(.W(65)) b65 ();
  bit_case #(.W(128)) b128 ();
  bit_case #(.W(129)) b129 ();
  bit_case #(.W(200)) b200 ();
  element_case #(.N(5)) e80 ();
  element_case #(.N(9)) e144 ();
  element_case #(.N(13)) e208 ();
  element_case #(.N(17)) e272 ();
endmodule
