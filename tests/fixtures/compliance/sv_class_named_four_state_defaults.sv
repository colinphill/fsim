// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.8 (Table 6-7), 6.11, 6.18, 6.19, 8.25, 13.5: a variable
// declared without an initializer starts at X when its type is 4-state and
// at 0 (null for a chandle) when it is 2-state, whether the type is spelled
// directly or named. A typedef of logic (also through another typedef, from
// a package or in a class), a typedef of integer, an enumeration over logic
// and a class type parameter bound to a 4-state type are 4-state; the same
// over bit, and int, are 2-state. Class properties, method locals, block
// locals and unassigned output formals are checked, in post_randomize (the
// class method executor) and in a method the module calls.
package pk;
  typedef logic [3:0] pnib;
  typedef enum logic [1:0] {PA, PB} ple;
endpackage

typedef logic [3:0] nib;
typedef nib nib2;
typedef bit [3:0] bnib;
typedef integer intg;
typedef enum logic [1:0] {LA, LB} le;
typedef enum bit [1:0] {BA = 1, BB = 2} be;

class C #(type T = logic [3:0], type U = int);
  typedef logic [2:0] cl;
  rand bit [1:0] x;
  nib n;
  nib2 n2;
  bnib b;
  intg g;
  le e;
  be e2;
  T t;
  U u;
  cl c3;
  pk::pnib pn;
  pk::ple pe;
  int i;
  bit [3:0] bv;
  logic [3:0] lv;
  // Each failed check inside the class sets one bit.
  int bad;
  // Whether the locals of the parameter types are all X or all 0.
  int t_x, t_0, u_x, u_0;

  function void fill_nib(output nib o);
  endfunction

  function void fill_int(output int o);
  endfunction

  function void check();
    int li;
    bit [3:0] lb;
    be lbe;
    chandle lch;
    logic [3:0] ll;
    nib ln;
    nib2 ln2;
    le lle;
    intg lg;
    cl lc;
    T lt;
    U lu;
    nib on;
    int oi;
    bad = 0;
    if (li !== 0) bad |= 1 << 0;
    if (lb !== 4'b0000) bad |= 1 << 1;
    if (lbe !== 2'b00) bad |= 1 << 2;
    if (lch != null) bad |= 1 << 3;
    if (ll !== 4'bxxxx) bad |= 1 << 4;
    if (ln !== 4'bxxxx) bad |= 1 << 5;
    if (ln2 !== 4'bxxxx) bad |= 1 << 6;
    if (lle !== 2'bxx) bad |= 1 << 7;
    if (lg !== 32'hxxxx_xxxx) bad |= 1 << 8;
    if (lc !== 3'bxxx) bad |= 1 << 9;
    begin
      int bi;
      nib bn;
      if (bi !== 0) bad |= 1 << 10;
      if (bn !== 4'bxxxx) bad |= 1 << 11;
    end
    on = 4'b0101;
    fill_nib(on);
    if (on !== 4'bxxxx) bad |= 1 << 12;
    oi = 5;
    fill_int(oi);
    if (oi !== 0) bad |= 1 << 13;
    // The properties as the class sees them.
    if (n !== 4'bxxxx || n2 !== 4'bxxxx || g !== 32'hxxxx_xxxx
        || e !== 2'bxx || c3 !== 3'bxxx || pn !== 4'bxxxx || pe !== 2'bxx
        || lv !== 4'bxxxx) bad |= 1 << 14;
    if (b !== 4'b0000 || e2 !== 2'b00 || i !== 0 || bv !== 4'b0000)
      bad |= 1 << 15;
    // T is logic [3:0], nib or bit [3:0]; U is int, le or be.
    t_x = lt === 4'bxxxx;
    t_0 = lt === 4'b0000;
    u_x = lu === 2'bxx;
    u_0 = lu === 0;
  endfunction

  function void post_randomize();
    check();
  endfunction
endclass

module top;
  C c_default;
  C #(nib, le) c_named;
  C #(bit [3:0], be) c_two;
  int failures;

  task automatic expect_properties;
    if (c_default.n !== 4'bxxxx || c_default.n2 !== 4'bxxxx
        || c_default.g !== 32'hxxxx_xxxx || c_default.e !== 2'bxx
        || c_default.c3 !== 3'bxxx || c_default.pn !== 4'bxxxx
        || c_default.pe !== 2'bxx || c_default.lv !== 4'bxxxx) begin
      $display("FAIL 4-state properties %b %b %b %b %b %b %b %b",
               c_default.n, c_default.n2, c_default.g, c_default.e,
               c_default.c3, c_default.pn, c_default.pe, c_default.lv);
      failures++;
    end
    if (c_default.b !== 4'b0000 || c_default.e2 !== 2'b00
        || c_default.i !== 0 || c_default.bv !== 4'b0000) begin
      $display("FAIL 2-state properties %b %b %0d %b", c_default.b,
               c_default.e2, c_default.i, c_default.bv);
      failures++;
    end
    if (c_default.t !== 4'bxxxx || c_default.u !== 0
        || c_named.t !== 4'bxxxx || c_named.u !== 2'bxx
        || c_two.t !== 4'b0000 || c_two.u !== 2'b00) begin
      $display("FAIL parameter properties %b %0d %b %b %b %b", c_default.t,
               c_default.u, c_named.t, c_named.u, c_two.t, c_two.u);
      failures++;
    end
  endtask

  task automatic expect_locals(string where);
    if (c_default.bad != 0 || c_named.bad != 0 || c_two.bad != 0) begin
      $display("FAIL %s locals %b %b %b", where, c_default.bad,
               c_named.bad, c_two.bad);
      failures++;
    end
    if (c_default.t_x !== 1 || c_default.u_0 !== 1 || c_named.t_x !== 1
        || c_named.u_x !== 1 || c_two.t_0 !== 1 || c_two.u_0 !== 1) begin
      $display("FAIL %s parameter locals %0d %0d %0d %0d %0d %0d", where,
               c_default.t_x, c_default.u_0, c_named.t_x, c_named.u_x,
               c_two.t_0, c_two.u_0);
      failures++;
    end
  endtask

  initial begin
    c_default = new;
    c_named = new;
    c_two = new;
    expect_properties();
    if (!c_default.randomize() || !c_named.randomize()
        || !c_two.randomize()) begin
      $display("FAIL randomize");
      failures++;
    end
    expect_locals("post_randomize");
    c_default = new;
    c_named = new;
    c_two = new;
    c_default.check();
    c_named.check();
    c_two.check();
    expect_locals("call");
    if (failures == 0) $display("PASS");
  end
endmodule
