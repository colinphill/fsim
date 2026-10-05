module sampled_explicit_clock;
  logic clock;
  logic data;
  logic gate;

  initial begin
    clock = 1'b0;
    data = 1'b0;
    gate = 1'b1;
    #1;
    data = 1'b1;
    clock = 1'b1;
    #0;
    data = 1'b0;
    gate = 1'b0;
    clock = 1'b0;
    #0;
    data = 1'b1;
    gate = 1'b1;
    clock = 1'b1;
    #1;
    $finish;
  end

  initial begin
    @(posedge clock);
    $display("SAMPLE posedge-1 value=%b rose=%b past=%b gated=%b",
             $sampled(data),
             $rose(data, @(posedge clock)),
             $past(data, 1, , @(posedge clock)),
             $past(data, 1, gate, @(posedge clock)));

    @(negedge clock);
    $display("SAMPLE negedge value=%b fell=%b past=%b gated=%b",
             $sampled(data),
             $fell(data, @(negedge clock)),
             $past(data, 1, , @(negedge clock)),
             $past(data, 1, gate, @(negedge clock)));

    @(posedge clock);
    $display("SAMPLE posedge-2 value=%b rose=%b past=%b gated=%b",
             $sampled(data),
             $rose(data, @(posedge clock)),
             $past(data, 1, , @(posedge clock)),
             $past(data, 1, gate, @(posedge clock)));
  end
endmodule
