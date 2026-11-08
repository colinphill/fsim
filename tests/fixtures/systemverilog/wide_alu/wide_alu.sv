// SPDX-License-Identifier: Apache-2.0
//
// Arithmetic and logic on wide (65, 128, 200 and 256 bit) four-state values,
// computed by the v4 kernel's KOp::wide_alu in a thread (the initial block)
// and in combinational members (continuous assignments), against oracles
// written bit by bit on one-bit values. The expected output
// (wide_alu.expected) is that every width reports zero mismatches; each
// oracle follows IEEE 1800-2023:
//   11.4.3  arithmetic: an x or z operand bit makes the result all x; the
//           division and modulus of zero are x; the quotient truncates toward
//           zero and % takes the dividend's sign; 11.4.3 Table 11-4: a signed
//           negative exponent gives x for base 0, 1 for base 1, 1 or -1 for
//           base -1 (even or odd exponent) and 0 otherwise;
//   11.4.4  relational: x when an operand bit is x or z, else the integer
//           comparison (signed when both operands are signed);
//   11.4.5  equality: 0 on a known differing bit, else x with an x or z bit;
//           11.4.6 case equality compares x and z literally;
//   11.4.7  logical: one bit, x when the truth value is unknown;
//   11.4.8  bitwise: the Table 11-6 .. 11-9 per-bit tables;
//   11.4.9  reduction: the same tables folded over the bits;
//   11.4.10 shifts: x or z in the amount gives all x; a logical shift fills
//           with 0, an arithmetic right shift of a signed value with the sign;
//   11.4.11 conditional: an unknown condition merges the operands, equal 0 or
//           1 bits surviving and every other bit becoming x.
// The operand table holds zero, all ones, one, alternating bits, values with
// an x bit and a z bit, the minimum negative number and a patterned value
// with bit 64 set (straddling the first chunk boundary).

`define CHECK(NAME, GOT, EXP) \
  if ((GOT) !== (EXP)) begin \
    bad = bad + 1; \
    if (bad < 6) $display("  FAIL %s a=%h b=%h got=%h exp=%h", NAME, a, b, GOT, EXP); \
  end

module wide_case #(parameter int W = 65);
  logic [W-1:0] a, b, wamt;
  logic signed [W-1:0] sa, sb;
  logic [31:0] amt;
  logic cond;
  int bad;
  int checks;

  // Operands.
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

  function automatic bit unknown(input logic [W-1:0] v);
    unknown = 0;
    for (int i = 0; i < W; i++) if (v[i] === 1'bx || v[i] === 1'bz) unknown = 1;
  endfunction

  function automatic logic [W-1:0] o_add(input logic [W-1:0] x, y);
    logic carry;
    logic [W-1:0] r;
    carry = 0;
    for (int i = 0; i < W; i++) begin
      r[i] = x[i] ^ y[i] ^ carry;
      carry = (x[i] & y[i]) | (carry & (x[i] ^ y[i]));
    end
    o_add = r;
  endfunction

  function automatic logic [W-1:0] o_neg(input logic [W-1:0] x);
    logic [W-1:0] inv;
    logic [W-1:0] one;
    for (int i = 0; i < W; i++) inv[i] = ~x[i];
    one = 1;
    o_neg = o_add(inv, one);
  endfunction

  function automatic logic [W-1:0] o_sub(input logic [W-1:0] x, y);
    o_sub = o_add(x, o_neg(y));
  endfunction

  function automatic logic [W-1:0] o_mul(input logic [W-1:0] x, y);
    logic [W-1:0] acc;
    logic [W-1:0] part;
    acc = '0;
    for (int j = 0; j < W; j++) begin
      if (y[j]) begin
        part = '0;
        for (int i = j; i < W; i++) part[i] = x[i - j];
        acc = o_add(acc, part);
      end
    end
    o_mul = acc;
  endfunction

  // x < y, unsigned, on known values.
  function automatic bit o_ltu(input logic [W-1:0] x, y);
    o_ltu = 0;
    for (int i = 0; i < W; i++) begin
      if (x[i] != y[i]) o_ltu = y[i];
    end
  endfunction

  function automatic bit o_lts(input logic [W-1:0] x, y);
    if (x[W-1] != y[W-1]) o_lts = x[W-1];
    else o_lts = o_ltu(x, y);
  endfunction

  function automatic bit o_zero(input logic [W-1:0] x);
    o_zero = 1;
    for (int i = 0; i < W; i++) if (x[i]) o_zero = 0;
  endfunction

  // Restoring division of known unsigned values: quotient, remainder.
  function automatic logic [2*W-1:0] o_divmod(input logic [W-1:0] x, y);
    logic [W-1:0] q;
    logic [W:0] r;
    logic [W:0] ye;
    q = '0;
    r = '0;
    ye = {1'b0, y};
    for (int i = W - 1; i >= 0; i--) begin
      for (int k = W; k > 0; k--) r[k] = r[k-1];
      r[0] = x[i];
      // r >= ye ?
      begin
        bit ge;
        ge = 1;
        for (int k = 0; k <= W; k++) if (r[k] != ye[k]) ge = r[k];
        if (ge) begin
          logic borrow;
          borrow = 0;
          for (int k = 0; k <= W; k++) begin
            logic d;
            d = r[k] ^ ye[k] ^ borrow;
            borrow = (~r[k] & (ye[k] | borrow)) | (ye[k] & borrow);
            r[k] = d;
          end
          q[i] = 1;
        end
      end
    end
    o_divmod = {r[W-1:0], q};
  endfunction

  function automatic logic [W-1:0] o_divu(input logic [W-1:0] x, y);
    logic [2*W-1:0] qr;
    if (unknown(x) || unknown(y) || o_zero(y)) o_divu = {W{1'bx}};
    else begin qr = o_divmod(x, y); o_divu = qr[W-1:0]; end
  endfunction

  function automatic logic [W-1:0] o_modu(input logic [W-1:0] x, y);
    logic [2*W-1:0] qr;
    if (unknown(x) || unknown(y) || o_zero(y)) o_modu = {W{1'bx}};
    else begin qr = o_divmod(x, y); o_modu = qr[2*W-1:W]; end
  endfunction

  function automatic logic [W-1:0] o_divs(input logic [W-1:0] x, y);
    logic [W-1:0] mx, my, q;
    if (unknown(x) || unknown(y) || o_zero(y)) o_divs = {W{1'bx}};
    else begin
      mx = x[W-1] ? o_neg(x) : x;
      my = y[W-1] ? o_neg(y) : y;
      q = o_divu(mx, my);
      o_divs = (x[W-1] != y[W-1]) ? o_neg(q) : q;
    end
  endfunction

  function automatic logic [W-1:0] o_mods(input logic [W-1:0] x, y);
    logic [W-1:0] mx, my, r;
    if (unknown(x) || unknown(y) || o_zero(y)) o_mods = {W{1'bx}};
    else begin
      mx = x[W-1] ? o_neg(x) : x;
      my = y[W-1] ? o_neg(y) : y;
      r = o_modu(mx, my);
      o_mods = x[W-1] ? o_neg(r) : r;
    end
  endfunction

  // Per-bit four-state tables (IEEE 1800-2023 Tables 11-6 .. 11-9).
  function automatic logic bit_and(input logic p, q);
    if (p === 1'b0 || q === 1'b0) bit_and = 1'b0;
    else if (p === 1'b1 && q === 1'b1) bit_and = 1'b1;
    else bit_and = 1'bx;
  endfunction
  function automatic logic bit_or(input logic p, q);
    if (p === 1'b1 || q === 1'b1) bit_or = 1'b1;
    else if (p === 1'b0 && q === 1'b0) bit_or = 1'b0;
    else bit_or = 1'bx;
  endfunction
  function automatic logic bit_xor(input logic p, q);
    if ((p !== 1'b0 && p !== 1'b1) || (q !== 1'b0 && q !== 1'b1)) bit_xor = 1'bx;
    else bit_xor = p ^ q;
  endfunction

  function automatic logic [W-1:0] o_and(input logic [W-1:0] x, y);
    for (int i = 0; i < W; i++) o_and[i] = bit_and(x[i], y[i]);
  endfunction
  function automatic logic [W-1:0] o_or(input logic [W-1:0] x, y);
    for (int i = 0; i < W; i++) o_or[i] = bit_or(x[i], y[i]);
  endfunction
  function automatic logic [W-1:0] o_xor(input logic [W-1:0] x, y);
    for (int i = 0; i < W; i++) o_xor[i] = bit_xor(x[i], y[i]);
  endfunction
  function automatic logic [W-1:0] o_not(input logic [W-1:0] x);
    for (int i = 0; i < W; i++) o_not[i] = (x[i] === 1'b0) ? 1'b1
                                          : (x[i] === 1'b1) ? 1'b0 : 1'bx;
  endfunction

  function automatic logic o_red_and(input logic [W-1:0] x);
    o_red_and = 1'b1;
    for (int i = 0; i < W; i++) o_red_and = bit_and(o_red_and, x[i]);
  endfunction
  function automatic logic o_red_or(input logic [W-1:0] x);
    o_red_or = 1'b0;
    for (int i = 0; i < W; i++) o_red_or = bit_or(o_red_or, x[i]);
  endfunction
  function automatic logic o_red_xor(input logic [W-1:0] x);
    o_red_xor = 1'b0;
    for (int i = 0; i < W; i++) o_red_xor = bit_xor(o_red_xor, x[i]);
  endfunction

  // Truth value of a vector (11.4.7).
  function automatic logic o_truth(input logic [W-1:0] x);
    o_truth = o_red_or(x);
  endfunction

  function automatic logic o_eq(input logic [W-1:0] x, y);
    bit differ;
    bit any;
    differ = 0;
    any = 0;
    for (int i = 0; i < W; i++) begin
      if ((x[i] !== 1'b0 && x[i] !== 1'b1) || (y[i] !== 1'b0 && y[i] !== 1'b1)) any = 1;
      else if (x[i] !== y[i]) differ = 1;
    end
    o_eq = differ ? 1'b0 : any ? 1'bx : 1'b1;
  endfunction

  function automatic logic o_ceq(input logic [W-1:0] x, y);
    o_ceq = 1'b1;
    for (int i = 0; i < W; i++) if (x[i] !== y[i]) o_ceq = 1'b0;
  endfunction

  function automatic logic o_lt(input logic [W-1:0] x, y, input bit sgn);
    if (unknown(x) || unknown(y)) o_lt = 1'bx;
    else o_lt = sgn ? o_lts(x, y) : o_ltu(x, y);
  endfunction

  // Shifts by a known amount n (0 to W+3).
  function automatic logic [W-1:0] o_shl(input logic [W-1:0] x, input int n);
    for (int i = 0; i < W; i++) o_shl[i] = (i - n >= 0) ? x[i - n] : 1'b0;
  endfunction
  function automatic logic [W-1:0] o_shr(input logic [W-1:0] x, input int n);
    for (int i = 0; i < W; i++) o_shr[i] = (i + n < W) ? x[i + n] : 1'b0;
  endfunction
  function automatic logic [W-1:0] o_sra(input logic [W-1:0] x, input int n);
    for (int i = 0; i < W; i++) o_sra[i] = (i + n < W) ? x[i + n] : x[W-1];
  endfunction

  function automatic logic [W-1:0] o_cond(input logic c, input logic [W-1:0] x, y);
    if (c === 1'b1) o_cond = x;
    else if (c === 1'b0) o_cond = y;
    else
      for (int i = 0; i < W; i++)
        o_cond[i] = (x[i] === y[i] && (x[i] === 1'b0 || x[i] === 1'b1)) ? x[i] : 1'bx;
  endfunction

  // The values under test, as continuous assignments (combinational members).
  wire [W-1:0] c_add = a + b;
  wire [W-1:0] c_sub = a - b;
  wire [W-1:0] c_mul = a * b;
  wire [W-1:0] c_div = a / b;
  wire [W-1:0] c_mod = a % b;
  wire signed [W-1:0] c_sdiv = sa / sb;
  wire signed [W-1:0] c_smod = sa % sb;
  wire [W-1:0] c_and = a & b;
  wire [W-1:0] c_or = a | b;
  wire [W-1:0] c_xor = a ^ b;
  wire [W-1:0] c_not = ~a;
  wire c_ltu = a < b;
  wire c_slt = sa < sb;
  wire c_eq = a == b;
  wire c_ne = a != b;
  wire c_ceq = a === b;
  wire c_cne = a !== b;
  wire c_rand = &a;
  wire c_ror = |a;
  wire c_rxor = ^a;
  wire c_rnand = ~&a;
  wire c_land = a && b;
  wire c_lor = a || b;
  wire c_lnot = !a;
  wire [W-1:0] c_shl = a << amt;
  wire [W-1:0] c_shr = a >> amt;
  wire signed [W-1:0] c_sra = sa >>> amt;
  wire [W-1:0] c_shlw = a << wamt;
  wire [W-1:0] c_cond = cond ? a : b;

  function automatic int amount(input int k);
    case (k)
      0: amount = 0;
      1: amount = 1;
      2: amount = 63;
      3: amount = 64;
      4: amount = 65;
      5: amount = W - 1;
      6: amount = W;
      default: amount = W + 3;
    endcase
  endfunction

  logic [W-1:0] t_add, t_sub, t_mul, t_div, t_mod, t_and, t_or, t_xor, t_not;
  logic [W-1:0] t_shl, t_shr, t_cond, t_shlw;
  logic signed [W-1:0] t_sdiv, t_smod, t_sra;
  logic t_ltu, t_slt, t_eq, t_ne, t_ceq, t_rand, t_ror, t_rxor, t_land, t_lnot;
  bit [W-1:0] t_two;
  logic [W-1:0] e;

  task automatic run_pair(input int i, j);
    a = val(i);
    b = val(j);
    sa = a;
    sb = b;
    // Thread computations.
    t_add = a + b;
    t_sub = a - b;
    t_mul = a * b;
    t_div = a / b;
    t_mod = a % b;
    t_sdiv = sa / sb;
    t_smod = sa % sb;
    t_and = a & b;
    t_or = a | b;
    t_xor = a ^ b;
    t_not = ~a;
    t_ltu = a < b;
    t_slt = sa < sb;
    t_eq = a == b;
    t_ne = a != b;
    t_ceq = a === b;
    t_rand = &a;
    t_ror = |a;
    t_rxor = ^a;
    t_land = a && b;
    t_lnot = !a;
    t_two = a;
    #1;
    // Oracles, against the thread values and the combinational members.
    e = unknown(a) || unknown(b) ? {W{1'bx}} : o_add(a, b);
    `CHECK("add thread", t_add, e) `CHECK("add comb", c_add, e)
    e = unknown(a) || unknown(b) ? {W{1'bx}} : o_sub(a, b);
    `CHECK("sub thread", t_sub, e) `CHECK("sub comb", c_sub, e)
    e = unknown(a) || unknown(b) ? {W{1'bx}} : o_mul(a, b);
    `CHECK("mul thread", t_mul, e) `CHECK("mul comb", c_mul, e)
    e = o_divu(a, b);
    `CHECK("div thread", t_div, e) `CHECK("div comb", c_div, e)
    e = o_modu(a, b);
    `CHECK("mod thread", t_mod, e) `CHECK("mod comb", c_mod, e)
    e = o_divs(a, b);
    `CHECK("sdiv thread", t_sdiv, e) `CHECK("sdiv comb", c_sdiv, e)
    e = o_mods(a, b);
    `CHECK("smod thread", t_smod, e) `CHECK("smod comb", c_smod, e)
    e = o_and(a, b);
    `CHECK("and thread", t_and, e) `CHECK("and comb", c_and, e)
    e = o_or(a, b);
    `CHECK("or thread", t_or, e) `CHECK("or comb", c_or, e)
    e = o_xor(a, b);
    `CHECK("xor thread", t_xor, e) `CHECK("xor comb", c_xor, e)
    e = o_not(a);
    `CHECK("not thread", t_not, e) `CHECK("not comb", c_not, e)
    `CHECK("ltu thread", t_ltu, o_lt(a, b, 0)) `CHECK("ltu comb", c_ltu, o_lt(a, b, 0))
    `CHECK("slt thread", t_slt, o_lt(a, b, 1)) `CHECK("slt comb", c_slt, o_lt(a, b, 1))
    `CHECK("eq thread", t_eq, o_eq(a, b)) `CHECK("eq comb", c_eq, o_eq(a, b))
    `CHECK("ne thread", t_ne, ~o_eq(a, b)) `CHECK("ne comb", c_ne, ~o_eq(a, b))
    `CHECK("ceq thread", t_ceq, o_ceq(a, b)) `CHECK("ceq comb", c_ceq, o_ceq(a, b))
    `CHECK("cne comb", c_cne, ~o_ceq(a, b))
    `CHECK("rand thread", t_rand, o_red_and(a)) `CHECK("rand comb", c_rand, o_red_and(a))
    `CHECK("ror thread", t_ror, o_red_or(a)) `CHECK("ror comb", c_ror, o_red_or(a))
    `CHECK("rxor thread", t_rxor, o_red_xor(a)) `CHECK("rxor comb", c_rxor, o_red_xor(a))
    `CHECK("rnand comb", c_rnand, ~o_red_and(a))
    `CHECK("land thread", t_land, bit_and(o_truth(a), o_truth(b)))
    `CHECK("land comb", c_land, bit_and(o_truth(a), o_truth(b)))
    `CHECK("lor comb", c_lor, bit_or(o_truth(a), o_truth(b)))
    `CHECK("lnot thread", t_lnot, ~o_truth(a)) `CHECK("lnot comb", c_lnot, ~o_truth(a))
    // ConvertToTwoState: x and z read as 0.
    e = a;
    for (int k = 0; k < W; k++) if (a[k] !== 1'b1) e[k] = 1'b0;
    `CHECK("two-state thread", t_two, e)
    checks = checks + 1;
  endtask

  task automatic run_shift(input int i, n);
    a = val(i);
    sa = a;
    amt = amount(n);
    wamt = '0;
    wamt[31:0] = amount(n);
    cond = 1'bx;
    t_shl = a << amt;
    t_shr = a >> amt;
    t_sra = sa >>> amt;
    t_shlw = a << wamt;
    #1;
    e = o_shl(a, amt);
    `CHECK("shl thread", t_shl, e) `CHECK("shl comb", c_shl, e) `CHECK("shl wide amount", c_shlw, e)
    `CHECK("shl wide amount thread", t_shlw, e)
    e = o_shr(a, amt);
    `CHECK("shr thread", t_shr, e) `CHECK("shr comb", c_shr, e)
    e = o_sra(a, amt);
    `CHECK("sra thread", t_sra, e) `CHECK("sra comb", c_sra, e)
    // An unknown amount.
    amt = 32'bx;
    t_shl = a << amt;
    #1;
    e = {W{1'bx}};
    `CHECK("shl x amount", t_shl, e) `CHECK("shl x amount comb", c_shl, e)
    checks = checks + 1;
  endtask

  task automatic run_cond(input int i, j, input int c);
    a = val(i);
    b = val(j);
    case (c)
      0: cond = 1'b0;
      1: cond = 1'b1;
      2: cond = 1'bx;
      default: cond = 1'bz;
    endcase
    t_cond = cond ? a : b;
    #1;
    e = o_cond(cond, a, b);
    `CHECK("cond thread", t_cond, e) `CHECK("cond comb", c_cond, e)
    checks = checks + 1;
  endtask

  // Powers: a ** e for small known exponents, then the signed negative ones.
  task automatic run_power(input int i, input int k);
    logic [W-1:0] base, expo, acc;
    logic [W-1:0] r, rs;
    logic signed [W-1:0] sbase, sexpo;
    base = val(i);
    case (k)
      0: expo = 0;
      1: expo = 1;
      2: expo = 3;
      default: expo = 7;
    endcase
    r = base ** expo;
    #1;
    acc = 1;
    for (int n = 0; n < expo; n++) acc = o_mul(acc, base);
    e = unknown(base) ? {W{1'bx}} : acc;
    `CHECK("power", r, e)
    // Signed negative exponents (Table 11-4).
    sbase = base;
    sexpo = -1 - k;
    rs = sbase ** sexpo;
    #1;
    if (unknown(base)) e = {W{1'bx}};
    else if (o_zero(base)) e = {W{1'bx}};
    else if (base == 1) e = 1;
    else if (o_ceq(base, {W{1'b1}})) e = (sexpo[0]) ? {W{1'b1}} : 1;
    else e = '0;
    `CHECK("signed negative power", rs, e)
    checks = checks + 1;
  endtask

  initial begin
    bad = 0;
    checks = 0;
    cond = 1'b0;
    amt = 0;
    wamt = '0;
    for (int i = 0; i < 8; i++)
      for (int j = 0; j < 8; j++) run_pair(i, j);
    for (int i = 0; i < 8; i++)
      for (int n = 0; n < 8; n++) run_shift(i, n);
    for (int i = 0; i < 8; i++)
      for (int j = 0; j < 8; j++)
        for (int c = 0; c < 4; c++) run_cond(i, j, c);
    for (int i = 0; i < 8; i++)
      for (int k = 0; k < 4; k++) run_power(i, k);
    $display("wide_alu W=%0d checks=%0d mismatches=%0d", W, checks, bad);
  end
endmodule

module top;
  wide_case #(.W(65)) w65 ();
  wide_case #(.W(128)) w128 ();
  wide_case #(.W(200)) w200 ();
  wide_case #(.W(256)) w256 ();
endmodule
