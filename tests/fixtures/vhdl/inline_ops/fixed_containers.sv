// SPDX-License-Identifier: Apache-2.0
//
// Fixed-size unpacked arrays of an automatic task that the array
// manipulation methods reach as whole values, which the v4 kernel holds as
// its own containers (the dynamic array layout, elements in declared
// order, left first) and runs in compiled member code:
//   7.4.6: a read through an out-of-range or unknown index yields the
//   element type's invalid-index value (int: 0; logic: x); a write through
//   one writes nothing;
//   7.12.1: a locator's index results are declared indices, in element
//   order (left first);
//   7.12.4: item.index is the declared index;
//   7.12.2: rsort and reverse reorder the elements;
//   7.6, 11.4.5: assignment and equality go element by element, whatever
//   the bounds and directions.
// down[3:0] takes down[i] = 10*i + 1: element order 31 21 11 1.
//   find_index (item > 10): 3 2 1; find_first_index (x.index < 2): 1;
//   sum with (item * item.index) = 31*3 + 21*2 + 11*1 + 1*0 = 146.
// up[2:5] takes up[k] = k*k: 4 9 16 25.
//   find_last_index (x < 20): 4; rsort: 25 16 9 4; reverse: 4 9 16 25.
// other[0:3] takes other[i] = down[3 - i]: the same elements as down.
// bytes[0:1] = 8'h0f, 8'hf0: sum 8'hff, an out-of-range read xxxxxxxx.
module top;
  task automatic run();
    int down[3:0];
    int up[2:5];
    int other[0:3];
    int copied[3:0];
    logic [7:0] bytes[0:1];
    logic [3:0] unknown;
    int found[$];
    for (int i = 0; i < 4; i++) down[i] = 10 * i + 1;
    for (int k = 2; k <= 5; k++) up[k] = k * k;
    unknown = 4'bx;
    $display("inl read %0d %0d %0d %0d out=%0d neg=%0d unk=%0d", down[3], down[2], down[1],
      down[0], down[4], down[-1], down[unknown]);
    down[7] = 99;
    $display("inl write %0d %0d %0d %0d", down[3], down[2], down[1], down[0]);
    found = down.find_index with (item > 10);
    $display("inl find_index %0d: %0d %0d %0d", found.size(), found[0], found[1], found[2]);
    found = down.find_first_index(x) with (x.index < 2);
    $display("inl first %0d: %0d", found.size(), found[0]);
    $display("inl sum %0d", down.sum() with (item * item.index));
    found = up.find_last_index(x) with (x < 20);
    $display("inl last %0d: %0d", found.size(), found[0]);
    up.rsort();
    $display("inl sorted %0d %0d %0d %0d", up[2], up[3], up[4], up[5]);
    up.reverse();
    $display("inl reversed %0d %0d %0d %0d", up[2], up[3], up[4], up[5]);
    copied = down;
    for (int i = 0; i < 4; i++) other[i] = down[3 - i];
    $display("inl equal %0d %0d", copied == down, other == down);
    bytes[0] = 8'h0f;
    bytes[1] = 8'hf0;
    $display("inl bytes %h %b", bytes.sum(), bytes[2]);
    foreach (down[k]) $display("inl each %0d=%0d", k, down[k]);
  endtask
  initial run();
endmodule
