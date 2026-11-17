// SPDX-License-Identifier: Apache-2.0
// Every message of a mailbox, oldest first (IEEE 1800-2023 37.33 detail
// 4): a typeless mailbox holding an integral value, a string, an unpacked
// structure (a message with no one value, 37.26 detail 1, 38.15) and an
// integral value; a typed mailbox of an unpacked structure type.
typedef struct { string name; int n; } tagged_t;
module top;
  mailbox any = new();
  mailbox #(tagged_t) tags = new();
  tagged_t tag = '{"t", 2};
  string word = "word";
  initial begin
    any.put(3);
    any.put(word);
    any.put(tag);
    any.put(4);
    tags.put(tag);
    tags.put(tag);
  end
  initial #3 $finish;
endmodule
