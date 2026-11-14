// SPDX-License-Identifier: Apache-2.0
//
// Nonblocking assignments to static variables (IEEE 1800-2023 6.21, 10.4.2,
// 13.4.4) and variable declaration assignments that read variables (6.8,
// 10.5):
//  - a function may make nonblocking assignments to variables outside it and
//    to its own static variables; the update lands in the NBA region;
//  - a variable of an initial block is static (6.21), so a nonblocking
//    assignment may write it;
//  - an unpacked array initialized by a pattern that reads variables is
//    evaluated, in source order, before any procedure starts.
module top;
  int total = 0;
  int log_n = 0;
  logic [3:0] vec = 4'b0;

  function void bump(int v);
    total <= total + v;
    vec[1] <= 1'b1;
  endfunction

  function int note(int v);
    log_n <= v;
    return v + 1;
  endfunction

  function automatic int keep(int v);
    static int latest = 0;
    latest <= v;
    return latest;
  endfunction

  int base = 3;
  int arr[3] = '{base, base + 1, base + 2};
  int q[$] = '{base, 9};
  int dyn[] = '{arr[0], arr[2]};

  initial begin
    static int s = 1;
    int u = 1;
    int w = 2;
    int r;
    int k;
    $display("sns init %0d %0d %0d q=%0d,%0d dyn=%0d,%0d", arr[0], arr[1],
        arr[2], q[0], q[1], dyn[0], dyn[1]);
    bump(2);
    r = note(7);
    k = keep(5);
    s <= 9;
    u <= w + 1;
    w <= 20;
    $display("sns same-step total=%0d log=%0d vec=%b r=%0d k=%0d s=%0d u=%0d w=%0d",
        total, log_n, vec, r, k, s, u, w);
    #1;
    k = keep(6);
    $display("sns next-step total=%0d log=%0d vec=%b k=%0d s=%0d u=%0d w=%0d",
        total, log_n, vec, k, s, u, w);
    $finish;
  end
endmodule
