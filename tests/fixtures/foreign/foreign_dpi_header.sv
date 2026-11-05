// SPDX-License-Identifier: Apache-2.0
// The C declarations `fsim compile --dpi-header` writes (IEEE 1800-2023
// H.7.8, H.10.1): an unpacked structure a formal uses is a C structure of
// its members, named after its typedef and the unit that declares it.
// Typedefs of equal layout keep their own names.
package dpi_shapes;
  typedef struct {
    int count;
    byte tag;
    real scale;
  } sample_t;
  typedef struct {
    int count;
    byte tag;
    real scale;
  } reading_t;
endpackage

import dpi_shapes::*;
import "DPI-C" function int dpi_take_sample(input sample_t value,
                                            output sample_t copy);
import "DPI-C" function void dpi_count(input int value);
import "DPI-C" function void dpi_take_reading(input reading_t value);

module top;
  sample_t a, b;
  reading_t r;
  int result;
  initial begin
    result = dpi_take_sample(a, b);
    dpi_count(result);
    dpi_take_reading(r);
  end
endmodule
