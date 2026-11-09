// SPDX-License-Identifier: Apache-2.0
// The typespec chain of a variable whose packed dimensions are declared over
// a typedef keeps the typedef's name at the element level (IEEE 1800-2023
// 7.4.1, 37.25 details 1, 8): the dimensions composed over nib_t must not
// flatten it away.
module top;
    typedef logic [3:0] nib_t;
    typedef nib_t [1:0] two_t;
    typedef nib_t arr_t [3:0];
    two_t v;
    nib_t [1:0] w;
    arr_t a;
endmodule
