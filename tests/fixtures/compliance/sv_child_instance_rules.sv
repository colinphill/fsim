// fsim: top=sv_child_instance_rules std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 23.10, 25.10, 27.5: an instance below the root is checked
// with its own parameters, interface ports and generate blocks. Legal there:
// a size cast by a class parameter (6.24.1); a part-select of an unpacked
// array member by its own ascending range (7.4.5, 7.4.6); a packed type
// built from an interface instance's parameters (25.10), whose layout at
// the interface's default parameters would be empty; an implicit enum value
// following a sized literal that is negative as an int (6.19); a parameter
// of an interface port (25.10); and a part-select in a generate arm, without
// a block of its own, that the instance leaves out (27.5).
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
interface sizes_if #(parameter int N = 0) ();
  typedef logic [$clog2(N)-1:0] index_t;
  typedef union packed {
    logic [$bits(index_t)+2-1:0] raw;
    struct packed {
      index_t index;
      logic [1:0] tag;
    } fields;
  } address_t;
endinterface

interface width_if #(parameter int W = 4) ();
  logic [W-1:0] data;
endinterface

class shifter #(parameter int W = 8);
  bit [W-1:0] value;
  task run;
    value = W'(4);
  endtask
endclass

module child #(parameter bit ASCENDING = 1) (width_if port);
  typedef struct { logic [31:0] items[0:3]; } holder_t;
  typedef enum { LOW = 32'hdeadbeef, HIGH } style_t;
  localparam int PORT_WIDTH = port.W;
  sizes_if #(16) sizes();
  typedef sizes.address_t address_t;
  holder_t holder;
  address_t address;
  shifter #(12) shift = new;
  logic [7:0] bits;
  logic [3:0] part;
  int failures;

  if (ASCENDING)
    initial part = 4'd5;
  else
    initial part = bits[0:3];

  initial begin
    #1;
    holder.items[1:3] = '{32'd4, 32'd3, 32'd2};
    `CHECK("ascending unpacked part-select", holder.items[1] == 4
      && holder.items[3] == 2)
    shift.run();
    `CHECK("class parameter size cast", shift.value == 4
      && $bits(shift.value) == 12)
    address.fields.index = 4'd9;
    `CHECK("interface-parameter layout", $bits(address_t) == 6
      && address.raw[5:2] == 9)
    `CHECK("implicit enum value", HIGH == 32'hdeadbef0)
    `CHECK("interface port parameter", PORT_WIDTH == 6)
    `CHECK("generate arm left out", part == 5)
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule

module sv_child_instance_rules;
  width_if #(6) bus();
  child u(bus);
endmodule
