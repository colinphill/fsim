// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// Subroutines a modport exports (IEEE 1800-2017 25.7.3): a task given by
// its prototype in the modport and an `extern` interface function, both
// defined in the module connected through the exporting modport as
// `port.name`, and called through an instance's importing modport and
// through an interface port.
interface bus_if;
  logic [7:0] data;
  extern function logic [7:0] twice(input logic [7:0] v);
  modport provider(output data, export task put(input logic [7:0] v),
      export twice);
  modport consumer(input data, import task put(input logic [7:0] v),
      import twice);
endinterface

module provider_m(bus_if.provider port);
  task port.put(input logic [7:0] v);
    port.data = v + 8'd1;
  endtask
  function logic [7:0] port.twice(input logic [7:0] v);
    return v * 2;
  endfunction
endmodule

module user_m(bus_if.consumer port, output logic [7:0] seen);
  initial begin
    #1;
    port.put(8'd20);
    seen = port.twice(port.data);
  end
endmodule

module top;
  bus_if bif ();
  logic [7:0] seen;
  provider_m p (.port(bif.provider));
  user_m u (.port(bif.consumer), .seen(seen));
  bit ok = 1;

  initial begin
    bif.consumer.put(8'd4);
    if (bif.data != 8'd5) begin
      $display("FAIL put %0d", bif.data);
      ok = 0;
    end
    if (bif.consumer.twice(8'd7) != 8'd14) begin
      $display("FAIL twice");
      ok = 0;
    end
    #2;
    if (bif.data != 8'd21 || seen != 8'd42) begin
      $display("FAIL port %0d %0d", bif.data, seen);
      ok = 0;
    end
    if (ok) $display("PASS");
  end
endmodule
