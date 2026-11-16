// fsim: top=sv_kernel_string_ops std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.16, 11.4.13: string variables of automatic tasks, held
// by the compiled kernel. Assignment, literals, concatenation (into a new
// string, appended to its first operand, and with the target as a later
// operand), equality, len(), indexing (out of range and unknown indices
// read 0), byte replacement (out of range and zero bytes leave the string
// unchanged), and strings kept across a suspension.
module sv_kernel_string_ops;
  int failures;

  task automatic check(input string what, input string got, input string want);
    if (got != want) begin
      $display("FAIL %s: got \"%s\" want \"%s\"", what, got, want);
      failures++;
    end
  endtask

  task automatic check_int(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  task automatic strings();
    string a;
    string b;
    string c;
    string e;
    integer unknown;
    int sum;
    a = "hello";
    b = a;
    c = {a, " ", b};
    check("concatenation", c, "hello hello");
    check_int("length", c.len(), 11);
    a = {a, a};
    check("appended to itself", a, "hellohello");
    b = {"<", b, ">", b};
    check("target as a later operand", b, "<hello>hello");
    check_int("equal", a == "hellohello", 1);
    check_int("not equal", a != c, 1);
    e = "";
    check_int("empty", e == "", 1);
    for (int i = 0; i < 40; i++) e = {e, "x"};
    check_int("grown", e.len(), 40);
    sum = 0;
    for (int i = 0; i < c.len(); i++) sum += c[i];
    check_int("bytes", sum, 1096);
    check_int("negative index", c[-1], 0);
    check_int("index past the end", c[99], 0);
    unknown = 'x;
    check_int("unknown index", c[unknown], 0);
    c[0] = "J";
    c[1] = 0;
    c[50] = "Q";
    check("replaced byte", c, "Jello hello");
    #10;
    check("kept across a wait", c, "Jello hello");
    check("kept appended", a, "hellohello");
    check_int("kept length", e.len(), 40);
  endtask

  initial begin
    strings();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
