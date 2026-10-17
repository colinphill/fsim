// fsim: top=sv_class_handle_fixed_array_range_index_20261015 std=2017
// SPDX-License-Identifier: Apache-2.0
class RangeIndexLeaf;
  int marker;
  function new(input int initial_marker);
    marker = initial_marker;
  endfunction
endclass

class RangeIndexOwner;
  RangeIndexLeaf ascending[4:7];
  RangeIndexLeaf descending[7:4];
  int receiver_calls;
  int ascending_index_calls;
  int descending_index_calls;
  int rhs_calls;

  function RangeIndexOwner receiver_step();
    receiver_calls = receiver_calls + 1;
    return this;
  endfunction

  function int ascending_index_step();
    ascending_index_calls = ascending_index_calls + 1;
    return 6;
  endfunction

  function int descending_index_step();
    descending_index_calls = descending_index_calls + 1;
    return 5;
  endfunction

  function RangeIndexLeaf value_step();
    rhs_calls = rhs_calls + 1;
    return new(99);
  endfunction

  function new();
    ascending[4] = new(40);
    ascending[5] = new(50);
    ascending[6] = new(60);
    ascending[7] = new(70);
    descending[7] = new(70);
    descending[6] = new(60);
    descending[5] = new(50);
    descending[4] = new(40);
  endfunction

  function bit check_ranges();
    RangeIndexLeaf selected;
    receiver_calls = 0;
    ascending_index_calls = 0;
    descending_index_calls = 0;
    rhs_calls = 0;
    receiver_step().ascending[ascending_index_step()] = value_step();
    if (receiver_calls !== 1 || ascending_index_calls !== 1
        || descending_index_calls !== 0 || rhs_calls !== 1)
      return 0;
    selected = ascending[6];
    if (selected == null || selected.marker !== 99) return 0;
    selected = ascending[4];
    if (selected == null || selected.marker !== 40) return 0;
    selected = ascending[5];
    if (selected == null || selected.marker !== 50) return 0;
    selected = ascending[7];
    if (selected == null || selected.marker !== 70) return 0;

    receiver_calls = 0;
    ascending_index_calls = 0;
    descending_index_calls = 0;
    rhs_calls = 0;
    receiver_step().descending[descending_index_step()] = value_step();
    if (receiver_calls !== 1 || ascending_index_calls !== 0
        || descending_index_calls !== 1 || rhs_calls !== 1)
      return 0;
    selected = descending[5];
    if (selected == null || selected.marker !== 99) return 0;
    selected = descending[7];
    if (selected == null || selected.marker !== 70) return 0;
    selected = descending[6];
    if (selected == null || selected.marker !== 60) return 0;
    selected = descending[4];
    return selected != null && selected.marker === 40;
  endfunction
endclass

module sv_class_handle_fixed_array_range_index_20261015;
  RangeIndexOwner dut;
  initial begin
    dut = new;
    if (!dut.check_ranges()) $fatal(1, "fixed array range index mapping");
    $display("CLASS_FIXED_ARRAY_RANGE_INDEX_PASS");
    $finish;
  end
endmodule
