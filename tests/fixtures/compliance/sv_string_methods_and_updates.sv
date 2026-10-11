// fsim: top=sv_string_methods_and_updates
// IEEE 1800-2017 8.10: class methods that return a string; 11.4.1:
// compound assignments and increments of unpacked array elements selected
// by variable indices; 8.21: a virtual method called through a pure
// virtual prototype runs the object's override.
module sv_string_methods_and_updates;
  virtual class shape;
    pure virtual function int sides();
    function string kind();
      return sides() == 3 ? "triangle" : "polygon";
    endfunction
  endclass

  class triangle extends shape;
    virtual function int sides();
      return 3;
    endfunction
  endclass

  class label;
    string text;
    function new(string t);
      text = t;
    endfunction
    function string get();
      return text;
    endfunction
  endclass

  int histogram[4];
  int grid[2][3];
  int index;
  int failures = 0;
  shape s;
  triangle t;
  label l;

  initial begin
    t = new;
    s = t;
    l = new("name");
    if (s.sides() != 3) failures++;
    if (l.get() != "name") failures++;
    if ({l.get(), "!"} != "name!") failures++;
    index = 2;
    histogram[index] += 5;
    histogram[index]++;
    grid[1][index] += 4;
    grid[index - 2][0]++;
    if (histogram[2] != 6 || grid[1][2] != 4 || grid[0][0] != 1) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
