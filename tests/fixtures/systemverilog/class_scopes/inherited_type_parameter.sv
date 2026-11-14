// SPDX-License-Identifier: Apache-2.0
// A method called on a property inherited from a parameterized base class,
// whose type parameter the derived class's extends clause binds (IEEE
// 1800-2023 8.25, 8.13): `req.convert2string()` in the style of a UVM driver,
// as a function, a task and a statement call.
class pkt;
  int id = 3;
  function string convert2string(); return $sformatf("pkt%0d", id); endfunction
  function int ident(); return id; endfunction
  function void bump(int d); id += d; endfunction
endclass

class comp;
  string name = "c";
endclass

class drv #(type REQ = int, type RSP = REQ) extends comp;
  REQ req;
  RSP rsp;
  function new(); req = new; endfunction
endclass

class my extends drv #(pkt);
  task run();
    $display("run %s", req.convert2string());
  endtask
  function string show(); return req.convert2string(); endfunction
  function int bumped(); req.bump(2); return req.ident(); endfunction
endclass

module top;
  my d;
  initial begin
    d = new;
    d.run();
    $display("show %s bumped %0d", d.show(), d.bumped());
  end
endmodule
