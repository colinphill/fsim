// fsim: top=sv_associative_invalid_index std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.8.6, Table 7-1: a read of a nonexistent associative-array
// entry, or with an index holding x or z bits, returns the element type's
// nonexistent-entry value ('x for a 4-state integral element, '0 for a
// 2-state one, an enumeration's base type's, 0.0, ""), with a warning; a
// user `default:` is returned instead (7.9.11); a write with such an index
// is ignored. Module, automatic, class-property and string arrays.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
class sv_associative_invalid_index_holder;
  logic [7:0] values[bit [3:0]];
endclass

module sv_associative_invalid_index;
  typedef enum logic [1:0] { L0, L1 } logic_e;
  typedef enum bit [1:0] { B0, B1 } bit_e;
  typedef struct packed { logic [3:0] x; } packed_t;
  logic [7:0] four[int];
  bit [7:0] two[int];
  logic [7:0] keyed[logic [3:0]];
  logic_e logic_enum[int];
  bit_e bit_enum[int];
  packed_t packed_values[int];
  real reals[int];
  string texts[int];
  string by_text[string];
  int defaulted[int] = '{default: 7};
  logic [3:0] counted[int];
  logic [3:0] unknown;
  int failures;
  sv_associative_invalid_index_holder holder = new;

  initial begin
    automatic logic [7:0] local_values[int];
    keyed[1] = 5;
    unknown = 4'b1x0z;
    `CHECK("4-state missing", four[3] === 8'bx)
    `CHECK("2-state missing", two[3] === 8'd0)
    `CHECK("4-state enumeration missing", logic_enum[0] === 2'bx)
    `CHECK("2-state enumeration missing", bit_enum[0] === 2'd0)
    `CHECK("packed structure missing", packed_values[0] === 4'bx)
    `CHECK("real missing", reals[0] == 0.0)
    `CHECK("string missing", texts[0] == "")
    `CHECK("string-keyed missing", by_text["none"] == "")
    `CHECK("user default", defaulted[4] == 7)
    `CHECK("automatic missing", local_values[2] === 8'bx)
    `CHECK("class property missing", holder.values[2] === 8'bx)
    `CHECK("unknown index read", keyed[unknown] === 8'bx)
    keyed[unknown] = 9;
    `CHECK("unknown index write ignored", keyed.num() == 1 && keyed[1] == 5)
    counted[1]++;
    `CHECK("allocated by increment", counted.num() == 1 && counted[1] === 4'bx)
    `CHECK("reads allocate nothing", four.num() == 0 && texts.num() == 0)
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
