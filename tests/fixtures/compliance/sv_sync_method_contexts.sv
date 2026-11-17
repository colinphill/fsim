// fsim: top=sv_sync_method_contexts std=2017
// SPDX-License-Identifier: Apache-2.0
// A mailbox's and a semaphore's methods are functions of type int (IEEE
// 1800-2023 15.3, 15.4): their calls are operands of every integral
// expression form, a conditional's condition and arms (11.4.11), a
// concatenation and replication operand (11.4.12), a case selector and
// case item (12.5), a unary and binary operand, and an argument of
// $sformatf (21.3.3). A typeless mailbox's try_peek of another type is
// negative (15.4.8).
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_sync_method_contexts;
  mailbox #(int) box = new;
  mailbox any = new;
  semaphore sem = new(1);
  int failures;
  int value;
  int n;
  string s;
  bit [39:0] joined;
  real r;
  initial begin
    box.put(5);
    any.put(7);
    n = box.num() > 0 ? box.try_peek(value) : -1;
    `CHECK("conditional arm", n == 1)
    n = box.num() == 0 ? 2 : any.try_peek(r);
    `CHECK("negative conditional arm", n < 0)
    n = sem.try_get(1) ? 3 : 4;
    `CHECK("conditional condition", n == 3)
    n = sem.try_get(1) ? 3 : 4;
    `CHECK("conditional condition false", n == 4)
    sem.put(1);
    joined = {box.num(), 8'h0};
    `CHECK("concatenation", joined == 40'h100)
    `CHECK("concatenation width", $bits({box.num(), 8'h0}) == 40)
    `CHECK("replication", {2{box.num()}} == 64'h0000_0001_0000_0001)
    `CHECK("unary", -box.num() == -1 && ~box.num() == -2 && !box.num() == 0)
    `CHECK("binary", box.num() + any.num() == 2 && (box.num() << 3) == 8)
    `CHECK("negative binary", any.try_peek(r) + 1 <= 0)
    case (box.num())
      1: n = 10;
      default: n = 11;
    endcase
    `CHECK("case selector", n == 10)
    case (1)
      any.num(): n = 12;
      default: n = 13;
    endcase
    `CHECK("case item", n == 12)
    s = $sformatf("%0d/%0d/%0d", box.num(), sem.try_get(1), any.try_peek(r) < 0);
    `CHECK("sformatf arguments", s == "1/1/1")
    if (failures == 0) begin
      $display("PASS");
    end
  end
endmodule
