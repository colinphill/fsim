// SPDX-License-Identifier: Apache-2.0
module backend_tier_shared_leaf(
  input logic clock,
  input logic enable,
  input logic data,
  output logic result
);
  always @(posedge clock) begin
    if (enable)
      result <= data;
  end
endmodule

module backend_tier_distinct_leaf #(
  parameter integer TAG = 0
)(
  input logic clock,
  input logic [7:0] data,
  output logic result
);
  always @(posedge clock)
    result <= ((data ^ TAG) != 8'hA5);
endmodule

module backend_tier_boundary_runner #(
  parameter integer TARGET_INSTANCES = 63,
  parameter integer LABEL = 63
);
  logic clock;
  logic enable;
  logic target_data;
  logic [7:0] distinct_data;
  wire [TARGET_INSTANCES-1:0] target_results;
  wire [64:0] distinct_results;

  for (genvar target_index = 0;
       target_index < TARGET_INSTANCES;
       target_index = target_index + 1) begin : target_instances
    backend_tier_shared_leaf target(
      .clock(clock),
      .enable(enable),
      .data(target_data),
      .result(target_results[target_index])
    );
  end

  for (genvar distinct_index = 0;
       distinct_index < 65;
       distinct_index = distinct_index + 1) begin : distinct_instances
    backend_tier_distinct_leaf #(.TAG(distinct_index)) distinct(
      .clock(clock),
      .data(distinct_data),
      .result(distinct_results[distinct_index])
    );
  end

  initial begin
    clock = 1'b0;
    enable = 1'b1;
    target_data = 1'b0;
    distinct_data = 8'h5A;
    #1;
    target_data = 1'b1;
    #1;
    clock = 1'b1;
    #1;
    if (target_results !== {TARGET_INSTANCES{1'b1}})
      $fatal(1, "shared template outputs did not all update");
    if (distinct_results !== {65{1'b1}})
      $fatal(1, "distinct template outputs did not all update");
    $display("BACKEND_TIER_BOUNDARY_%0d PASS", LABEL);
    $finish;
  end
endmodule

module backend_tier_boundary_63;
  backend_tier_boundary_runner #(
    .TARGET_INSTANCES(63),
    .LABEL(63)
  ) runner();
endmodule

module backend_tier_boundary_64;
  backend_tier_boundary_runner #(
    .TARGET_INSTANCES(64),
    .LABEL(64)
  ) runner();
endmodule
