// fsim: top=d8_escaped_dotted_identifier std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 5.6.1: the backslash and trailing white space are not part of
// an escaped identifier, so \bot.r  declares the identifier "bot.r"; the
// hierarchical reference bot.r (23.6) is a path into instance bot and must
// not match that declaration.
module bot;
  reg r;
  initial r = 1'b0;
endmodule

module d8_escaped_dotted_identifier;
  reg \bot.r ;
  bot bot ();
  initial begin
    \bot.r = 1'b1;
    #1;
    if (bot.r === 1'b0 && \bot.r === 1'b1) $display("PASS");
    else $display("FAIL bot.r=%b esc=%b", bot.r, \bot.r );
  end
endmodule
