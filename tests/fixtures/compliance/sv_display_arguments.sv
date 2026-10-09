// fsim: top=sv_display_arguments std=2017
// IEEE 1800-2017 21.2.1: an empty display argument prints a space, a string
// literal after the first argument is a format for the arguments that follow,
// and a decimal value without an explicit field width is padded to the width
// of its largest magnitude (21.2.1.3). 21.3.1: a file descriptor is usable by
// every process, not only the one that opened it.
module sv_display_arguments;
  logic [3:0] nibble = 4'd5;
  integer count = 42;
  logic signed [7:0] negative = -8'sd7;
  logic [15:0] word = 16'd300;
  integer fd;
  int failures = 0;

  task automatic check(string actual, string expected);
    if (actual != expected) begin
      $display("FAIL got [%s] expected [%s]", actual, expected);
      failures++;
    end
  endtask

  initial begin
    fd = $fopen("sv_display_arguments.txt", "w");
  end

  initial begin
    #1;
    $fdisplay(fd, "written by another process");
    #1;
    $fclose(fd);
  end

  initial begin
    string line;
    integer reader, length;
    #3;
    check($sformatf("%d", nibble), " 5");
    check($sformatf("%d", count), "         42");
    check($sformatf("%d", negative), "  -7");
    check($sformatf("%0d|%3d|%-4d|", word, nibble, nibble), "300|  5|5   |");
    check($sformatf("[%d]", word), "[  300]");
    reader = $fopen("sv_display_arguments.txt", "r");
    length = $fgets(line, reader);
    $fclose(reader);
    check(line, "written by another process\n");
    // Visible output, compared by eye against other simulators.
    $display(nibble,,count);
    $display(nibble,, "<%0d>", count);
    if (failures == 0) $display("PASS");
  end
endmodule
