// fsim: top=sv_interface_port_task_statics std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 13.3.2, 25.3, 10.4.2: a static task of an interface keeps
// one set of static formals in the interface instance; a call through an
// interface port, directly or from a task of a module called by its
// hierarchical name (23.8), uses the connected instance's formals, which a
// nonblocking assignment may write.
interface sv_interface_port_task_statics_if;
  logic tck = 0;
  logic tdo = 1;
  task tsk(output logic [31:0] data_o, input logic [31:0] data_i);
    @(posedge tck);
    data_o[$size(data_i)-1] <= tdo;
  endtask
endinterface

module sv_interface_port_task_statics_user(sv_interface_port_task_statics_if ifc);
  bit [31:0] r;
  task go();
    bit [31:0] read;
    ifc.tsk(read, '0);
  endtask
  initial begin
    fork
      ifc.tsk(r, '0);
    join_none
  end
endmodule

module sv_interface_port_task_statics;
  sv_interface_port_task_statics_if ifc();
  sv_interface_port_task_statics_user uut(.ifc);
  int failures;

  initial begin
    fork
      uut.go();
    join_none
    #1 ifc.tck = 1;
    #1;
    if (ifc.tsk.data_o[31] !== 1'b1) begin
      $display("FAIL static formal %b", ifc.tsk.data_o[31]);
      failures++;
    end
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
