// SPDX-License-Identifier: Apache-2.0
module fixed_array_write;
  logic [7:0] matrix [1:0][-1:0];
  logic [7:0] ascending [0:1][0:1];
  logic [7:0] line [-1:1];
  logic [128:0] wide_values [0:1][0:1];
  bit [7:0] two_state [0:1][0:1];
  logic [64:0] wide_index;
  logic [128:0] very_wide_index;
  logic [1:0] unknown_index;
  integer row;
  integer column;
  integer first_calls;
  integer second_calls;
  integer rhs_calls;
  integer selected_part;
  integer events = 0;

  always @(matrix[1][-1]) events = events + 1;

  function automatic integer first_index;
    begin
      first_calls = first_calls + 1;
      first_index = 1;
    end
  endfunction

  function automatic integer second_index;
    begin
      second_calls = second_calls + 1;
      second_index = -1;
    end
  endfunction

  function automatic [7:0] rhs;
    begin
      rhs_calls = rhs_calls + 1;
      rhs = 8'hc3;
    end
  endfunction

  function automatic [7:0] local_result;
    logic [3:0] local_values [0:1][0:1];
    integer local_row;
    begin
      local_values[0][0] = 4'h2;
      local_values[1][1] = 4'h9;
      local_row = 99;
      local_values[local_row][0] = 4'hf;
      local_values[2'bx1][1] = 4'hf;
      local_result = {local_values[0][0], local_values[1][1]};
    end
  endfunction

  initial begin
    matrix[1][-1] = 8'h12;
    matrix[0][0] = 8'h34;
    ascending[0][0] = 8'h56;
    ascending[1][1] = 8'h78;
    line[-1] = 8'hab;
    line[0] = 8'hcd;
    line[1] = 8'hef;
    wide_values[0][0] = 129'h1_00000000_00000000_00000000_00000001;
    two_state[0][0] = 8'h5a;
    row = 1;
    column = -1;
    selected_part = 0;
    wide_index = 65'h1_0000_0001;
    very_wide_index = 129'h1_00000000_00000000_00000000_00000001;
    unknown_index = 2'bx1;
    #1;
    events = 0;

    matrix[row][column] = 8'h21;
    ascending[0][row] = 8'h65;
    line[column] = 8'hba;
    #1;
    $display("VALID descending=%h ascending=%h negative=%h events=%0d",
      matrix[1][-1], ascending[0][1], line[-1], events);
    events = 0;

    matrix[unknown_index][-1] = 8'hff;
    matrix[1][2'bz0] = 8'hff;
    matrix[wide_index][-1] = 8'hff;
    matrix[very_wide_index][-1] = 8'hff;
    matrix[99][-1] = 8'hff;
    matrix[-99][-1] = 8'hff;
    line[unknown_index] = 8'hff;
    line[wide_index] = 8'hff;
    line[very_wide_index] = 8'hff;
    wide_values[wide_index][0] = '0;
    two_state[unknown_index][0] = '1;
    matrix[unknown_index][-1][selected_part +: 4] = 4'hf;
    #1;
    $display("INVALID descending=%h sibling=%h line=%h/%h/%h wide=%h bit=%h events=%0d",
      matrix[1][-1], matrix[0][0], line[-1], line[0], line[1],
      wide_values[0][0], two_state[0][0], events);

    first_calls = 0;
    second_calls = 0;
    rhs_calls = 0;
    matrix[first_index()][second_index()] = rhs();
    $display("EFFECT valid first=%0d second=%0d rhs=%0d value=%h",
      first_calls, second_calls, rhs_calls, matrix[1][-1]);
    first_calls = 0;
    second_calls = 0;
    rhs_calls = 0;
    matrix[99][second_index()] = rhs();
    $display("EFFECT invalid first=%0d second=%0d rhs=%0d value=%h",
      first_calls, second_calls, rhs_calls, matrix[1][-1]);
    $display("LOCAL value=%h", local_result());
    $display("READ negative=%h unknown=%h wide=%h",
      line[column], line[unknown_index], line[wide_index]);

    row = 1;
    column = -1;
    matrix[row][column] <= 8'hde;
    matrix[unknown_index][column] <= 8'hff;
    row = 0;
    column = 0;
    unknown_index = 0;
    $display("NBA before=%h sibling=%h", matrix[1][-1], matrix[0][0]);
    #1;
    $display("NBA after=%h sibling=%h", matrix[1][-1], matrix[0][0]);
    matrix[1][-1][selected_part +: 4] = 4'h5;
    $display("PART value=%h", matrix[1][-1]);
    row = 1;
    column = -1;
    $display("CACHE before=%h", matrix[1][-1]);
    matrix[row][column] = 8'h96;
    $display("CACHE after=%h sibling=%h", matrix[1][-1], matrix[0][0]);
    $finish;
  end
endmodule
