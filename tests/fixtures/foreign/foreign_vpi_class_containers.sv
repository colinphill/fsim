// SPDX-License-Identifier: Apache-2.0
// Container properties of a live class object: the array var, its elements
// by iteration and by index with the types they were declared with, class
// handle, structure and nested container elements, values written to an
// element and through the array routines, and size changes (IEEE 1800-2023
// 37.17, 37.20, 37.33, 38.15, 38.34, 38.36.1). A fixed array of several
// unpacked dimensions is an array of arrays, one level per dimension, and a
// fixed array is indexed by its declared range (7.4.5, 38.16).
module top;
    typedef enum { RED, GREEN } color_t;
    typedef struct { int a; byte b; } pair_t;

    class Box;
        int q[$];
        int dyn[];
        int aa[int];
        bit signed [31:0] bs[2];
        byte by[2];
        shortint sh[2];
        color_t col[2];
        Box peers[2];
        pair_t ps[2];
        int nest[2][$];
        real rl[2];
        int grid[2][3];
        int down[4:1];
    endclass

    Box b;
    initial begin
        b = new;
        #1 b.q = {10, 20};
        b.dyn = new[3];
        b.aa[5] = 50;
        b.bs = '{5, -6};
        b.by = '{1, 2};
        b.sh = '{300, 400};
        b.col = '{GREEN, RED};
        b.peers[0] = b;
        b.ps[0] = '{a: 7, b: 3};
        b.nest[1] = {9, 8};
        b.rl = '{1.5, 2.5};
        foreach (b.grid[i, j]) b.grid[i][j] = i * 10 + j;
        b.down = '{1, 2, 3, 4};
        #1 b.q.push_back(30);
    end
endmodule
