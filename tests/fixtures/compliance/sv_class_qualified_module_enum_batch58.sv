// fsim: top=sv_class_qualified_module_enum_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0

module sv_uninstantiated_enum_scope_batch58;
  typedef enum integer { A = 99 } unrelated_enum_t;
endmodule

module sv_class_qualified_module_enum_batch58 #(parameter integer VALUE = 1);
  typedef enum integer { A = VALUE + 1 } module_enum_t;

  class enum_holder;
    typedef enum integer { A = 10 } class_enum_t;
    module_enum_t module_value;
    class_enum_t class_value;

    function new();
      module_value = sv_class_qualified_module_enum_batch58.A;
      class_value = A;
    endfunction
  endclass

  enum_holder holder;
  initial begin
    holder = new;
    if (!(holder.module_value == 2))
      $fatal(1, "hierarchical enum reference selected the wrong owner");
    if (!(holder.class_value == 10))
      $fatal(1, "unqualified enum reference lost class scope");
    $display("PASS");
    $finish;
  end
endmodule
