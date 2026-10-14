// fsim: top=top
// SPDX-License-Identifier: Apache-2.0

interface channel_if #(parameter type T = logic [7:0]);
  T payload;
endinterface

interface width_pair_if #(
    parameter type A = logic,
    parameter type B = logic
);
  A first;
  B second;
  logic [3:0] ordinary;
endinterface

interface typed_member_width_if #(parameter type T = logic);
  T payload;
  wire [$bits(payload)-1:0] mirror;
  assign mirror = payload;
endinterface

interface typed_member_array_decl_width_if #(parameter type T = logic);
  T payload [1:0];
  localparam int PAYLOAD_BITS = $bits(payload);
endinterface

interface hierarchical_write_if #(parameter type T = logic [7:0]);
  T data;
  logic [$bits(data)-1:0] other_data;
endinterface

module hierarchical_interface_write_case #(parameter int W = 3);
  typedef struct packed { logic [W-1:0] field; } write_type_t;
  hierarchical_write_if #(.T(write_type_t)) the_intf();
  logic [W-1:0] signal;

  always_comb begin
    the_intf.data.field = signal;
    the_intf.other_data = signal;
  end

  initial begin
    signal = '1;
    #1;
    if (the_intf.data.field !== {W{1'b1}}
        || the_intf.other_data !== {W{1'b1}}) begin
      $fatal(1, "interface hierarchical assignment mismatch for W=%0d", W);
    end
  end
endmodule

interface width_derived_leaf_if #(parameter type T = logic);
  T payload;
endinterface

interface lexical_shadow_if;
  logic [2:0] field;
endinterface

module lexical_shadow_child(output logic [2:0] value);
  typedef struct packed { logic [2:0] field; } lexical_shadow_t;
  lexical_shadow_t ifc;

  always_comb begin
    ifc.field = 3'b101;
    value = ifc.field;
  end
endmodule

interface width_derived_parent_if #(parameter type T = logic);
  width_derived_leaf_if #(.T(logic [$bits(T)*2-1:0])) child();
endinterface

module width_type_leaf #(parameter type T = logic) (
  input T value_in,
  output T value_out
);
  assign value_out = value_in;
endmodule

module width_type_forwarder #(parameter type T = logic) (
  width_derived_leaf_if bus,
  output T value_out
);
  width_type_leaf #(.T(T)) leaf (
    .value_in(bus.payload),
    .value_out(value_out)
  );
endmodule

interface signed_type_if #(parameter type T = logic signed [7:0]);
  T payload;
endinterface

module signed_type_leaf #(parameter type T = logic signed [7:0]) (
  input T value_in,
  output T value_out
);
  assign value_out = value_in >>> 1;
endmodule

module signed_type_forwarder #(parameter type T = logic signed [7:0]) (
  signed_type_if bus,
  output T value_out
);
  signed_type_leaf #(.T(T)) leaf (
    .value_in(bus.payload),
    .value_out(value_out)
  );
endmodule

module width_case #(parameter int W = 3) ();
  typedef logic [W-1:0] word_t;
  typedef struct packed {
    logic [W-1:0] data;
    bit valid;
  } packet_t;
  typedef packet_t [1:0] packet_array_t;

  channel_if #(.T(word_t)) typedef_bus();
  channel_if #(.T(packet_t)) struct_bus();
  channel_if #(.T(packet_array_t)) struct_array_bus();
  channel_if #(.T(logic [W+1:0])) range_bus();
  width_pair_if #(
    .A(logic [W-1:0]),
    .B(logic [W+1:0])
  ) pair_bus();
  typed_member_width_if #(.T(word_t)) typed_member_word_bus();
  typed_member_width_if #(.T(packet_t)) typed_member_struct_bus();
  typed_member_width_if #(.T(packet_array_t)) typed_member_array_bus();
  typed_member_array_decl_width_if #(.T(word_t))
    typed_member_array_decl_bus();
  width_derived_parent_if #(.T(logic [W-1:0])) derived_bus();
  logic [2*W-1:0] forwarded_payload;
  width_type_forwarder #(.T(logic [2*W-1:0])) derived_forwarder (
    .bus(derived_bus.child),
    .value_out(forwarded_payload)
  );

  initial begin
    typedef_bus.payload = '1;
    struct_bus.payload = '1;
    struct_array_bus.payload = '1;
    range_bus.payload = '1;
    pair_bus.first = '1;
    pair_bus.second = '1;
    pair_bus.ordinary = '1;
    typed_member_word_bus.payload = '1;
    typed_member_struct_bus.payload = '1;
    typed_member_array_bus.payload = '1;
    typed_member_array_decl_bus.payload[0] = '1;
    typed_member_array_decl_bus.payload[1] = '0;
    derived_bus.child.payload = '1;
    #1;
    if ($bits(typedef_bus.payload) != W
        || $bits(struct_bus.payload) != W + 1
        || $bits(struct_array_bus.payload) != 2 * (W + 1)
        || $bits(range_bus.payload) != W + 2
        || $bits(pair_bus.first) != W
        || $bits(pair_bus.second) != W + 2
        || $bits(pair_bus.ordinary) != 4
        || $bits(typed_member_word_bus.payload) != W
        || $bits(typed_member_word_bus.mirror) != W
        || $bits(typed_member_struct_bus.payload) != W + 1
        || $bits(typed_member_struct_bus.mirror) != W + 1
        || $bits(typed_member_array_bus.payload) != 2 * (W + 1)
        || $bits(typed_member_array_bus.mirror) != 2 * (W + 1)
        || typed_member_array_decl_bus.PAYLOAD_BITS != 2 * W
        || typed_member_array_decl_bus.payload[0] !== {W{1'b1}}
        || typed_member_array_decl_bus.payload[1] !== {W{1'b0}}
        || $bits(derived_bus.child.payload) != 2 * W
        || $bits(forwarded_payload) != 2 * W
        || pair_bus.first !== {W{1'b1}}
        || pair_bus.second !== {W+2{1'b1}}
        || pair_bus.ordinary !== 4'b1111
        || typed_member_word_bus.payload !== {W{1'b1}}
        || typed_member_word_bus.mirror !== {W{1'b1}}
        || typed_member_struct_bus.payload !== {W+1{1'b1}}
        || typed_member_struct_bus.mirror !== {W+1{1'b1}}
        || typed_member_array_bus.payload !== {2*(W+1){1'b1}}
        || typed_member_array_bus.mirror !== {2*(W+1){1'b1}}
        || derived_bus.child.payload !== {2*W{1'b1}}
        || forwarded_payload !== {2*W{1'b1}}
        || typedef_bus.payload !== {W{1'b1}}
        || struct_bus.payload !== {W+1{1'b1}}
        || struct_array_bus.payload !== {2*(W+1){1'b1}}
        || range_bus.payload !== {W+2{1'b1}}) begin
      $fatal(1, "packed type actual width mismatch for W=%0d", W);
    end
  end
endmodule

module top;
  width_case #(.W(3)) narrow();
  width_case #(.W(7)) wide();
  hierarchical_interface_write_case #(.W(3)) write_narrow();
  hierarchical_interface_write_case #(.W(7)) write_wide();
  lexical_shadow_if ifc();
  logic [2:0] shadow_value;
  lexical_shadow_child lexical_shadow_dut(.value(shadow_value));
  signed_type_if #(.T(logic signed [3:0])) signed_logic_bus();
  signed_type_if #(.T(bit signed [3:0])) signed_bit_bus();
  logic signed [3:0] signed_logic_out;
  bit signed [3:0] signed_bit_out;
  signed_type_forwarder #(.T(logic signed [3:0])) signed_logic_dut (
    .bus(signed_logic_bus),
    .value_out(signed_logic_out)
  );
  signed_type_forwarder #(.T(bit signed [3:0])) signed_bit_dut (
    .bus(signed_bit_bus),
    .value_out(signed_bit_out)
  );

  initial begin
    ifc.field = 3'b000;
    signed_logic_bus.payload = 4'bx000;
    signed_bit_bus.payload = 4'bx000;
    #1;
    if (shadow_value !== 3'b101 || ifc.field !== 3'b000) begin
      $fatal(1, "lexical signal did not shadow enclosing interface handle");
    end
    if (signed_logic_out !== 4'bxx00
        || signed_bit_out !== 4'b0000
        || $isunknown(signed_bit_out)) begin
      $fatal(1, "signed type actual forwarding mismatch");
    end
    #1;
    $display("PASS");
    $finish;
  end
endmodule
