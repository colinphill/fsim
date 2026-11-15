// SPDX-License-Identifier: Apache-2.0
// Static variable declaration initializers run once, before any initial or
// always procedure starts (IEEE 1800-2023 6.8, 6.21, 10.5): a block or
// callable static whose initializer is not a constant keeps its value from
// one entry to the next, and a module variable holds its initial value
// when the first procedure reads it. An initializer is no process of the
// source (37.63).
package p;
    function automatic int twice(int a); return 2 * a; endfunction
    function int counter();
        static int n = twice(5);
        n = n + 1;
        return n;
    endfunction
endpackage

module child;
    function int h(int a); return a * 3; endfunction
    int cm = h(7);
endmodule

module top;
    function int f(int a); return a + 40; endfunction
    task count;
        static int t = f(1);
        t = t + 1;
        $display("#c task %0d", t);
    endtask
    int g = 2;
    int m = f(g);
    string s = $sformatf("v%0d", g);
    logic clk = 0;
    child u ();
    initial begin
        $display("#c module m=%0d s=%s child=%0d", m, s, u.cm);
        for (int i = 0; i < 3; i++) begin : nb
            static real r = 1.5;
            static int k = f(g);
            r = r + 1;
            k = k + 1;
            $display("#c named %0d %f %0d", i, r, k);
        end
        for (int i = 0; i < 3; i++) begin
            static real ur = 1.5;
            static int uk = f(g);
            static string us = $sformatf("s%0d", g);
            ur = ur + 1;
            uk = uk + 1;
            us = {us, "x"};
            $display("#c unnamed %0d %f %0d %s", i, ur, uk, us);
        end
        repeat (2) count();
        $display("#c package %0d", p::counter());
        $display("#c package %0d", p::counter());
        #1 clk = 1;
        #1 clk = 0;
        #1 clk = 1;
        #20 $finish;
    end
    always @(posedge clk) begin : ab
        static int hits = f(0);
        hits = hits + 1;
        $display("#c always %0d", hits);
    end
    for (genvar gi = 0; gi < 2; gi++) begin : gen
        initial begin : gb
            static int base = f(gi);
            #(10 + gi) base = base + 1;
            $display("#c generate %0d %0d", gi, base);
        end
    end
endmodule
