// fsim-expect-run: top=sv_sva_sampled_null_handle std=2017
// fsim-expect-text: null or stale SystemVerilog class handle
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.5.1, 8.4: $sampled(h.p) reads the property through the
// object h holds in the Preponed region of its time slot. In the slot at 5,
// h is null there (the process allocates its object later in the slot), so
// the read goes through a null handle, a run-time error, although h holds an
// object when the read runs.
class C;
  int p;
endclass

module sv_sva_sampled_null_handle;
  C h;
  initial begin
    #5;
    h = new;
    h.p = 1;
    $display("sampled %0d", $sampled(h.p));
  end
endmodule
