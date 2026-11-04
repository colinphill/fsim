// SPDX-License-Identifier: Apache-2.0
// DPI argument types (IEEE 1800-2023 35.5.6, 35.5.6.1, H.7.4, H.7.6, H.12):
// ranges that are constant expressions, open packed `[]` formals, sized
// unpacked arrays in their normalized layout, arrays of real, shortreal,
// chandle and enumeration elements.
module top;
  localparam int W = 8;
  typedef enum int { RED, GREEN = 5, BLUE } color_t;

  import "DPI-C" function int dpi_t_range(
      input bit [W*2-1:0] v, input logic [$clog2(16)-1:0] l);
  import "DPI-C" function int dpi_t_open_packed(input bit [] v);
  import "DPI-C" function int dpi_t_open_mixed(input logic [] a [3:1]);
  import "DPI-C" function int dpi_t_sized(
      input int a [3:0], output bit [7:0] b [2], inout int m [2][3]);
  import "DPI-C" function void dpi_t_scalars(
      input real r [3], output shortreal f [2], input chandle h [2],
      output chandle o [2], input color_t c [3]);

  bit [W*2-1:0] wide = 16'hBEEF;
  logic [3:0] four = 4'b10x1;
  bit [11:0] twelve = 12'hABC;
  logic [4:0] lg [3:1];
  int sa [3:0];
  bit [7:0] sb [2];
  int sm [2][3];
  real ra [3];
  shortreal fa [2];
  chandle ha [2];
  chandle oa [2];
  color_t ca [3];
  int status;

  initial begin
    status = dpi_t_range(wide, four);
    $display("#c range ret=%0h", status);
    status = dpi_t_open_packed(twelve);
    $display("#c open_packed ret=%0h", status);
    lg[1] = 5'h01;
    lg[2] = 5'h12;
    lg[3] = 5'b1z0x1;
    status = dpi_t_open_mixed(lg);
    $display("#c open_mixed ret=%0d", status);
    sa[3] = 13;
    sa[2] = 12;
    sa[1] = 11;
    sa[0] = 10;
    for (int i = 0; i < 2; i++)
      for (int j = 0; j < 3; j++)
        sm[i][j] = i * 10 + j;
    status = dpi_t_sized(sa, sb, sm);
    $display("#c sized ret=%0d b0=%0h b1=%0h m00=%0d m12=%0d", status,
        sb[0], sb[1], sm[0][0], sm[1][2]);
    ra[0] = 1.5;
    ra[1] = 2.5;
    ra[2] = 3.5;
    ca[0] = RED;
    ca[1] = GREEN;
    ca[2] = BLUE;
    dpi_t_scalars(ra, fa, ha, oa, ca);
    $display("#c scalars f0=%0.2f f1=%0.2f o0=%0d o1=%0d", fa[0], fa[1],
        oa[0] != null, oa[1] != null);
  end
endmodule
