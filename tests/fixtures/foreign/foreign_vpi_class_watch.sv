// SPDX-License-Identifier: Apache-2.0
// cbValueChange on the properties of a live class object (IEEE 1800-2023
// 37.33, 38.36.1). From the LRM: a callback occurs after each value change,
// so the two writes of w in bump2 give two callbacks (values 1 and 2), and
// so do the two writes of arr[2] (5 and 6) and of the static sarr[1] (7 and
// 8); w = 3 at t=4 gives one more. A string, a queue and a class object
// are excluded from value change callbacks (38.36.1): their registration
// fails with an error. The static element reads 8 at t=5.
module top;
  class Item;
    int w;
    int arr[4];
    string label;
    int q[$];
    Item next;
    static int sarr[3];
    function void bump2();
      w = 1;
      w = 2;
      arr[2] = 5;
      arr[2] = 6;
    endfunction
    static function void sbump();
      sarr[1] = 7;
      sarr[1] = 8;
    endfunction
  endclass
  Item it;
  initial begin
    it = new;
    #2 it.bump2();
    #1 Item::sbump();
    #1 it.w = 3;
    #2 $display("#c done w=%0d arr2=%0d sarr1=%0d", it.w, it.arr[2],
      Item::sarr[1]);
  end
endmodule
