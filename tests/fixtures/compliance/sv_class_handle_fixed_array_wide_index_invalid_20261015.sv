// fsim: top=sv_class_handle_fixed_array_wide_index_invalid_20261015 std=2017
class WideIndexLeaf;
  int marker;
  function new(input int initial_marker);
    marker = initial_marker;
  endfunction
endclass

class WideIndexOwner;
  WideIndexLeaf entries[4];
  logic [64:0] source_index;
  int receiver_calls;
  int index_calls;
  int rhs_calls;

  function WideIndexOwner receiver_step();
    receiver_calls = receiver_calls + 1;
    return this;
  endfunction

  function logic [64:0] index_step();
    index_calls = index_calls + 1;
    return source_index;
  endfunction

  function WideIndexLeaf value_step();
    rhs_calls = rhs_calls + 1;
    return new(99);
  endfunction

  function bit write_invalid(input logic [64:0] candidate_index);
    WideIndexLeaf selected;
    source_index = candidate_index;
    receiver_calls = 0;
    index_calls = 0;
    rhs_calls = 0;
    receiver_step().entries[index_step()] = value_step();
    if (receiver_calls !== 1 || index_calls !== 1 || rhs_calls !== 1)
      return 0;
    selected = entries[0];
    if (selected == null || selected.marker !== 10) return 0;
    selected = entries[1];
    if (selected == null || selected.marker !== 20) return 0;
    selected = entries[2];
    if (selected == null || selected.marker !== 30) return 0;
    selected = entries[3];
    if (selected == null || selected.marker !== 40) return 0;
    selected = entries[{1'bx, 64'b0}];
    if (selected != null) return 0;
    selected = entries[{1'b1, 64'b0}];
    return selected == null;
  endfunction

  function bit check_invalid_indices();
    entries[0] = new(10);
    entries[1] = new(20);
    entries[2] = new(30);
    entries[3] = new(40);
    return write_invalid({1'bx, 64'b0})
        && write_invalid({1'b1, 64'b0});
  endfunction
endclass

module sv_class_handle_fixed_array_wide_index_invalid_20261015;
  WideIndexOwner dut;
  initial begin
    dut = new;
    if (!dut.check_invalid_indices()) $fatal(1, "wide invalid index write");
    $display("CLASS_FIXED_ARRAY_WIDE_INVALID_INDEX_PASS");
    $finish;
  end
endmodule
