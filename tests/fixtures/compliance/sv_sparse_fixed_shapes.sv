// fsim: top=sv_sparse_fixed_shapes std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.4, 8.9, 10.9.1: fixed unpacked arrays far beyond any dense
// storage budget hold every element; only written elements take storage.
// Multidimensional arrays, string elements, `default:` and keyed
// initializers, class statics and module variables alike.
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
  int failures;

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
    if (failures == 0) $display("PASS");
  end
endmodule
