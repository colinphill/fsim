// fsim: top=sv_generate_interfaces_and_upward_names
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 25.3 and 27.4: members of an interface instance declared in
// a generate block, driven by continuous and procedural assignments in the
// block; 23.8: functions of module instance array elements called by
// hierarchical name, and an upward name that begins with an enclosing
// module's name.
interface flag_if;
  logic value;
  int count;
endinterface

module slot;
  int stored = 0;
  function automatic int put(input int value);
    stored = value;
    return value;
  endfunction
endmodule

module holder;
  int marker = 41;
  slot slots[1:0]();
  leaf_reader reader();
endmodule

module leaf_reader;
  int seen = 0;
  initial seen = holder.marker + 1;
endmodule

module sv_generate_interfaces_and_upward_names;
  holder h();

  if (1) begin : g
    flag_if bus();
    assign bus.value = 1'b1;
    initial bus.count = 7;
  end

  initial begin
    int failures = 0;
    void'(h.slots[0].put(5));
    void'(h.slots[1].put(9));
    #1;
    if (g.bus.value !== 1'b1) failures++;
    if (g.bus.count != 7) failures++;
    if (h.slots[0].stored != 5 || h.slots[1].stored != 9) failures++;
    if (h.reader.seen != 42) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
