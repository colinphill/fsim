// fsim: top=sv_force_switches_and_strengths
// IEEE 1800-2017 10.6.2: a force keeps its expression applied until it is
// released, and overrides a procedural assign; 23.3.3: an inout port
// connected to a bit-select joins the selected net; 28.8: a tran passes
// values both ways, including high impedance; 21.2.1.5: %v shows a net's
// strength.
module buffer_driver(inout wire pin, input wire value, input wire enable);
  assign pin = enable ? value : 1'bz;
endmodule

module sv_force_switches_and_strengths;
  wire [1:0] forced_net;
  assign forced_net = 2'd0;
  reg [1:0] source;
  reg held;
  reg target;

  wire [3:0] bus;
  reg drive_value, drive_enable;
  buffer_driver driver(bus[1], drive_value, drive_enable);

  wire left, right;
  reg left_value;
  assign left = left_value;
  tran link(left, right);

  wire pulled;
  assign (pull1, strong0) pulled = 1'b1;

  int failures = 0;

  initial begin
    force forced_net = source;
    source = 2'd1;
    #1 if (forced_net !== 2'd1) failures++;
    source = 2'd2;
    #1 if (forced_net !== 2'd2) failures++;
    release forced_net;
    #1 if (forced_net !== 2'd0) failures++;

    held = 1'b0;
    force target = 1'b1;
    assign target = held;
    #1 if (target !== 1'b1) failures++;
    release target;
    #1 if (target !== 1'b0) failures++;
    deassign target;

    drive_value = 1'b1;
    drive_enable = 1'b1;
    #1 if (bus !== 4'bzz1z) failures++;
    drive_enable = 1'b0;
    #1 if (bus !== 4'bzzzz) failures++;

    left_value = 1'b1;
    #1 if (right !== 1'b1) failures++;
    left_value = 1'bz;
    #1 if (right !== 1'bz) failures++;

    if ($sformatf("%v", pulled) != "Pu1") failures++;

    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
