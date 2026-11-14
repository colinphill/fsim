// SPDX-License-Identifier: Apache-2.0
// The drivers of nets (IEEE 1800-2023 37.21): the output terminal of a gate
// connected to a net, a continuous assignment, and a module output port that
// drives a parent net through the port, on a scalar net and on the bits of a
// vector net.
module leaf (input logic a, output logic y);
    assign y = ~a;
endmodule

module top;
    logic a = 1'b1;
    wire [3:0] v;
    wire w, s;
    wire [1:0] p;
    not g_bit (v[0], a);
    assign v[1] = a;
    leaf l_bit (a, v[2]);
    buf g_whole (w, a);
    leaf l_whole (a, s);
    not g_vec [0:1] (p, a);
endmodule
