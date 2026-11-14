// SPDX-License-Identifier: Apache-2.0
// Arrays of events (IEEE 1800-2023 7.4, 37.43 vpiNamedEventArray): bounds far
// from zero, a descending range and an array in a generate scope. VPI finds
// each array by name, iterates its elements in index order (each element's
// parent is the array) and triggers one by index. An event
// property declared through a class-scoped typedef (6.18, 8.18) is a named
// event too, used from a process outside the class with `->` and `@`.
class Holder;
  typedef event ev_t;
  ev_t pe;
endclass

module top;
  event big[5000:5001];
  event d[3:1];
  if (1) begin : g
    event ge[7:8];
    initial begin @(ge[8]); $display("#c ge 8 seen at %0d", $time); end
  end
  Holder h;

  initial begin
    h = new;
    fork
      begin @(big[5001]); $display("#c big 5001 seen at %0d", $time); end
      begin @(h.pe); $display("#c class pe seen at %0d", $time); end
      begin @(d[2]); $display("#c d 2 seen at %0d", $time); end
    join_none
    #5 -> h.pe;
    #1 $display("#c end at %0d", $time);
  end
endmodule
