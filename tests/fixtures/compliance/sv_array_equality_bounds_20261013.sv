// fsim: top=sv_array_equality_bounds std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.22.2(d), 11.2.2: fixed-array bounds may differ.
module sv_array_equality_bounds;
  logic [3:0] left_values[2:0];
  logic [3:0] right_values[5:7];
  logic [3:0] matrix_left[1:0][0:2];
  logic [3:0] matrix_right[4:5][9:7];
  initial begin
    left_values[2] = 1;
    left_values[1] = 2;
    left_values[0] = 3;
    right_values[5] = 1;
    right_values[6] = 2;
    right_values[7] = 3;
    if (!(left_values == right_values) || left_values !== right_values)
      $fatal(1, "equal arrays with different bounds or directions differ");
    left_values[2] = 4'bx001;
    right_values[5] = 4'bx001;
    if ((left_values == right_values) !== 1'bx || !(left_values === right_values))
      $fatal(1, "four-state equality changed with array bounds");
    right_values[7] = 4;
    if ((left_values == right_values) !== 1'b0 || !(left_values !== right_values))
      $fatal(1, "known mismatch did not dominate unknown elements");
    for (int row = 0; row < 2; ++row) begin
      for (int column = 0; column < 3; ++column) begin
        matrix_left[1-row][column] = row*3+column;
        matrix_right[4+row][9-column] = row*3+column;
      end
    end
    if (!(matrix_left == matrix_right))
      $fatal(1, "multidimensional comparison did not follow declaration order");
    matrix_right[5][7] = 15;
    if (!(matrix_left != matrix_right))
      $fatal(1, "multidimensional mismatch lost");
    $display("PASS");
  end
endmodule
