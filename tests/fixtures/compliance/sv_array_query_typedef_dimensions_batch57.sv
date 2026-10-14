// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
localparam QUERY_A = 2;
localparam QUERY_B = 4;
typedef logic [QUERY_A-1:0] query_element_t;
typedef query_element_t query_port_t[QUERY_B];
module query_scope_leaf(input query_port_t value);
  localparam QUERY_A = 5;
  localparam QUERY_B = 10;
  initial begin
    if ($size(value, 1) != 4 || $size(value, 2) != 2)
      $fatal(1, "typedef query used the port consumer scope");
  end
endmodule
module top;
  query_port_t port_value;
  query_scope_leaf leaf(port_value);
  typedef logic [31:0] inner_t[3];
  typedef inner_t middle_t[5][2];
  middle_t nested[7];
  logic [5:3][0:1] packed_value;
  typedef logic [7:0] byte_t;
  typedef byte_t table_t[5:55];
  integer dimension;
  integer l, r, n;
  task automatic query(input integer d, output integer left_bound,
                       right_bound, size_value);
    left_bound = $left(table_t, d);
    right_bound = $right(table_t, d);
    size_value = $size(table_t, d);
  endtask
  initial begin
    if ($unpacked_dimensions(nested) != 4 || $dimensions(nested) != 5
        || $size(nested, 1) != 7 || $size(nested, 2) != 5
        || $size(nested, 3) != 2 || $size(nested, 4) != 3
        || $size(nested, 5) != 32)
      $fatal(1, "nested typedef dimensions not outermost first");
    if ($dimensions(packed_value) != 2
        || $left(packed_value, 1) != 5 || $right(packed_value, 1) != 3
        || $left(packed_value, 2) != 0 || $right(packed_value, 2) != 1)
      $fatal(1, "packed dimensions were flattened");
    for (dimension = 0; dimension <= 3; dimension = dimension + 1) begin
      query(dimension, l, r, n);
      if (dimension == 1) begin
        if (l != 5 || r != 55 || n != 51
            || $low(table_t, dimension) != 5
            || $high(table_t, dimension) != 55
            || $increment(table_t, dimension) != -1)
          $fatal(1, "dynamic unpacked dimension query");
      end else if (dimension == 2) begin
        if (l != 7 || r != 0 || n != 8
            || $increment(table_t, dimension) != 1)
          $fatal(1, "dynamic packed dimension query");
      end else if (l !== 32'bx || r !== 32'bx || n !== 32'bx)
        $fatal(1, "out-of-range dimension did not return unknown");
    end
    dimension = 'x;
    query(dimension, l, r, n);
    if (l !== 32'bx || r !== 32'bx || n !== 32'bx)
      $fatal(1, "unknown dimension did not return unknown");
    #1;
    $display("PASS");
    $finish;
  end
endmodule
