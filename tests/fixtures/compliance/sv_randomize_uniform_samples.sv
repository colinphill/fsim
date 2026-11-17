// fsim: top=sv_randomize_uniform_samples std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 18.5.10, 18.12: every combination of legal values is
// equally probable. A 32-bit member under a constraint, an element of a
// random array of structures and a std::randomize variable each draw a
// value of simple bit structure (zero, all ones, one bit set, a low mask)
// or a bound of the range about as rarely as any other value, so 2000
// draws see almost none.
typedef struct { rand bit [31:0] x; } sv_randomize_uniform_samples_t;
class sv_randomize_uniform_samples_c;
  rand sv_randomize_uniform_samples_t items[];
  rand bit [31:0] z;
  constraint order { foreach (items[i]) { if (i > 0) items[i].x > items[i-1].x; } }
  constraint bound { z > 1000; }
  function new();
    items = new[1];
  endfunction
endclass

module sv_randomize_uniform_samples;
  function automatic bit structured(bit [31:0] v);
    return v == 0 || v == '1 || $onehot(v) || $onehot(v + 1) || v == 1001;
  endfunction
  int failures;

  initial begin
    sv_randomize_uniform_samples_c object = new;
    bit [31:0] w;
    int seen_x;
    int seen_z;
    int seen_w;
    repeat (2000) begin
      if (!object.randomize()) failures++;
      seen_x += structured(object.items[0].x);
      seen_z += structured(object.z);
      if (!std::randomize(w) with { w > 1000; }) failures++;
      seen_w += structured(w);
    end
    if (seen_x > 2 || seen_z > 2 || seen_w > 2) begin
      $display("FAIL structured draws x=%0d z=%0d w=%0d", seen_x, seen_z, seen_w);
      failures++;
    end
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
