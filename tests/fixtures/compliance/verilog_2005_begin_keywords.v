// fsim: top=verilog_2005_begin_keywords lang=verilog std=2005
`begin_keywords "1364-2005"
// IEEE 1364-2005 19.11: keyword directives are part of Verilog-2005.
module verilog_2005_begin_keywords;
  reg [7:0] logic;  // 'logic' is an identifier in the 1364-2005 keyword set
  initial begin
    logic = 8'h5a;
    if (logic == 8'h5a) $display("PASS");
    else $display("FAIL");
  end
endmodule
`end_keywords
