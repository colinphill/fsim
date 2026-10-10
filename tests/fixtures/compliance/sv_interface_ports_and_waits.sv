// fsim: top=sv_interface_ports_and_waits std=2017
// IEEE 1800-2017 25.5: an interface port array connected element by element,
// and a non-ANSI interface port declared in the module body; 23.3.2.1:
// positional connections follow the header order of non-ANSI ports; 25.9:
// event controls and waits on a virtual interface member; 9.2.2.4: an
// always_ff with an asynchronous reset event; 9.4.2: the edge event.
interface sv_ports_bus;
  logic [7:0] data;
  logic       flag;
endinterface

module sv_ports_array_user (sv_ports_bus ports[2]);
  initial #1 ports[1].data = ports[0].data + 8'd1;
endmodule

module sv_ports_counter (clk, bus, rst_n);
  input clk;
  sv_ports_bus bus;
  input rst_n;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) bus.data <= 8'd0;
    else bus.data <= bus.data + 8'd1;
endmodule

module sv_interface_ports_and_waits;
  sv_ports_bus pair[2] ();
  sv_ports_bus counted ();
  sv_ports_bus watched ();
  virtual sv_ports_bus handle;
  logic clk = 0, rst_n = 1;
  int edges, failures;

  sv_ports_array_user user (.ports(pair));
  sv_ports_counter counter (clk, counted, rst_n);

  always @(edge clk) edges++;

  initial begin
    pair[0].data = 8'd41;
    #2;
    if (pair[1].data != 8'd42) failures++;

    rst_n = 0;
    #1 rst_n = 1;
    repeat (3) begin
      #1 clk = 1;
      #1 clk = 0;
    end
    #1;
    if (counted.data != 8'd3 || edges != 6) failures++;

    handle = watched;
    watched.data = 0;
    watched.flag = 0;
    fork
      begin
        @(handle.data);
        if (handle.data != 8'd7) failures++;
        @(posedge handle.flag);
        wait (handle.data == 8'd9);
      end
      begin
        #1 watched.data = 8'd7;
        #1 watched.flag = 1;
        #1 watched.data = 8'd9;
      end
    join
    if ($time != 13) failures++;

    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
