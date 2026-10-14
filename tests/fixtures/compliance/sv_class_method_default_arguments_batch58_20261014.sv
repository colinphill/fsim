// fsim: top=sv_class_method_default_arguments_batch58_20261014 std=2017
// SPDX-License-Identifier: Apache-2.0

class b58_default_argument_class;
  function integer sum3(integer x = 1, integer y = 2, integer z = 3);
    return x + y + z;
  endfunction
endclass

module sv_class_method_default_arguments_batch58_20261014;
  b58_default_argument_class object_handle = new;

  initial begin
    if (object_handle.sum3() !== 6) $fatal(1, "all defaults");
    if (object_handle.sum3(4) !== 9) $fatal(1, "trailing defaults");
    if (object_handle.sum3(4,) !== 9) $fatal(1, "trailing open actual");
    if (object_handle.sum3(4,,) !== 9) $fatal(1, "multiple open actuals");
    if (object_handle.sum3(4, 6) !== 13) $fatal(1, "last default");
    if (object_handle.sum3(4, 6,) !== 13) $fatal(1, "last open actual");
    if (object_handle.sum3(4,,8) !== 14) $fatal(1, "middle default");
    if (object_handle.sum3(4, 6, 8) !== 18) $fatal(1, "all explicit");
    if (object_handle.sum3(,6) !== 10) $fatal(1, "leading default");
    if (object_handle.sum3(,6,) !== 10) $fatal(1, "leading and trailing defaults");
    if (object_handle.sum3(,6,8) !== 15) $fatal(1, "leading default with explicit tail");
    if (object_handle.sum3(,,8) !== 11) $fatal(1, "first and middle defaults");
    if (object_handle.sum3(.z(8), .x(4)) !== 14) $fatal(1, "named out of order");
    if (object_handle.sum3(.y()) !== 6) $fatal(1, "named open default");
    $display("PASS");
  end
endmodule
