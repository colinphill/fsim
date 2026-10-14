// fsim: top=sv_class_global_void_shadow_precedence_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
package b58_void_shadow_precedence_pkg;
  integer imported_sum = 0;
  function void b58_global_add(input integer amount);
    imported_sum += amount;
  endfunction
endpackage

// A compilation-unit declaration takes precedence over the wildcard import.
import b58_void_shadow_precedence_pkg::*;
integer local_sum = 0;
function void b58_global_add(input integer amount);
  local_sum += amount;
endfunction

class b58_shadow_caller;
  task invoke;
    b58_global_add(7);
  endtask
endclass

module b58_nearer_scope_control(
  output integer observed_sum,
  output logic done
);
  import b58_void_shadow_precedence_pkg::*;
  integer module_sum = 0;

  function void b58_global_add(input integer amount);
    module_sum += amount;
  endfunction

  initial begin
    b58_global_add(3);
    observed_sum = module_sum;
    done = 1'b1;
  end
endmodule

module sv_class_global_void_shadow_precedence_batch58;
  b58_shadow_caller caller = new;
  integer module_local_sum;
  logic scope_control_done;
  b58_nearer_scope_control scope_control(
    .observed_sum(module_local_sum),
    .done(scope_control_done)
  );

  initial begin
    caller.invoke();
    wait (scope_control_done);
    if (local_sum !== 7
        || module_local_sum !== 3
        || b58_void_shadow_precedence_pkg::imported_sum !== 0) begin
      $fatal(1, "a local function lost to an imported same-name function");
    end
    $display("PASS");
  end
endmodule
