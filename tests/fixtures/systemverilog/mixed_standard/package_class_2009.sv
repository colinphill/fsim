// SPDX-License-Identifier: Apache-2.0
// Compiled with --standard 2009: a package holding a base class.
package p09;
  class base;
    int v;
    function new(int a);
      v = a;
    endfunction
    virtual function int get();
      return v;
    endfunction
  endclass
endpackage
