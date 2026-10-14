// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
class query_item;
  int value;
  function new(int initial_value);
    value = initial_value;
  endfunction
endclass
class query_owner;
  query_item items[2:3][5:3];
  function new();
    for (int i = 2; i <= 3; i++)
      for (int j = 5; j >= 3; j--)
        items[i][j] = new(10*i+j);
  endfunction
  task check();
    int visits;
    query_item selected;
    visits = 0;
    if ($dimensions(items) != 2 || $unpacked_dimensions(items) != 2
        || $left(items, 1) != 2 || $right(items, 1) != 3
        || $left(items, 2) != 5 || $right(items, 2) != 3
        || $increment(items, 1) != -1 || $increment(items, 2) != 1)
      $fatal(1, "class handle array bounds");
    foreach (items[i,j]) begin
      selected = items[i][j];
      if (selected.value != 10*i+j)
        $fatal(1, "foreach class handle array value");
      visits++;
    end
    if (visits != 6) $fatal(1, "foreach class handle array count");
  endtask
endclass
module top;
  query_owner owner;
  query_item plain[3:1];
  int dimension;
  initial begin
    owner = new;
    owner.check();
    if ($dimensions(plain) != 1 || $unpacked_dimensions(plain) != 1
        || $left(plain) != 3 || $right(plain) != 1)
      $fatal(1, "unpacked handle dimension gained a packed dimension");
    dimension = 2;
    if ($left(plain, dimension) !== 32'bx)
      $fatal(1, "invalid handle array dimension did not return unknown");
    $display("PASS");
    $finish;
  end
endmodule
