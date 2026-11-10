// SPDX-License-Identifier: Apache-2.0
//
// The assertion action control tasks (IEEE 1800-2023 20.11) stop the pass or
// fail statement itself, not only its output: a statement that only counts
// is skipped too, on the interpreter and in generated code (the outcome pop
// of the directive's block). PassOn and PassOff (6, 7) cover vacuous and
// nonvacuous successes, NonvacuousOn (10) turns on the nonvacuous ones only,
// VacuousOff (11) turns off the vacuous ones only until a PassOn, FailOn and
// FailOff (8, 9) cover failures. They apply to assert and cover directives
// and to expect statements (20.11); the system tasks equal $assertcontrol
// with those control types.
//
// c1 ticks at 10 j + 5 (j = 0..29); every attempt below ends at the tick it
// starts at. The controls change between ticks, at multiples of 20:
//
//   ticks      control before them      nonvacuous  vacuous  failure
//   5, 15      (none)                       on         on       on
//   25, 35     $assertvacuousoff            on         off      on
//   45, 55     $assertnonvacuouson          on         off      on
//   65, 75     $assertcontrol(6)            on         on       on
//   85, 95     $assertpassoff               off        off      on
//   105, 115   $assertcontrol(10)           on         off      on
//   125, 135   $assertpasson                on         on       on
//   145, 155   $assertfailoff               on         on       off
//   165, 175   $assertcontrol(8)            on         on       on
//   185, 195   $assertcontrol(9)            on         on       off
//   205, 215   $assertfailon, $assertcontrol(7)  off   off      on
//   225, 235   $assertcontrol(11)           off        off      on
//   245, 255   $assertcontrol(10)           on         off      on
//   265..295   $assertcontrol(6)            on         on       on
//
// So of the 30 ticks the nonvacuous pass statements run at 24, the vacuous
// ones at 16 and the fail statements at 26, for each of the assertion, the
// cover and the expect statement (p*: nonvacuous, v*: vacuous, f*: failure).
module top;
  bit c1;
  always #5 c1 = ~c1;
  logic one = 1'b1, zero = 1'b0;
  int pa, va, fa, pc, vc, pe, ve, fe, never;
  assert property (@(posedge c1) one |-> one) pa++; else never++;
  assert property (@(posedge c1) zero |-> one) va++; else never++;
  assert property (@(posedge c1) one |-> zero) never++; else fa++;
  cover property (@(posedge c1) one |-> one) pc++;
  cover property (@(posedge c1) zero |-> one) vc++;
  initial forever expect (@(posedge c1) one |-> one) pe++; else never++;
  initial forever expect (@(posedge c1) zero |-> one) ve++; else never++;
  initial forever expect (@(posedge c1) one |-> zero) never++; else fe++;
  initial begin
    #20 $assertvacuousoff;
    #20 $assertnonvacuouson;
    #20 $assertcontrol(6);
    #20 $assertpassoff;
    #20 $assertcontrol(10);
    #20 $assertpasson;
    #20 $assertfailoff;
    #20 $assertcontrol(8);
    #20 $assertcontrol(9);
    #20 $assertfailon;
    $assertcontrol(7);
    #20 $assertcontrol(11);
    #20 $assertcontrol(10);
    #20 $assertcontrol(6);
  end
  initial begin
    #300;
    $display("inl actions assert p=%0d v=%0d f=%0d", pa, va, fa);
    $display("inl actions cover p=%0d v=%0d", pc, vc);
    $display("inl actions expect p=%0d v=%0d f=%0d", pe, ve, fe);
    $display("inl actions never=%0d", never);
    $finish;
  end
endmodule
