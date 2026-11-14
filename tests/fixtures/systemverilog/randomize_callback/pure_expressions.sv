// SPDX-License-Identifier: Apache-2.0
// Pure integral expressions in post_randomize, which randomize()
// calls: $bits (IEEE 1800-2023 20.6.2), $clog2 (20.8.1), the bit vector
// functions (20.9), $signed/$unsigned (11.7), bit-selects and part-selects
// of descending and ascending vectors, out of range and with an unknown
// index (11.5.1), concatenation and replication (11.4.12), the conditional
// operator with a known and an unknown condition (11.4.11), wildcard
// equality (11.4.6), inside (11.4.13) and casez/casex (12.5.1). The values
// are those the same expressions give in a module.
typedef logic [3:0] nib_t;
typedef enum logic [1:0] {RED, GREEN, BLUE} color_t;
class item;
  rand bit [1:0] x;
  logic [3:0] b = 4'b0110;
  logic [0:7] up = 8'b1000_0001;
  logic [3:0] u = 4'b1x0z;
  nib_t n = 4'hA;
  int i = -6;
  bit [7:0] two = 8'hA5;
  color_t color = BLUE;
  function void post_randomize();
    logic [3:0] t;
    t = 4'b0101;
    $display("bits %0d %0d %0d %0d %0d %0d", $bits(b), $bits(b + 1'b1), $bits(n), $bits({b, t}), $bits(color), $bits(i));
    $display("clog2 %0d %0d %0d %0d %0d %0d", $clog2(0), $clog2(1), $clog2(9), $clog2(16), $clog2(b), $clog2(two));
    $display("count %0d %0d %0d %0d %b %b %b %b %b", $countones(b), $countbits(u, 1'bx, 1'bz), $countbits(u, '0), $countbits(two, '1, '0), $onehot(b), $onehot(4'b0100), $onehot0(4'b0000), $isunknown(b), $isunknown(u));
    $display("sign %0d %0d %0d", $signed(4'b1110), $unsigned(i), $signed(b[2:1]));
    $display("select %b %b %0d %0d %0d %b %0d %0d %0d %b %b %b %b %b", b[1], b[0], b[2:1], b[1+:2], b[3-:2], up[0], up[0:3], up[6+:2], up[3-:2], b[4], two[9], b[u[0]], i[31], n[3]);
    $display("concat %h %b %b", {b, t}, {2{b[1:0]}}, {b[3], 2'b10, {3{1'b1}}});
    $display("cond %0d %b %0d", b[0] ? 1 : 2, u[0] ? 4'b1100 : 4'b1010, (b == 4'b0110) ? 7 : 8);
    $display("wild %b %b %b %b", b ==? 4'b01x0, b !=? 4'b1xxx, u ==? 4'b1x0x, b ==? u);
    $display("inside %b %b %b %b %b", b inside {[1:3], 7}, b inside {[5:7]}, b inside {4'b01?0}, u inside {4'b0000}, u inside {4'b1000});
    casez (b) 4'b1???: $display("casez first"); 4'b?11?: $display("casez second"); default: $display("casez default"); endcase
    casez (u) 4'b0???: $display("casez u first"); 4'b1?0?: $display("casez u second"); default: $display("casez u default"); endcase
    casex (u) 4'b0xxx: $display("casex first"); 4'b10x1: $display("casex second"); default: $display("casex default"); endcase
  endfunction
endclass

module top;
  item it = new;
  initial $display("result=%0d", it.randomize());
endmodule
