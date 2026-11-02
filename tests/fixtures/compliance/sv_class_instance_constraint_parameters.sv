// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.22, 8.25, 18.5: the classes of each module instance read
// that instance's parameters, so their constraints use the instance's values,
// including a localparam computed from them and an inherited constraint.
module m #(int W = 4, int N = 2);
  localparam int L = W * 2;
  class Base;
    rand int x;
    constraint base_c { x >= 0; x < W; }
  endclass
  class Item extends Base;
    rand int y;
    rand int z;
    rand int a[];
    constraint item_c {
      y inside {[0 : L - 1]};
      z dist { W := 1, L := 1 };
      a.size() == N;
    }
  endclass
  int bad;
  int max_x;
  int max_y;
  initial begin
    static Item item = new;
    for (int i = 0; i < 200; i++) begin
      if (item.randomize() != 1) bad++;
      else if (item.x >= W || item.y >= L || (item.z != W && item.z != L)
               || item.a.size() != N)
        bad++;
      if (item.x > max_x) max_x = item.x;
      if (item.y > max_y) max_y = item.y;
    end
  end
endmodule
module top;
  m #(.W(3), .N(5)) a ();
  m #(.W(100)) b ();
  for (genvar g = 0; g < 2; g++) begin : gen
    m #(.W(10 + g)) c ();
  end
  initial begin
    #1;
    if (a.bad == 0 && b.bad == 0 && gen[0].c.bad == 0 && gen[1].c.bad == 0
        && a.max_x == 2 && a.max_y == 5 && b.max_x >= 50 && b.max_y >= 100
        && gen[0].c.max_x == 9 && gen[1].c.max_x == 10)
      $display("PASS");
    else
      $display("FAIL a=%0d/%0d/%0d b=%0d/%0d/%0d g=%0d/%0d", a.bad, a.max_x,
               a.max_y, b.bad, b.max_x, b.max_y, gen[0].c.max_x,
               gen[1].c.max_x);
  end
endmodule
