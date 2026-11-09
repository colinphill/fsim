// SPDX-License-Identifier: Apache-2.0
//
// SystemVerilog threads that make every kind of effect FSIM_KERNEL_VERIFY's
// reference replay has to shadow: a blocking write read back by the next
// statement (IEEE 1800-2023 10.4.1), $display output (21.2), a task call
// (13.3), fork-join_none (9.3.2), string operations (6.16), and $fwrite /
// $fscanf on a file (21.3). Each thread must be replayed (none is skipped) and
// the run must print what the semantics give:
//   A: x = 5 at 1, y = x + 1 = 6, x = y = 6 at 2; B prints at 3: x=6 y=6.
//   C: bump(2) at 1, bump(3) at 2; D's branch adds 10 at 3; C and D print
//      cnt = 15 at 4 and 5.
//   E: s = "ab", s = {s, "cd"} at 1; prints "abcd" and 4 at 2.
//   G: prints the constant string concatenation "abcd" at 2 (string display
//      through the generic bridge).
//   H: $fread reads the two bytes 0x41 0x42 back into an array at 2:
//      "G 2 65 66".
//   F: writes "hello 7" at 1, reads the 7 back at 2.
module top;
  logic [7:0] x = 8'd0;
  logic [7:0] y = 8'd0;
  int cnt = 0;
  int n = 0;
  int fd;
  int fd2;
  string s;
  reg [7:0] mem [0:1];
  int fd3;
  int got;

  task automatic bump(input int d);
    cnt = cnt + d;
  endtask

  initial begin
    #1 x = 8'd5;
    y = x + 8'd1;
    #1 x = y;
  end

  initial begin
    #3 $display("B x=%0d y=%0d", x, y);
  end

  initial begin
    #1 bump(2);
    #1 bump(3);
    #2 $display("C cnt=%0d", cnt);
  end

  initial begin
    #1 fork
      #2 cnt = cnt + 10;
    join_none
    #4 $display("D cnt=%0d", cnt);
  end

  initial begin
    #1 s = "ab";
    s = {s, "cd"};
    #1 $display("E %s %0d", s, s.len());
  end

  initial begin
    #2 $display({"ab", "cd"});
  end

  initial begin
    fd = $fopen("verify_out.txt", "w");
    #1 $fwrite(fd, "hello %0d\n", 7);
    $fclose(fd);
  end

  // The reader uses $fscanf, which the kernel still runs on its operation
  // tier.
  initial begin
    #2 fd2 = $fopen("verify_out.txt", "r");
    void'($fscanf(fd2, "hello %d", n));
    $fclose(fd2);
    $display("F %0d", n);
  end

  // $fread into an array (IEEE 1800-2023 21.3.4.4): the file holds the bytes
  // 0x41 and 0x42.
  initial begin
    fd3 = $fopen("bin_out.txt", "wb");
    $fwrite(fd3, "%c%c", 8'h41, 8'h42);
    $fclose(fd3);
    #2 fd3 = $fopen("bin_out.txt", "rb");
    got = $fread(mem, fd3);
    $fclose(fd3);
    $display("G %0d %0d %0d", got, mem[0], mem[1]);
  end
endmodule
