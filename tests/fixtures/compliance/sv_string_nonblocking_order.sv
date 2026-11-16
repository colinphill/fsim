// fsim: top=sv_string_nonblocking_order std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 10.4.2, 4.6 b), 4.9.4: a nonblocking write of a string
// variable takes its value in the NBA region, in execution order with the
// process's other nonblocking updates: the old value until then, the last
// of two writes wins, a right-hand side reads the value before the update,
// and a process woken by a packed update of the same region sees the
// string already written. Module variables from an initial and an always
// block, and array elements written from a task in a loop.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_string_nonblocking_order;
  int failures;
  string s, t, u;
  string seen;
  logic flag, go;
  string names[3];

  always @(flag) seen = s;
  always @(posedge go) u <= {s, "!"};

  task automatic spell(input int count);
    string word;
    word = "";
    for (int i = 0; i < count; i++) begin
      word = {word, "x"};
      names[i] <= word;
    end
  endtask

  initial begin
    #10;
    spell(3);
    `CHECK("element old value", names[2] == "")
    #1;
    `CHECK("elements written in a loop", names[0] == "x" && names[2] == "xxx")
  end

  initial begin
    flag = 0;
    go = 0;
    s = "a";
    s <= "b";
    `CHECK("old value before the NBA region", s == "a")
    #0;
    `CHECK("old value after #0", s == "a")
    #1;
    `CHECK("new value", s == "b")
    s <= "c";
    s <= "d";
    t <= s;
    #1;
    `CHECK("last write wins", s == "d")
    `CHECK("right-hand side before the update", t == "b")
    s <= "e";
    flag <= 1;
    #1;
    `CHECK("woken process sees the string", seen == "e")
    go = 1;
    #1;
    `CHECK("always block's nonblocking write", u == "e!")
    #10;
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
