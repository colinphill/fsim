// SPDX-License-Identifier: Apache-2.0
// Live class objects, constraints, size changes and reclamation through VPI
// (IEEE 1800-2023 37.17, 37.33, 37.34, 37.38, 38.36.1).
module top;
  class Item;
    rand int unsigned value;
    string label;
    Item next;
    int w;
    int arr[4];
    static int count;
    constraint c_small { value < 100; value > 1; }
    constraint cond { if (value > 50) { value < 80; } else value < 10; }
    constraint impl { value == 7 -> value != 9; }
    constraint dst { value dist { 1 := 2, [20:30] :/ 3 }; }
    constraint ord { solve value before w; }
    constraint each { foreach (arr[i]) arr[i] < 4; }
    // Expression trees (37.78): inside with a range and a value, nested
    // arithmetic under a comparison, and a negated comparison.
    constraint ops {
      value inside {[1:3], 7};
      (value + 1) * 2 > 4;
      !(w == 0);
    }
    // Selects (37.19): arr[2] selects an element of the unpacked array
    // property (vpiVarSelect, one constant index), value[3:0] a part of the
    // packed property (vpiPartSelect with constant bounds). Constants read
    // the value they spell (37.59): 2 and 0 are 32-bit integers, 4'ha is a
    // four-bit literal with value 10.
    constraint sel {
      arr[2] > 0;
      value[3:0] == 4'ha;
    }
  endclass
  Item a, b, c;
  int dyn[];
  int q[$];
  int aa[int];
  string s;
  initial begin
    #1 a = new;
    a.value = 5;
    a.label = "hi";
    #1 b = new;
    a.next = b;
    #1 dyn = new[3];
    #1 q = {1, 2};
    #1 a.c_small.constraint_mode(0);
    // t=7: the first entry of the associative array makes its size 1; t=8:
    // the string gets 5 characters; t=9: an existing entry changes, no size
    // change (IEEE 1800-2023 37.17 detail 14).
    #2 aa[7] = 1;
    #1 s = "hello";
    #1 aa[7] = 2;
    #2 $finish;
  end
endmodule
