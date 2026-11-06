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
  endclass
  Item a, b, c;
  int dyn[];
  int q[$];
  initial begin
    #1 a = new;
    a.value = 5;
    a.label = "hi";
    #1 b = new;
    a.next = b;
    #1 dyn = new[3];
    #1 q = {1, 2};
    #1 a.c_small.constraint_mode(0);
    #6 $finish;
  end
endmodule
