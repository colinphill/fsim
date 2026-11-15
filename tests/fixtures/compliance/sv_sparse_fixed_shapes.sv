// fsim: top=sv_sparse_fixed_shapes std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.4, 8.9, 10.9.1: fixed unpacked arrays far beyond any dense
// storage budget hold every element; only written elements take storage.
// Multidimensional arrays, string elements, `default:` and keyed
// initializers, class statics and module variables alike. Whole-array
// operations see every element (7.12, 11.4.11, 7.6), and an automatic array
// of that size is stored the same way.
class sv_sparse_fixed_shapes_store;
  static bit [7:0] grid [0:8191][0:8191];
  static string names [0:(1 << 24)];
  static int filled [0:(1 << 25) - 1] = '{default: 7};
  static int keyed [0:(1 << 25) - 1] = '{5: 50, 4'd8: 80, default: -1};
  static string labels [0:(1 << 24)] = '{3: "three", default: "none"};
endclass

module sv_sparse_fixed_shapes;
  bit [7:0] plane [0:8191][0:8191];
  logic [3:0] cube [0:1023][0:1023][0:63];
  string words [0:(1 << 24)];
  int big [0:(1 << 25) - 1];
  int other [0:(1 << 25) - 1];
  logic [3:0] left_side [0:(1 << 24) - 1];
  logic [3:0] right_side [0:(1 << 24) - 1];
  logic [3:0] chosen [0:(1 << 24) - 1];
  int found [$];
  int failures;
  logic sel_unknown;

  task automatic check(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d, want %0d", what, got, want);
      failures++;
    end
  endtask

  task automatic check_text(input string what, input string got,
                            input string want);
    if (got != want) begin
      $display("FAIL %s: got \"%s\", want \"%s\"", what, got, want);
      failures++;
    end
  endtask

  function automatic int automatic_sum(input int seed);
    int local_big [0:(1 << 26) - 1];
    local_big[seed] = seed * 3;
    return local_big.sum() + local_big[seed];
  endfunction

  initial begin
    sv_sparse_fixed_shapes_store::grid[3][4] = 8'h33;
    sv_sparse_fixed_shapes_store::grid[8191][8191] = 8'h99;
    check("class 2-D written", sv_sparse_fixed_shapes_store::grid[3][4], 'h33);
    check("class 2-D transposed", sv_sparse_fixed_shapes_store::grid[4][3], 0);
    check("class 2-D last", sv_sparse_fixed_shapes_store::grid[8191][8191], 'h99);

    sv_sparse_fixed_shapes_store::names[9] = "nine";
    check_text("class strings written", sv_sparse_fixed_shapes_store::names[9], "nine");
    check_text("class strings unwritten", sv_sparse_fixed_shapes_store::names[10], "");

    check("default initializer", sv_sparse_fixed_shapes_store::filled[3], 7);
    sv_sparse_fixed_shapes_store::filled[2] = 1;
    check("default initializer written", sv_sparse_fixed_shapes_store::filled[2], 1);
    sv_sparse_fixed_shapes_store::filled[2] = 7;
    check("default initializer rewritten", sv_sparse_fixed_shapes_store::filled[2], 7);

    check("keyed initializer key", sv_sparse_fixed_shapes_store::keyed[5], 50);
    check("keyed initializer unsigned key", sv_sparse_fixed_shapes_store::keyed[8], 80);
    check("keyed initializer default", sv_sparse_fixed_shapes_store::keyed[6], -1);
    check_text("keyed string key", sv_sparse_fixed_shapes_store::labels[3], "three");
    check_text("keyed string default", sv_sparse_fixed_shapes_store::labels[4], "none");

    plane[3][4] = 8'h44;
    check("module 2-D written", plane[3][4], 'h44);
    check("module 2-D unwritten", plane[4][3], 0);
    cube[1000][2][63] = 4'h5;
    check("module 3-D written", cube[1000][2][63], 5);
    if (cube[1000][2][62] !== 4'bxxxx) begin
      $display("FAIL module 3-D unwritten: %b", cube[1000][2][62]);
      failures++;
    end
    words[(1 << 24)] = "last";
    check_text("module strings written", words[(1 << 24)], "last");
    check_text("module strings unwritten", words[0], "");

    big[3] = 5;
    big[10] = -2;
    big[(1 << 25) - 1] = 9;
    check("size", $size(big), 1 << 25);
    check("sum", big.sum(), 12);
    check("sum with", big.sum() with (item + 1), 12 + (1 << 25));
    check("product", big.product(), 0);
    check("or", big.or(), 5 | -2 | 9);
    check("xor", big.xor(), 5 ^ -2 ^ 9);
    found = big.find_index with (item != 0);
    check("find_index count", found.size(), 3);
    check("find_index last", found[2], (1 << 25) - 1);
    found = big.find_first_index with (item == 0);
    check("find_first_index", found[0], 0);
    found = big.min();
    check("min", found[0], -2);
    found = big.max();
    check("max", found[0], 9);
    found = big.unique();
    check("unique", found.size(), 4);
    other = big;
    check("equal", other == big, 1);
    other[7] = 1;
    check("not equal", other != big, 1);
    big.reverse();
    check("reverse", big[(1 << 25) - 1 - 3], 5);
    big.sort();
    check("sort first", big[0], -2);
    check("sort last", big[(1 << 25) - 1], 9);
    big.rsort();
    check("rsort first", big[0], 9);
    check("rsort last", big[(1 << 25) - 1], -2);

    left_side[1] = 4'h3;
    left_side[2] = 4'h5;
    right_side[1] = 4'h3;
    right_side[2] = 4'h6;
    chosen = sel_unknown ? left_side : right_side;
    if (chosen[1] !== 4'h3 || chosen[2] !== 4'bxxxx
        || chosen[3] !== 4'bxxxx) begin
      $display("FAIL ambiguous select: %b %b %b", chosen[1], chosen[2],
        chosen[3]);
      failures++;
    end

    check("automatic array", automatic_sum(5), 30);
    if (failures == 0) $display("PASS");
  end
endmodule
