// fsim: top=b58_mutable_index_callable_fold std=2017
// SPDX-License-Identifier: Apache-2.0
package b58_mutable_index_pkg;
  int selected_index = 0;
  parameter int fixed_index = 1;

  task automatic set_selected_index(input int value);
    selected_index = value;
  endtask

  function automatic int read_mutable_index();
    int values [0:1];
    values[0] = 10;
    values[1] = 20;
    values[selected_index] = 30;
    return values[0] + values[1];
  endfunction

  function automatic int read_mutable_index_transitive();
    return read_mutable_index();
  endfunction

  function automatic int read_fixed_index();
    int values [0:1];
    values[0] = 10;
    values[1] = 20;
    values[fixed_index] = 30;
    return values[0] + values[1];
  endfunction
endpackage

module b58_mutable_index_callable_fold;
  initial begin
    b58_mutable_index_pkg::set_selected_index(1);
    if (b58_mutable_index_pkg::read_mutable_index() != 40)
      $fatal(1, "mutable external index was folded at its initializer");
    if (b58_mutable_index_pkg::read_mutable_index_transitive() != 40)
      $fatal(1, "transitive mutable callable read was folded");
    if (b58_mutable_index_pkg::read_fixed_index() != 40)
      $fatal(1, "constant parameter index changed callable result");
    $display("PASS");
  end
endmodule
