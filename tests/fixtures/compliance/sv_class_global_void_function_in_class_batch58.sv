// fsim: top=sv_class_global_void_function_in_class_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0

package b58_void_shadow_pkg;
  integer imported_sum = 0;
  function void b58_global_add(input integer amount);
    imported_sum += amount;
  endfunction
endpackage

// A local $unit declaration takes precedence over an imported same-name
// package function (IEEE 1800-2017 3.12.1, 26.3).
import b58_void_shadow_pkg::*;
integer b58_global_sum = 0;

function void b58_global_add(input integer amount);
  b58_global_sum += amount;
endfunction

class b58_void_base;
  function void base_add(input integer amount);
    b58_global_sum += amount;
  endfunction
endclass

class b58_void_derived extends b58_void_base;
  function void derived_add(input integer amount);
    b58_global_sum += amount;
  endfunction

  task invoke_all;
    b58_global_add(10);
    base_add(20);
    derived_add(30);
  endtask
endclass

module sv_class_global_void_function_in_class_batch58;
  b58_void_derived object_handle = new;

  initial begin
    object_handle.invoke_all();
    object_handle.base_add(40);
    object_handle.derived_add(50);
    if (b58_global_sum !== 150
        || b58_void_shadow_pkg::imported_sum !== 0) begin
      $fatal(1, "class call ignored the compilation-unit name shadow");
    end
    $display("PASS");
  end
endmodule
