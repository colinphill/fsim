// SPDX-License-Identifier: Apache-2.0
//
// Class objects a module's process allocates and whose properties it reads
// and writes (IEEE 1800-2023 8.4, 8.6, 8.9): the process runs in compiled
// member code, the allocations and property accesses through their own
// runtime helpers.
//   8.7: new(3, 4) runs the constructor: x = 3, y = 4, count = 1;
//   p.x += 10 -> 13; a second object q = new(5, 6) -> count = 2;
//   8.9: point::count is shared: q's constructor sees count 1 and stores
//   order = 2; p's order stays 1;
//   8.4: q = p copies the handle: q.y = 40 writes p's y.
//   wide (a 100-bit property) holds 1 << 99 | 7: its top bit and low bits.
class point;
  int x;
  int y;
  int order;
  bit [99:0] wide;
  static int count;
  function new(int a, int b);
    x = a;
    y = b;
    count++;
    order = count;
  endfunction
endclass

module top;
  initial begin
    point p;
    point q;
    p = new(3, 4);
    p.x = p.x + 10;
    q = new(5, 6);
    $display("inl objects %0d %0d %0d %0d %0d %0d count=%0d", p.x, p.y, p.order, q.x, q.y,
      q.order, point::count);
    q = p;
    q.y = 40;
    p.wide = (100'd1 << 99) | 100'd7;
    $display("inl alias %0d %0d top=%0d low=%0d", p.y, q.x, q.wide[99], q.wide[2:0]);
    point::count = 9;
    $display("inl static %0d", point::count);
  end
endmodule
