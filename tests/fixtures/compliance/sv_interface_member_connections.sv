// fsim: top=sv_interface_member_connections
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 25.5: members of an interface instance as event controls,
// concurrent assertion clocks (16.5) and output port actuals (23.3.3);
// 25.9: a class method waits on and writes a member through a virtual
// interface property.
interface member_bus;
  bit clk;
  bit req;
  bit [7:0] data;
  bit [7:0] count;
endinterface

module member_counter(input bit clk, input bit req, output bit [7:0] count);
  always @(posedge clk) if (req) count <= count + 1;
endmodule

class member_driver;
  virtual member_bus vif;
  function new(virtual member_bus bus);
    vif = bus;
  endfunction
  task drive(bit [7:0] value);
    @(posedge vif.clk);
    vif.data = value;
  endtask
endclass

module sv_interface_member_connections;
  member_bus bus();
  member_counter counter(.clk(bus.clk), .req(bus.req), .count(bus.count));
  member_driver driver;
  int edges;
  int passes;
  int failures;

  always @(posedge bus.clk) edges++;
  assert property (@(posedge bus.clk) bus.req) passes++;

  initial begin
    driver = new(bus);
    bus.req = 1;
    fork
      driver.drive(8'h5a);
      repeat (6) #5 bus.clk = ~bus.clk;
    join
    #1;
    if (edges != 3) failures++;
    if (bus.count != 3) failures++;
    if (bus.data != 8'h5a) failures++;
    if (passes != 3) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
