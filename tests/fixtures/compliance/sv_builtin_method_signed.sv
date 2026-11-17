// fsim: top=sv_builtin_method_signed std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.8.1: an expression's signedness comes from its
// operands, and a built-in method's value has the type its clause
// declares: int for a mailbox's num, try_get, try_peek and try_put (15.4),
// a semaphore's try_get (15.3.4), a string's len, compare and icompare
// (6.16), an enumeration's num (6.19.5.6), an array's size, num and exists
// (7.5.2, 7.9) and an associative array's traversal functions (7.9.4); byte
// for getc and a string's element and integer for atoi (6.16, Table 6-9).
// These are signed (6.11), so a negative result compares, prints and takes
// part in arithmetic as negative, and $bits of such a call is its type's
// width (20.6.2).
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_builtin_method_signed;
  typedef enum { RED, GREEN, BLUE } color_t;
  mailbox box = new;
  mailbox #(int) ints = new(1);
  semaphore sem = new(0);
  string text = "ab";
  string other = "b";
  string high = "a\xff";
  byte character;
  int code;
  int sizes [] = new[2];
  int table_ [int];
  color_t color;
  real value;
  int index;
  int failures;

  initial begin
    box.put(5);
    `CHECK("try_peek of another type is negative", box.try_peek(value) < 0)
    `CHECK("try_peek of another type is not positive", !(box.try_peek(value) > 0))
    `CHECK("try_get of another type plus one", box.try_get(value) + 1 == 0)
    `CHECK("mailbox num arithmetic", box.num() - 2 < 0)
    `CHECK("mailbox try_put", ints.try_put(1) - 2 == -1)
    `CHECK("semaphore try_get", sem.try_get(1) - 1 == -1)
    `CHECK("string len", text.len() - 3 == -1)
    `CHECK("string len prints negative", $sformatf("%0d", text.len() - 3) == "-1")
    `CHECK("string len in a conditional", (text.len() > 0 ? text.len() - 3 : 0) < 0)
    `CHECK("string compare", text.compare(other) < 0)
    `CHECK("string getc", text.getc(5) - 1 == -1)
    `CHECK("string atoi", text.atoi() - 1 == -1)
    `CHECK("enumeration num", color.num() - 4 == -1)
    `CHECK("array size", sizes.size() - 3 == -1)
    `CHECK("associative num", table_.num() - 1 == -1)
    `CHECK("associative exists", table_.exists(3) - 1 == -1)
    `CHECK("associative first", table_.first(index) - 1 == -1)
    `CHECK("bits of try_peek", $bits(box.try_peek(value)) == 32)
    `CHECK("bits of mailbox num", $bits(box.num()) == 32)
    `CHECK("bits of semaphore try_get", $bits(sem.try_get(1)) == 32)
    `CHECK("bits of len", $bits(text.len()) == 32)
    `CHECK("bits of getc", $bits(high.getc(1)) == 8)
    `CHECK("bits of a string element", $bits(high[1]) == 8)
    `CHECK("getc byte", high.getc(1) == -1 && high.getc(1) + 0 == -1)
    `CHECK("getc prints signed", $sformatf("%0d", high.getc(1)) == "-1")
    `CHECK("string element byte", high[1] == -1 && high[1] + 1 == 0)
    code = high.getc(1);
    character = high[0];
    `CHECK("getc into int", code == -1)
    `CHECK("string element into byte", character == 8'sd97)
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
