// fsim: top=top
// SPDX-License-Identifier: Apache-2.0

interface leaf_if #(parameter int WIDTH = 1);
  logic [WIDTH-1:0] payload;
endinterface

interface wrapper_if;
  leaf_if #(3) nested();
endinterface

module leaf_consumer #(parameter int WIDTH = 3)(leaf_if bus);
  initial begin
    #1;
    bus.payload = {WIDTH{1'b1}};
  end
endmodule

module wrapper_consumer(wrapper_if bus);
  leaf_consumer #(.WIDTH(3)) nested_sink(.bus(bus.nested));
endmodule

module top;
  wrapper_if outer();
  wrapper_if \escaped.outer ();
  wrapper_consumer sink(.bus(outer));
  leaf_consumer #(.WIDTH(3)) escaped_sink(
      .bus(\escaped.outer .nested));

  initial begin
    #2;
    if ($bits(outer.nested.payload) != 3
        || outer.nested.payload !== 3'b111
        || $bits(\escaped.outer .nested.payload) != 3
        || \escaped.outer .nested.payload !== 3'b111) begin
      $fatal(1, "nested interface port lost its member path");
    end
    $display("PASS");
    $finish;
  end
endmodule
