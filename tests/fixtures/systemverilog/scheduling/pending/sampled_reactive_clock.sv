program sampled_reactive_driver(
    input logic start,
    output logic clock,
    output logic data,
    output logic gate);
  initial begin
    clock = 1'b0;
    data = 1'b0;
    gate = 1'b1;
    @(posedge start);
    data <= 1'b1;
    clock <= 1'b1;
    #1;
    data <= 1'b0;
    gate <= 1'b0;
    clock <= 1'b0;
    #5;
  end
endprogram

program sampled_reactive_observer(
    input logic clock,
    input logic data,
    input logic gate);
  initial begin
    @(posedge clock);
    $display("REACTIVE posedge sampled=%b rose=%b fell=%b past=%b gated=%b",
             $sampled(data),
             $rose(data, @(posedge clock)),
             $fell(data, @(posedge clock)),
             $past(data, 1, , @(posedge clock)),
             $past(data, 1, gate, @(posedge clock)));
    #0;
    $display("RE-INACTIVE posedge clock=%b", clock);

    @(negedge clock);
    $display("REACTIVE negedge sampled=%b rose=%b fell=%b past=%b gated=%b",
             $sampled(data),
             $rose(data, @(negedge clock)),
             $fell(data, @(negedge clock)),
             $past(data, 1, , @(negedge clock)),
             $past(data, 1, gate, @(negedge clock)));
    #0;
    $display("RE-INACTIVE negedge clock=%b", clock);
    $strobe("POSTPONED clock=%b data=%b", clock, data);
    #5;
  end
endprogram

module sampled_reactive_clock;
  logic start;
  logic clock;
  logic data;
  logic gate;

  sampled_reactive_driver driver(start, clock, data, gate);
  sampled_reactive_observer observer(clock, data, gate);

  initial begin
    start = 1'b0;
    #1 start = 1'b1;
    #1 start = 1'b0;
    #3 $finish;
  end
endmodule
