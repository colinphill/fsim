// fsim: top=sv_pattern_files_and_production_defaults
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 21.2.1.7: `%p` written to a file shows an enumeration
// value by name; 18.17.7: a production formal's default value may contain
// commas inside a call.
module sv_pattern_files_and_production_defaults;
  typedef enum {RED, GREEN} color_t;
  color_t color = GREEN;
  int fd;
  int total;
  int failures;
  string line;

  function int add(int a, int b);
    return a + b;
  endfunction

  initial begin
    fd = $fopen("sv_pattern_files_and_production_defaults.txt", "w");
    $fwrite(fd, "%p\n", color);
    $fclose(fd);
    fd = $fopen("sv_pattern_files_and_production_defaults.txt", "r");
    void'($fgets(line, fd));
    $fclose(fd);
    if (line != "GREEN\n") failures++;
    randsequence (main)
      main : step(1) step();
      step (int x = add(2, 3)) : { total += x; };
    endsequence
    if (total != 6) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
