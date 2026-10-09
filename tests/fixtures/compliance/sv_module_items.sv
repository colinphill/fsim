// fsim: top=sv_module_items std=2017
// IEEE 1800-2017 6.20.6: a module-level const variable; 6.8: var with an
// implicit (logic) data type; A.1.10: an empty module item (`;`).
module sv_module_items;
  const int limit = 5;
  var logic [3:0] nibble;
  var flag;
  ;
  initial begin
    nibble = 4'd9;
    flag = 1'b1;
    if (limit == 5 && nibble == 4'd9 && flag === 1'b1 && $bits(flag) == 1)
      $display("PASS");
    else
      $display("FAIL");
  end
endmodule
