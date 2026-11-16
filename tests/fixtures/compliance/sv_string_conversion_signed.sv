// fsim: top=sv_string_conversion_signed std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.16.8, 6.16.9, 11.8.1: str.atoi(), atohex(), atooct()
// and atobin() return an integer, and compare() and icompare() an int, so
// each is signed as an operand of an arithmetic, relational or shift
// expression and when it is extended into a wider context.
module sv_string_conversion_signed;
  int failures;
  string s;
  longint wide;

  task automatic check(input string what, input bit ok);
    if (!ok) begin
      $display("FAIL %s", what);
      failures++;
    end
  endtask

  initial begin
    s = "-12";
    check("atoi alone", s.atoi() == -12);
    check("atoi below zero", s.atoi() < 0);
    check("atoi plus two", s.atoi() + 2 == -10);
    check("atoi divided", s.atoi() / 4 == -3);
    check("atoi shifted", (s.atoi() >>> 1) == -6);
    wide = s.atoi();
    check("atoi widened", wide == -64'sd12);
    check("atoi in a wider sum", s.atoi() + 64'sd0 == -64'sd12);
    s = "ffffffff";
    check("atohex all ones", s.atohex() < 0);
    s = "37777777777";
    check("atooct all ones", s.atooct() == -1);
    s = "11111111111111111111111111111110";
    check("atobin minus two", s.atobin() + 1 == -1);
    s = "abc";
    check("compare plus zero", s.compare("abd") + 0 < 0);
    check("icompare widened", s.icompare("ABD") + 64'sd0 < 0);
    #1;
    s = "-7";
    check("after a wait", s.atoi() * 3 == -21);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
