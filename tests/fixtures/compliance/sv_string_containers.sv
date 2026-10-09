// fsim: top=sv_string_containers std=2017
// IEEE 1800-2017 6.18: a typedef of a string-element unpacked container;
// 7.9.1: num() of an associative array is its number of entries.
module sv_string_containers;
  typedef string names_t[int];
  typedef int counts_t[string];
  names_t names;
  counts_t counts;

  initial begin
    int failures = 0;
    names[3] = "three";
    names[7] = "seven";
    counts["a"] = 1;
    if (names.num() != 2 || names.size() != 2) failures++;
    if (names[7] != "seven") failures++;
    if (counts.num() != 1 || !counts.exists("a")) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
