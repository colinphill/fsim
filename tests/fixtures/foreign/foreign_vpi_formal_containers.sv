// SPDX-License-Identifier: Apache-2.0
// The typespec of a task or function formal and result whose type is a class
// or a container (IEEE 1800-2023 37.41, 37.25 detail 2), and the ranges of
// one declared with a typedef (37.41).
class C;
  int v;
endclass

module top;
  typedef logic [3:0] nib_t;
  typedef int ints_t[$];

  function automatic nib_t f1(input nib_t a, input C c, input int q[$],
      input int da[], input int aa[string], input int fx[2:5],
      input ints_t t, input int plain);
    return a;
  endfunction
  function automatic C f2(input int x);
    return null;
  endfunction
  function automatic ints_t f3();
    ints_t r;
    return r;
  endfunction
endmodule
