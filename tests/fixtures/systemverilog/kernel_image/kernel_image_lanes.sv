// SPDX-License-Identifier: Apache-2.0
// Kernel image witness: identical lane instances (members sharing one body),
// combinational logic that calls a function (inlined into the member body),
// a memory written and read by clocked logic, and a combinational chain.
`timescale 1ns/1ps
module kernel_image_lane #(parameter int SEED = 1) (
    input logic clk,
    input logic rst,
    output logic [7:0] value);
    logic [7:0] state;
    logic [7:0] mixed;
    logic [7:0] memory [0:7];
    logic [2:0] address;

    function automatic logic [7:0] scramble(input logic [7:0] x);
        return {x[6:0], x[7] ^ x[5]} + 8'(SEED);
    endfunction

    always_comb mixed = scramble(state) ^ memory[address];

    always_ff @(posedge clk) begin
        if (rst) begin
            state <= 8'(SEED);
            address <= '0;
        end else begin
            state <= mixed;
            memory[address] <= state;
            address <= address + 3'd1;
        end
    end

    assign value = mixed;
endmodule

module kernel_image_lanes;
    logic clk = 1'b0;
    logic rst = 1'b1;
    logic [7:0] lane_value [0:3];
    logic [7:0] folded;

    for (genvar lane = 0; lane < 4; ++lane) begin : lanes
        kernel_image_lane #(.SEED(lane * 37 + 5)) unit (
            .clk(clk), .rst(rst), .value(lane_value[lane]));
    end

    always_comb folded = lane_value[0] + lane_value[1] - lane_value[2] ^ lane_value[3];

    always #5 clk = ~clk;

    initial begin
        for (int lane = 0; lane < 8; ++lane) begin
            lanes[0].unit.memory[lane] = 8'(lane * 3);
            lanes[1].unit.memory[lane] = 8'(lane * 5);
            lanes[2].unit.memory[lane] = 8'(lane * 7);
            lanes[3].unit.memory[lane] = 8'(lane * 11);
        end
        repeat (2) @(posedge clk);
        rst = 1'b0;
        repeat (24) begin
            @(negedge clk);
            $display("t=%0t lanes=%h %h %h %h folded=%h", $time,
                lane_value[0], lane_value[1], lane_value[2], lane_value[3], folded);
        end
        $finish;
    end
endmodule
