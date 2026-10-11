// fsim: top=sv_nonblocking_struct_members
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 10.4.2: nonblocking assignments to members of unpacked
// structures and of unpacked arrays of structures update the members in the
// NBA region, and several such writes in one time step all take effect.
module sv_nonblocking_struct_members;
  typedef struct {
    int foo;
    int bar;
  } entry_t;
  struct {
    logic x;
    logic [3:0] y;
  } flags;
  entry_t table_q [2];
  int failures = 0;

  initial begin
    flags.x = 0;
    flags.y = 4'h1;
    table_q[0].foo = 5;
    table_q[0].bar = 6;
    table_q[1].foo = 7;
    flags.x <= 1;
    flags.y <= 4'h9;
    table_q[0].foo <= 0;
    table_q[0].bar <= 8;
    table_q[1].foo <= table_q[0].foo;
    if (flags.x !== 1'b0 || table_q[0].foo != 5) failures++;
    #1;
    if (flags.x !== 1'b1 || flags.y !== 4'h9) failures++;
    if (table_q[0].foo != 0 || table_q[0].bar != 8) failures++;
    if (table_q[1].foo != 5) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
