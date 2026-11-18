// fsim: top=sv_sva_sampled_class_property
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.5.1: the sampled value of an object's property is its
// value in the Preponed region of the time slot, read through the objects
// the handles hold there; at time 0 it is the default sampled value, the
// type's default. clk rises at 5, 15, 25, 35 (ticks k = 1..4) and falls at
// 10, 20, 30 (m = 1..3). At time 0 h.p = 7, h.q = 3 and h.in.v = 0, and
// Outer::last is h; each tick adds 1 to p and q and 10 to h.in.v in another
// process of the same time slot, so, whatever the order of the processes:
//   at tick k:  $sampled(h.p) = $sampled(Outer::last.p) = 6 + k,
//               $sampled(h.q) = 2 + k;
//   at fall m:  $sampled(h.p) = 7 + m.
// The fall at 20 replaces h.in by an object whose v is 500, in the slot of a
// reader of $sampled(h.in.v): that reader still reads the old object's 20.
// So $sampled(h.in.v) is 0, 10, 500, 510 at the ticks and 10, 20, 510 at
// the falls. At time 0 every $sampled is the default: 0 for p and v, 4'bx
// for q. The assertion compares $sampled(h.p) with m_p, which the writer
// keeps equal to h.p: it passes at each of the 4 ticks.
class Inner;
  int v;
endclass

class Outer;
  static Outer last;
  Inner in;
  int p;
  logic [3:0] q;
endclass

module sv_sva_sampled_class_property;
  Outer h;
  Inner fresh;
  bit clk;
  int m_p;
  int errors = 0, ticks = 0, falls = 0, downs = 0, passes = 0;
  always #5 clk = ~clk;
  initial begin
    h = new;
    h.in = new;
    h.p = 7;
    h.q = 4'h3;
    m_p = 7;
    Outer::last = h;
    if ($sampled(h.p) !== 0) errors++;
    if ($sampled(h.q) !== 4'bxxxx) errors++;
    if ($sampled(h.in.v) !== 0) errors++;
  end
  always @(posedge clk) begin
    h.p = h.p + 1;
    h.q = h.q + 1;
    h.in.v = h.in.v + 10;
    m_p = h.p;
  end
  always @(posedge clk) begin
    ticks++;
    if ($sampled(h.p) != 6 + ticks) errors++;
    if ($sampled(Outer::last.p) != 6 + ticks) errors++;
    if ($sampled(h.q) !== 4'(2 + ticks)) errors++;
    if ($sampled(h.in.v) != (ticks <= 2 ? 10 * (ticks - 1) : 500 + 10 * (ticks - 3)))
      errors++;
  end
  always @(negedge clk) begin
    falls++;
    if (falls == 2) begin
      fresh = new;
      fresh.v = 500;
      h.in = fresh;
    end
  end
  always @(negedge clk) begin
    downs++;
    if ($sampled(h.p) != 7 + downs) errors++;
    if ($sampled(h.in.v) != (downs <= 2 ? 10 * downs : 510)) errors++;
  end
  a_p: assert property (@(posedge clk) $sampled(h.p) == m_p) passes++; else errors++;
  initial begin
    #38;
    if (errors == 0 && ticks == 4 && downs == 3 && passes == 4)
      $display("PASS");
    else
      $display("FAIL errors %0d ticks %0d downs %0d passes %0d", errors, ticks,
        downs, passes);
    $finish;
  end
endmodule
