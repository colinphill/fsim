// fsim: top=sv_typename_type_parameter std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 20.6.1: $typename of a typedef name, or of a type parameter
// bound to one, names the type the typedef declares with its scope: a
// structure of the instantiating module, a package enumeration or packed
// structure, a typedef renaming a structure, and built-in types.
`define CHECK(what, got, want) \
  if ((got) != (want)) begin \
    $display("FAIL %s: %s", what, got); \
    failures++; \
  end
package sv_typename_type_parameter_pkg;
  typedef enum { A, B } e_t;
  typedef struct packed { bit x; } ps_t;
endpackage

module sv_typename_type_parameter_child #(type T = int, type U = int,
    type V = int) (output string names);
  initial names = {$typename(T), "|", $typename(U), "|", $typename(V)};
endmodule

module sv_typename_type_parameter;
  typedef struct { int a; } s_t;
  typedef s_t s2_t;
  typedef int int_t;
  typedef logic [3:0] nib_t;
  string first;
  string second;
  int failures;

  sv_typename_type_parameter_child #(.T(s_t),
      .U(sv_typename_type_parameter_pkg::e_t), .V(s2_t)) u(first);
  sv_typename_type_parameter_child #(.T(int_t), .U(nib_t),
      .V(sv_typename_type_parameter_pkg::ps_t)) v(second);

  initial begin
    #1;
    `CHECK("structure typedef", $typename(s_t),
      "struct{int a;}sv_typename_type_parameter.s_t")
    `CHECK("renamed structure", $typename(s2_t),
      "struct{int a;}sv_typename_type_parameter.s_t")
    `CHECK("package enumeration",
      $typename(sv_typename_type_parameter_pkg::e_t),
      "enum{A=32'sd0,B=32'sd1}sv_typename_type_parameter_pkg::e_t")
    `CHECK("integer typedef", $typename(int_t), "int")
    `CHECK("type parameters", first,
      {"struct{int a;}sv_typename_type_parameter.s_t|",
       "enum{A=32'sd0,B=32'sd1}sv_typename_type_parameter_pkg::e_t|",
       "struct{int a;}sv_typename_type_parameter.s_t"})
    `CHECK("built-in type parameters", second,
      {"int|logic[3:0]|struct packed{bit x;}",
       "sv_typename_type_parameter_pkg::ps_t"})
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
