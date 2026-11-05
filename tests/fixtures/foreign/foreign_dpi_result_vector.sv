// SPDX-License-Identifier: Apache-2.0
// DPI function results are small values (IEEE 1800-2023 35.5.5): a packed
// bit vector is not one, whatever its width; the 35.5.4 example returning
// `bit [15:0]` is informative.
import "DPI-C" function bit [7:0] dpi_byte_vector(input int value);
import "DPI-C" function bit [0:0] dpi_one_bit_vector(input int value);

module top;
  bit [7:0] wide;
  bit [0:0] narrow;
  initial begin
    wide = dpi_byte_vector(1);
    narrow = dpi_one_bit_vector(2);
  end
endmodule
