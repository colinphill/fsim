// SPDX-License-Identifier: Apache-2.0
// Design for the VPI data-model plug-in (IEEE 1800-2023 37.7, 37.11,
// 37.5, 37.10, 37.14, 37.15, 37.16-37.18, 37.25, 37.26, 37.28, 37.85).
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
typedef struct { int a; logic [7:0] b; } rec_t;
typedef union packed { logic [7:0] raw; pair_t p; } word_t;
typedef enum logic [1:0] { IDLE, RUN = 2, DONE } state_t;
// The typedefs of IEEE 1800-2023 37.25 details 1, 6 and 8.
typedef enum bit [0:2] { red, yellow, blue } primary_colors;
typedef primary_colors colors;
typedef time my_time;
typedef struct packed { int i1; bit bvec; } elem_t;
typedef elem_t [1:3] parrtype;
typedef parrtype [2:1] parrtype2;
typedef parrtype2 unparrtype [6:4];

module leaf(input logic [3:0] a, output logic y, inout wire z);
  assign y = ^a;
endmodule

interface bus_if;
  logic [7:0] data;
  logic valid;
  modport src(output data, output valid);
  modport dst(input data, input valid);
endinterface

module sink(bus_if.dst b);
endmodule

module top;
  parameter int P = 5;
  localparam int LP = P + 1;
  parameter logic [7:0] PV = 8'h3c;
  pair_t pr = 8'hA5;
  rec_t rc = '{a: 7, b: 8'h3C};
  word_t un = 8'h96;
  state_t st = RUN;
  pair_t [1:0] pa = 16'h1234;
  logic [3:0][7:0] pk = 32'hDEADBEEF;
  logic [7:0] ra [0:1];
  wire [7:0] wa [0:1];
  assign wa[0] = 8'h11;
  assign wa[1] = 8'h22;
  logic [3:0] la = 4'h7;
  logic ly;
  wire lz;
  leaf u0(.a(la), .y(ly), .z());
  leaf ua [1:0] (la, , lz);
  bus_if bus();
  assign bus.data = 8'h3C;
  sink us(.b(bus));
  colors c = yellow;
  my_time t;
  unparrtype arr [3:0];
  parrtype2 p2v;
  int iv;
  int q [$];
  for (genvar i = 0; i < 3; i++) begin : g
    logic [i:0] v;
    typedef int gint_t;
  end
  // An unnamed generate block takes an implicit name (27.6, 37.85).
  if (P > 0) begin
    logic gw;
  end
  // An unnamed loop generate block does too, and its genvar is a local
  // parameter of each scope (37.85 details 2, 3).
  for (genvar k = 0; k < 2; k++) begin
    logic gk;
  end
  // A continuous assignment with more than one delay (37.47, 37.80).
  wire dly;
  assign #(1, 2, 3) dly = la[0];
  string sv = "a";
  initial begin
    #1 sv = "b";
  end
  tri tn;
  // An implicitly declared net (6.10).
  assign imp = la[0];
  initial begin
    ra[1] = 8'h5A;
    #2 $finish;
  end
  // Typedefs of the module (37.5), a parameter named by one (37.28), and
  // ports connected to expressions (37.14).
  typedef logic [3:0] nib_t;
  typedef int my_int_t;
  typedef int aa_t [string];
  localparam nib_t NP = 4'h5;
  // A parameter declared with a typedef of a real or a string (37.28).
  typedef real real_t;
  localparam real_t RP = 2.5;
  // A parameter of a typedef of real is a real parameter in simulation
  // too (6.20.2).
  initial $display("#c RP display=%f", RP);
  typedef string str_t;
  localparam str_t SP = "hi";
  // A parameter of an unpacked structure type is a constant structure var
  // (37.28; 37.26).
  localparam rec_t RS = '{a: 3, b: 8'h5A};
  // The else branch of a generate if is a scope of its own, and a named
  // block of its process is a scope beneath it (27.5, 37.43, 37.85).
  if (P < 0) begin : ge_if
    logic a1;
  end else begin : ge_else
    logic b1;
    initial begin : eb
      b1 = 1'b1;
    end
  end
  // An else branch named like its if branch shares its scope (27.5).
  if (P < 0) begin : shr
    logic s1;
  end else begin : shr
    logic s2;
    initial begin : sb
      s2 = 1'b0;
    end
  end
  leaf u1(.a({la[1:0], la[3:2]}), .y(), .z());
  leaf u2(.a(4'h9), .y(), .z());
  leaf u3(.a(la[3:0]), .y(), .z());
endmodule

// A type a wildcard import makes visible in a package (26.3).
package pkg_q;
  typedef bit [2:0] q_t;
endpackage
// The typedefs of a package (3.12.1, 37.10); the member type of a structure
// written in place is found through the package's import.
package pkg_p;
  import pkg_q::*;
  typedef int pk_int_t;
  typedef logic [1:0] pk_pair_t;
  typedef struct packed { q_t a; } pk_sq_t;
endpackage
