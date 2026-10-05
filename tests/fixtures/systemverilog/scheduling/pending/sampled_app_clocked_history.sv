module sampled_app_clocked_history;
  logic clock;
  logic data;
  logic gate;

  initial begin
    @(posedge clock);
    $display("APP posedge-1 sampled=%b rose=%b fell=%b past=%b gated=%b",
             $sampled(data),
             $rose(data, @(posedge clock)),
             $fell(data, @(posedge clock)),
             $past(data, 1, , @(posedge clock)),
             $past(data, 1, gate, @(posedge clock)));

    @(posedge clock);
    $display("APP posedge-2 sampled=%b rose=%b fell=%b past=%b gated=%b",
             $sampled(data),
             $rose(data, @(posedge clock)),
             $fell(data, @(posedge clock)),
             $past(data, 1, , @(posedge clock)),
             $past(data, 1, gate, @(posedge clock)));

    @(posedge clock);
    $display("APP posedge-3 sampled=%b rose=%b fell=%b past=%b gated=%b",
             $sampled(data),
             $rose(data, @(posedge clock)),
             $fell(data, @(posedge clock)),
             $past(data, 1, , @(posedge clock)),
             $past(data, 1, gate, @(posedge clock)));
  end

  initial begin
    clock = 1'b0;
    data = 1'b0;
    gate = 1'b1;
    #1 data = 1'b1;
    clock = 1'b1;
    #1 gate = 1'b0;
    clock = 1'b0;
    #1 data = 1'b0;
    clock = 1'b1;
    #1 gate = 1'b1;
    clock = 1'b0;
    #1 data = 1'b1;
    clock = 1'b1;
    #1 $finish;
  end
endmodule
