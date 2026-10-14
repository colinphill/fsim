// fsim: top=top
// SPDX-License-Identifier: Apache-2.0

interface owner_context_if #(parameter type T = logic);
  T payload;
endinterface

module owner_context_parent #(parameter int W = 1);
  typedef struct packed {
    logic [W-1:0] field;
    bit valid;
  } payload_t;

  owner_context_if #(.T(payload_t)) bus();
  logic [W-1:0] source;

  always_comb begin
    bus.payload.field = source;
    bus.payload.valid = 1'b1;
  end

  initial begin
    source = {1'b1, {(W-1){1'b0}}};
    #1;
    if ($bits(bus.payload) != W + 1
        || bus.payload.field !== {1'b1, {(W-1){1'b0}}}
        || bus.payload.valid !== 1'b1) begin
      $fatal(1, "type owner specialization mismatch for W=%0d", W);
    end
  end

  if (W > 0) begin : g_owner_context
    owner_context_if #(.T(payload_t)) generated_bus();
    logic [W-1:0] generated_source;

    always_comb begin
      generated_bus.payload.field = generated_source;
      generated_bus.payload.valid = 1'b1;
    end

    initial begin
      generated_source = {1'b1, {(W-1){1'b0}}};
      #1;
      if ($bits(generated_bus.payload) != W + 1
          || generated_bus.payload.field !== {1'b1, {(W-1){1'b0}}}
          || generated_bus.payload.valid !== 1'b1) begin
        $fatal(1, "generated type owner specialization mismatch for W=%0d", W);
      end
    end
  end
endmodule

module top;
  owner_context_parent #(.W(3)) narrow();
  owner_context_parent #(.W(7)) wide();

  initial begin
    #2;
    $display("PASS");
    $finish;
  end
endmodule
