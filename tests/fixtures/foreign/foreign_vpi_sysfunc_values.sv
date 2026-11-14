// SPDX-License-Identifier: Apache-2.0
// The value of a system function call with state is what the call returned
// at its last evaluation (IEEE 1800-2023 38.15, 37.58): $random, $urandom,
// $urandom_range and the seeded $random(seed). The task runs twice; the
// plug-in reads each call before the first evaluation and at the end of the
// run, where the value of the call must be the value its assignment stored.
package pk;
    function automatic int draw();
        int r;
        r = $random;
        return r;
    endfunction
endpackage

module top;
    int a, d, e, seed, seed2, p, q, w;
    bit [31:0] b, c;

    task automatic draws();
        a = $random;
        b = $urandom;
        c = $urandom_range(9, 5);
        d = $random(seed);
        e = $dist_uniform(seed2, -1000000, -1);
    endtask

    initial begin
        seed = 7;
        seed2 = 3;
        draws();
        #1 draws();
    end

    // A package function called from three processes: the later bodies are
    // replayed from the callable-body memo and keep the call's value slot.
    initial p = pk::draw();
    initial #1 q = pk::draw();
    initial #2 w = pk::draw();
endmodule
