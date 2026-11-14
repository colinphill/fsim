// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to an element of a string array captures the index
// and the value when the statement executes and writes that element in the
// NBA region (IEEE 1800-2023 7.4, 7.5, 7.8, 7.10, 10.4.2): fixed,
// multidimensional, dynamic, queue and associative string arrays. Writes to
// different elements of one array in one time step all land.
module top;
  string fx[3];
  string md[2][2];
  string dy[];
  string q[$];
  string ai[int];
  string as[string];

  function void note(string label);
    $display("sns %s fx=%s,%s,%s md=%s,%s,%s,%s dy=%s,%s q=%s,%s,%s ai=%0d,%s,%s as=%0d,%s,%s",
        label, fx[0], fx[1], fx[2], md[0][0], md[0][1], md[1][0], md[1][1],
        dy[0], dy[1], q[0], q[1], q.size() > 2 ? q[2] : "-", ai.size(),
        ai.exists(7) ? ai[7] : "-", ai.exists(9) ? ai[9] : "-", as.size(),
        as.exists("k") ? as["k"] : "-", as.exists("j") ? as["j"] : "-");
  endfunction

  initial begin
    string s;
    int i;
    dy = new[2];
    q = '{"q0", "q1"};
    ai[7] = "seven";
    as["k"] = "kay";
    fx[0] = "f0"; fx[1] = "f1"; fx[2] = "f2";
    md[0][0] = "m00"; md[0][1] = "m01"; md[1][0] = "m10"; md[1][1] = "m11";
    dy[0] = "d0"; dy[1] = "d1";
    note("start");

    // Every update waits for the NBA region, and the value and the index are
    // those of the statement's execution.
    s = "one"; i = 0;
    fx[i] <= s;
    s = "two"; i = 1;
    fx[i] <= s;
    fx[2] <= "x";
    fx[2] <= "y";
    md[1][0] <= "M10";
    md[0][1] <= "M01";
    dy[1] <= "D1";
    q[0] <= "Q0";
    q[5] <= "ignored";
    ai[7] <= "SEVEN";
    ai[9] <= "nine";
    as["k"] <= "KAY";
    as["j"] <= "jay";
    note("same-step");
    #1;
    note("next-step");

    // A delayed nonblocking assignment evaluates now and updates later.
    s = "late";
    fx[0] <= #2 s;
    s = "changed";
    #1;
    note("before-delay");
    #2;
    note("after-delay");
    $finish;
  end
endmodule
