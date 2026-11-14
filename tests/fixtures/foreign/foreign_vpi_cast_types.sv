// SPDX-License-Identifier: Apache-2.0
// A cast to a user-defined type has the typespec of the typedef the compiler
// resolved the name to (IEEE 1800-2023 6.24.1, 26.3, 37.59 details 3 and 5):
// an imported name, a `pkg::T` name, a local typedef that shadows an imported
// one, and two packages that declare the same name with only one imported.
package pa;
  typedef logic [3:0] nib_t;
  typedef logic [5:0] six_t;
endpackage
package pb;
  typedef logic [7:0] nib_t;
endpackage
module top;
  import pa::*;
  typedef bit [1:0] six_t;
  typedef bit [2:0] three_t;
  logic [7:0] a = 8'hC5;
  logic [15:0] y;
  initial begin : main
    y = nib_t'(a);
    y = pb::nib_t'(a);
    y = pa::six_t'(a);
    y = six_t'(a);
    y = three_t'(a);
  end
endmodule
