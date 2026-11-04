// SPDX-License-Identifier: Apache-2.0
// DPI unpacked aggregates (IEEE 1800-2023 35.5.3, 35.5.6, 35.5.6.1, H.7.8):
// an unpacked structure passed by pointer, sized and open arrays of it,
// output and inout open arrays whose actual is a dynamic array or queue,
// and exports with sized unpacked and structure formals.
module top;
  typedef struct {
    int a;
    byte b;
    real r;
    bit [9:0] v;
    int arr [2];
  } st_t;

  import "DPI-C" function int dpi_a_struct(input st_t s, output st_t o);
  import "DPI-C" function int dpi_a_struct_sized(input st_t s [2]);
  import "DPI-C" function int dpi_a_struct_open(input st_t s []);
  import "DPI-C" function void dpi_a_dynamic(
      inout int d [], output bit [7:0] q [], inout int rows [][2]);
  import "DPI-C" context function void dpi_a_run();
  export "DPI-C" function dpi_a_export_sized;
  export "DPI-C" function dpi_a_export_struct;

  function void dpi_a_export_sized(input int v [3], output int w [2]);
    $display("#c export_sized v=%0d,%0d,%0d", v[0], v[1], v[2]);
    w[0] = v[2];
    w[1] = v[0];
  endfunction

  function int dpi_a_export_struct(input st_t s);
    $display("#c export_struct a=%0d b=%0d r=%0.1f v=%0h arr=%0d,%0d", s.a,
        s.b, s.r, s.v, s.arr[0], s.arr[1]);
    return s.a + s.arr[1];
  endfunction

  st_t s, o;
  st_t pair [2];
  st_t open [4:5];
  int d [];
  bit [7:0] q [$];
  int rows [][2];
  int status;

  initial begin
    s = '{a: 7, b: 3, r: 2.5, v: 10'h155, arr: '{4, 5}};
    status = dpi_a_struct(s, o);
    $display("#c struct ret=%0d a=%0d b=%0d r=%0.1f v=%0h arr=%0d,%0d",
        status, o.a, o.b, o.r, o.v, o.arr[0], o.arr[1]);
    pair[0] = s;
    pair[1] = o;
    status = dpi_a_struct_sized(pair);
    $display("#c struct_sized ret=%0d", status);
    open[4] = s;
    open[5] = o;
    status = dpi_a_struct_open(open);
    $display("#c struct_open ret=%0d", status);
    d = new[4];
    d = '{1, 2, 3, 4};
    q = {5, 6, 7};
    rows = new[2];
    rows[0] = '{1, 2};
    rows[1] = '{3, 4};
    dpi_a_dynamic(d, q, rows);
    $display("#c dynamic d=%0d,%0d,%0d,%0d q=%0d,%0d,%0d", d[0], d[1], d[2],
        d[3], q[0], q[1], q[2]);
    $display("#c dynamic rows=%0d,%0d,%0d,%0d", rows[0][0], rows[0][1],
        rows[1][0], rows[1][1]);
    $display("#c dynamic sizes d=%0d q=%0d rows=%0d", d.size(), q.size(),
        rows.size());
    dpi_a_run();
  end
endmodule
