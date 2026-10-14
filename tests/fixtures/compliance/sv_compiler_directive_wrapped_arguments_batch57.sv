// fsim: top=b57_directive_wrapped_arguments std=2017
// SPDX-License-Identifier: Apache-2.0
`default_nettype
  /* comment before the operand */ wire
`begin_keywords
  // the quoted version is on a later line
  "1364-2005"
module logic;
endmodule
`end_keywords
`unconnected_drive
  /* a comment may separate directive and argument */
  pull0 module b57_directive_wrapped_arguments;
  initial $display("PASS");
endmodule
`nounconnected_drive
