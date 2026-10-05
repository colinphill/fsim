// SPDX-License-Identifier: Apache-2.0
program sampled_mixed_active_reactive_observer(
    input logic clock,
    input logic data,
    input integer active_iterations);
  initial begin
    @(posedge clock);
    @(posedge clock);
    $display("MIXED program sampled=%b past=%b active_iterations=%0d",
             $sampled(data),
             $past(data, 1, , @(posedge clock)),
             active_iterations);
    #3;
  end
endprogram

module sampled_mixed_active_reactive;
  logic clock;
  logic data = 1'b1;
  logic active_pulse = 1'b0;
  integer active_iterations = 0;

  sampled_mixed_active_reactive_observer observer(
      clock, data, active_iterations);

  always @(posedge clock) begin
    active_pulse = ~active_pulse;
    if (data) begin
      $display("MIXED module past=%b",
               $past(data, 1, , @(posedge clock)));
    end
  end

  always @(active_pulse) begin
    if (clock) begin
      active_iterations = active_iterations + 1;
    end
  end

  initial begin
    clock = 1'b0;
    data = 1'b0;
    #1 clock = 1'b1;
    #1 begin
      clock = 1'b0;
      data = 1'b1;
    end
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
