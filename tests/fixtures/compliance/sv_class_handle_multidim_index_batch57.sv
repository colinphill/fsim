// fsim: top=b57_class_handle_multidim_index_batch57 std=2017
// SPDX-License-Identifier: Apache-2.0
class b57_multidim_leaf;
  int value;
  function new(input int initial_value);
    value = initial_value;
  endfunction
endclass

class b57_multidim_owner;
  b57_multidim_leaf items[2:3][5:3];
  int row_calls;
  int column_calls;
  int rhs_calls;
  logic [31:0] unknown_index;
  b57_multidim_leaf replacement;

  function int selected_row();
    row_calls = row_calls + 1;
    return 3;
  endfunction

  function int selected_column();
    column_calls = column_calls + 1;
    return 4;
  endfunction

  function int selected_invalid_row();
    row_calls = row_calls + 1;
    return 1;
  endfunction

  function b57_multidim_leaf selected_rhs();
    rhs_calls = rhs_calls + 1;
    return replacement;
  endfunction

  function new();
    int i;
    int j;
    replacement = new(909);
    for (i = 2; i <= 3; i = i + 1) begin
      for (j = 5; j >= 3; j = j - 1) begin
        items[i][j] = new((i * 100) + j);
      end
    end
  endfunction

  task check();
    b57_multidim_leaf selected;
    int visits;
    visits = 0;
    foreach (items[i, j]) begin
      selected = items[i][j];
      if (selected.value != (i * 100) + j)
        $fatal(1, "foreach multidimensional class handle read");
      visits = visits + 1;
    end
    if (visits != 6) $fatal(1, "foreach visit count");
    selected = items[2][5];
    if (selected.value != 205) $fatal(1, "row-major first cell");
    selected = items[2][4];
    if (selected.value != 204) $fatal(1, "row-major second cell");
    selected = items[2][3];
    if (selected.value != 203) $fatal(1, "row-major third cell");
    selected = items[3][5];
    if (selected.value != 305) $fatal(1, "row-major fourth cell");
    selected = items[3][4];
    if (selected.value != 304) $fatal(1, "row-major fifth cell");
    selected = items[3][3];
    if (selected.value != 303) $fatal(1, "row-major sixth cell");

    selected = items[selected_row()][selected_column()];
    if (selected.value != 304 || row_calls != 1 || column_calls != 1)
      $fatal(1, "read indices must be evaluated once");
    items[selected_row()][selected_column()] = new(777);
    if (row_calls != 2 || column_calls != 2)
      $fatal(1, "write indices must be evaluated once");
    selected = items[3][4];
    if (selected.value != 777) $fatal(1, "multidimensional write");

    selected = items[1][4];
    if (selected != null) $fatal(1, "out-of-range read returns null");
    selected = items[1][5];
    if (selected != null) $fatal(1, "invalid row must not alias a valid offset");
    selected = items[1][2];
    if (selected != null) $fatal(1, "invalid dimensions must not alias offset zero");
    selected = items[-1][4];
    if (selected != null) $fatal(1, "negative read index returns null");
    selected = items[2][6];
    if (selected != null) $fatal(1, "out-of-range column read returns null");

    row_calls = 0;
    column_calls = 0;
    rhs_calls = 0;
    items[selected_invalid_row()][selected_column()] = selected_rhs();
    if (row_calls != 1 || column_calls != 1 || rhs_calls != 1)
      $fatal(1, "invalid write evaluates each index and RHS once");

    items[1][5] = new(901);
    items[1][2] = new(904);
    items[-1][4] = new(905);
    items[2][6] = new(906);
    unknown_index = 32'hxxxxxxxx;
    selected = items[unknown_index][4];
    if (selected != null) $fatal(1, "X-index read returns null");
    unknown_index = 32'hzzzzzzzz;
    selected = items[2][unknown_index];
    if (selected != null) $fatal(1, "Z-index read returns null");

    items[1][4] = new(901);
    unknown_index = 32'hxxxxxxxx;
    items[unknown_index][4] = new(902);
    unknown_index = 32'hzzzzzzzz;
    items[2][unknown_index] = new(903);
    foreach (items[i, j]) begin
      selected = items[i][j];
      if (i == 3 && j == 4) begin
        if (selected.value != 777) $fatal(1, "valid write remains");
      end else if (selected.value != (i * 100) + j) begin
        $fatal(1, "invalid write changed an array element");
      end
    end
  endtask
endclass

module b57_class_handle_multidim_index_batch57;
  b57_multidim_owner owner;
  initial begin
    owner = new();
    owner.check();
    $display("PASS");
    $finish;
  end
endmodule
