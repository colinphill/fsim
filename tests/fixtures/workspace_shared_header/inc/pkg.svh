// SPDX-License-Identifier: Apache-2.0
`ifndef SHARED_PKG_SVH
`define SHARED_PKG_SVH
package shared_pkg;
  localparam int VALUE = 5;
  function automatic int twice(int x); return 2 * x; endfunction
  class box;
    int v = 7;
    function int get(); return v; endfunction
  endclass
endpackage
`endif
