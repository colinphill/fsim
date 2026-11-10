// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.8 (Table 6-7), 8.4, 11.4.5, 13.3.2: a class handle
// variable declared without an initializer holds null, so comparing it with
// null gives a known 1 or 0. Method locals, block locals, locals of a
// specialization type and an unassigned output formal of a class type all
// start null, in post_randomize (the class method executor) and in a
// method the module calls.
class base;
endclass

class box #(int P = 1) extends base;
endclass

class R;
  rand bit [1:0] x;
  int r_eq, r_ne, r_rev, r_spec, r_block, r_out, r_if, r_new;

  function void fill(output base o);
  endfunction

  function void check();
    base b;
    box #(7) b7;
    base made;
    base o;
    r_eq = (b == null);
    r_ne = (b != null);
    r_rev = (null == b7);
    r_spec = (b7 == null);
    begin
      base inner;
      r_block = (inner == null);
    end
    o = new;
    fill(o);
    r_out = (o == null);
    if (b == null) r_if = 1;
    else r_if = 2;
    made = new;
    r_new = (made == null);
  endfunction

  function void post_randomize();
    check();
  endfunction
endclass

module top;
  R r;
  int failures;

  task automatic expect_known(string where);
    if (r.r_eq !== 1 || r.r_ne !== 0 || r.r_rev !== 1 || r.r_spec !== 1
        || r.r_block !== 1 || r.r_out !== 1 || r.r_if !== 1
        || r.r_new !== 0) begin
      $display("FAIL %s %0d %0d %0d %0d %0d %0d %0d %0d", where, r.r_eq,
               r.r_ne, r.r_rev, r.r_spec, r.r_block, r.r_out, r.r_if,
               r.r_new);
      failures++;
    end
  endtask

  initial begin
    r = new;
    if (!r.randomize()) begin
      $display("FAIL randomize");
      failures++;
    end
    expect_known("post_randomize");
    r = new;
    r.check();
    expect_known("call");
    if (failures == 0) $display("PASS");
  end
endmodule
