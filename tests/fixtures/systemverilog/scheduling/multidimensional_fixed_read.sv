// SPDX-License-Identifier: Apache-2.0
module multidimensional_fixed_read;
  logic [3:0] ascending [0:1][0:2];
  logic [3:0] descending [1:0][2:1];
  logic [3:0] rank_three [1:0][0:1][2:1];
  bit [3:0] two_state [0:1][0:1];
  logic [63:0] wide64_index;
  logic [64:0] wide_index;
  integer signed_index;
  integer index_effects;
  logic [1:0] dynamic_row;
  logic [1:0] dynamic_column;
  integer descending_row;
  integer descending_column;
  integer rank_three_first;
  integer rank_three_second;
  integer rank_three_third;
  wire [3:0] continuous_selected;
  logic [3:0] always_comb_selected;
  integer always_comb_runs = 0;
  integer runs_before;
  logic [3:0] result;

  assign continuous_selected = ascending[dynamic_row][dynamic_column];

  always_comb begin
    always_comb_selected = ascending[dynamic_row][dynamic_column];
    always_comb_runs = always_comb_runs + 1;
  end

  function automatic integer next_index;
    begin
      index_effects = index_effects + 1;
      next_index = 1;
    end
  endfunction

  initial begin
    $display("INITIAL rank2=%b rank3=%b", ascending[0][0],
      rank_three[1][0][2]);

    ascending[0][0] = 4'h1;
    ascending[0][1] = 4'h2;
    ascending[1][1] = 4'h3;
    ascending[1][2] = 4'h6;
    descending[1][2] = 4'ha;
    descending[0][1] = 4'hb;
    rank_three[1][0][2] = 4'h1;
    rank_three[0][1][1] = 4'h5;
    signed_index = -1;
    wide64_index = 64'h1_0000_0001;
    wide_index = 65'h1_0000_0001;
    dynamic_column = 1;
    descending_row = 1;
    descending_column = 2;
    rank_three_first = 0;
    rank_three_second = 1;
    rank_three_third = 1;

    $display("VALID ascending=%b descending=%b rank3=%b",
      ascending[0][1], descending[1][2], rank_three[0][1][1]);
    $display("SIBLING before=%b", ascending[0][1]);
    ascending[1][0] = 4'hf;
    $display("SIBLING after=%b", ascending[0][1]);
    $display("UNKNOWN first=%b second=%b",
      ascending[2'bx0][1], ascending[0][2'bz1]);
    $display("WIDE index64=%b index65=%b",
      ascending[wide64_index][0], ascending[wide_index][0]);
    $display("SIGNED negative=%b", ascending[signed_index][0]);
    $display("TWO_STATE invalid=%b", two_state[2][0]);

    index_effects = 0;
    result = ascending[next_index()][next_index()];
    $display("SIDE_EFFECT dynamic_first count=%0d result=%b",
      index_effects, result);

    index_effects = 0;
    result = ascending[99][next_index()];
    $display("SIDE_EFFECT count=%0d result=%b", index_effects, result);

    #1;
    $display("DYNAMIC initial_x comb=%b continuous=%b",
      always_comb_selected, continuous_selected);
    dynamic_row = 0;
    #1;
    $display("DYNAMIC valid ascending=%b descending=%b rank3=%b comb=%b continuous=%b",
      ascending[dynamic_row][dynamic_column],
      descending[descending_row][descending_column],
      rank_three[rank_three_first][rank_three_second][rank_three_third],
      always_comb_selected, continuous_selected);

    runs_before = always_comb_runs;
    ascending[1][2] = 4'h7;
    #1;
    $display("DYNAMIC unrelated comb=%b continuous=%b reran=%0d",
      always_comb_selected, continuous_selected,
      always_comb_runs > runs_before ? 1 : 0);

    runs_before = always_comb_runs;
    ascending[0][1] = 4'hc;
    #1;
    $display("DYNAMIC selected comb=%b continuous=%b reran=%0d",
      always_comb_selected, continuous_selected,
      always_comb_runs > runs_before ? 1 : 0);
    $finish;
  end
endmodule
