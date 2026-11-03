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
  end
  // An unnamed generate block takes an implicit name (27.6, 37.85).
  if (P > 0) begin
    logic gw;
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
  leaf u1(.a({la[1:0], la[3:2]}), .y(), .z());
  leaf u2(.a(4'h9), .y(), .z());
  leaf u3(.a(la[3:0]), .y(), .z());
endmodule
