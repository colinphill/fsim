// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2017 and imported by a 2023 module.
package p17;
  typedef logic [7:0] byte_t;
  localparam int K = 5;
  function automatic byte_t inc(byte_t v);
    return v + 8'd1;
  endfunction
endpackage
