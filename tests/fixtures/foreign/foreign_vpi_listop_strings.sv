// SPDX-License-Identifier: Apache-2.0
// Design for the VPI list-operation and string-watch plug-in: the connection list of an instance
// array (IEEE 1800-2023 37.11 detail 1), parameters of a generate scope
// declared with a typedef (37.28, 37.85), and value-change watches on a
// string element of an unpacked array and on a string class property
// (38.36.1); unpacked structure parameters whose type is spelled in place
// or declared in a package (37.28, owner decision 2026-11-05).
package lp_pkg;
  typedef struct { int a; logic [7:0] b; } prec_t;
  localparam prec_t PK = '{a: 9, b: 8'h21};
endpackage

module leaf(input logic [3:0] a, output logic y);
  assign y = ^a;
endmodule

module top;
  logic [3:0] la = 4'h7;
  logic [1:0] ly;
  // The connections of an instance array, by name and by expression.
  leaf ua [1:0] (la, ly);
  leaf ub [1:0] ({la[1:0], la[3:2]}, ly);

  typedef logic [3:0] nib_t;
  if (1) begin : g
    typedef int gint_t;
    localparam nib_t NP = 4'h3;
    localparam gint_t GP = 5;
    localparam int BP = 7;
  end

  localparam struct { int a; logic [3:0] b; } RI = '{a: 4, b: 4'h6};
  localparam int PKA = lp_pkg::PK.a;

  class Item;
    string label;
  endclass
  string sa [2];
  Item it;
  initial begin
    sa[0] = "a";
    sa[1] = "b";
    #1 it = new;
    it.label = "x";
    #4 sa[1] = "c";
    #1 it.label = "y";
    #1 $finish;
  end
endmodule
