// SPDX-License-Identifier: Apache-2.0
// The variables of a package are objects of the package (IEEE 1800-2023
// 37.10, 37.17, 37.29): found by `pkg::name`, listed by the package's
// variables, with the package as their scope, readable, writable and
// watchable. The virtual interface variable is one too.
interface SBus #(parameter int W = 8);
  logic [W-1:0] data;
endinterface

package pkg;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } pair_t;
  typedef enum logic [1:0] { E0, E1, E2 } mode_t;
  int px = 3;
  int pu;
  logic [7:0] pl;
  real pr;
  string ps;
  int pa [2];
  int pq [$];
  longint pw;
  pair_t pp;
  mode_t pm;
  virtual SBus pv;
endpackage

module top;
  SBus s1 ();
  SBus #(16) s16 ();
  initial begin
    pkg::pv = s16;
    #1 pkg::pl = 8'hA5;
    pkg::pq.push_back(5);
    pkg::pw = 64'h1_0000_0001;
    pkg::pp = 8'h3C;
    pkg::pm = pkg::E2;
    #1 pkg::px = 9;
    #1 $finish;
  end
endmodule
