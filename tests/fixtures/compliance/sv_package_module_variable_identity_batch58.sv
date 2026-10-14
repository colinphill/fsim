// fsim: top=b58_package_module_variable_identity std=2017
// SPDX-License-Identifier: Apache-2.0
package b58_package_module_variable_identity_pkg;
  logic [3:0] v = 4'h0;
  function automatic logic [3:0] get_v();
    return v;
  endfunction
endpackage

module b58_package_module_variable_identity;
  logic [7:0] v;
  initial begin
    v = 8'h05;
    if (b58_package_module_variable_identity_pkg::get_v() !== 4'h0)
      $fatal(1, "package getter read same-named module variable");
    b58_package_module_variable_identity_pkg::v = 4'h9;
    if (b58_package_module_variable_identity_pkg::get_v() !== 4'h9)
      $fatal(1, "package getter did not read package storage");
    if (v !== 8'h05)
      $fatal(1, "package write changed same-named module variable");
    $display("PASS");
    $finish;
  end
endmodule
