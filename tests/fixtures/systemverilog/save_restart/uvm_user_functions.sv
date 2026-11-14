// SPDX-License-Identifier: Apache-2.0
// Functions of classes deriving from the UVM library, called from code of the
// design (IEEE 1800-2023 8.20, 13.4; IEEE 1800.2-2020 5.3, 13.1): a run of the
// compiled design enters no class executor for them. An object class has a
// virtual function describe() that its derived class overrides, called through
// a base handle, and a plain function twice() the override calls; the
// component counter has functions that read and write its properties, call
// one another and return a string.
//
// Hand-derived output: total starts at 0. add(2, 3) makes it 5 and returns 5,
// add(10, 20) makes it 35 and returns 35. The object a holds v = 7 as a
// base_item: "base v=7" and twice() is 14. The object b holds v = 21 as a
// derived_item: describe() through a base handle runs the override, "derived
// v=21 twice=42". summary() joins the instance name and the total: "t t=35".
// The component also has a function that takes nothing and returns a library
// handle, written by the user (not a registry utility): get_cfg() returns the
// object stored by set_cfg, here the derived_item d, whose v is 21: "cfg v=21".
// A macro of the testbench's own, FSIM_ACCESSOR, declares answer() in the
// component: the macro is defined in user source, so its method is ordinary
// code and runs compiled too; answer() is total + 100, here 135. A static
// function of the object class, scale(4), is 4 * 3 = 12, and get_type_name(),
// which the library's type macro declares (it returns the class's type_name),
// names the object's class through a virtual call: "base_item" for a,
// "derived_item" for b; both run compiled too. The registry's get answers
// the class's one registry object, the wrapper get_type() returns (IEEE
// 1800.2-2020 8.2.4.2.1, B.2.1), however the call names the registry:
// type_id::get() in a static function of the class, derived_item's
// type_id::get(), and the registry specialization itself, which is not
// base_item's: "registry=111".
`timescale 1ns/1ns
import uvm_pkg::*;
`include "uvm_macros.svh"

class base_item extends uvm_object;
  int v;
  `uvm_object_utils(base_item)
  function new(string name = "base_item");
    super.new(name);
  endfunction
  virtual function string describe();
    return $sformatf("base v=%0d", v);
  endfunction
  function int twice();
    return v * 2;
  endfunction
  static function int scale(int k);
    int s;
    s = k * 3;
    return s;
  endfunction
  static function bit registered();
    return type_id::get() == get_type();
  endfunction
endclass

class derived_item extends base_item;
  `uvm_object_utils(derived_item)
  function new(string name = "derived_item");
    super.new(name);
  endfunction
  virtual function string describe();
    return $sformatf("derived v=%0d twice=%0d", v, twice());
  endfunction
endclass

`define FSIM_ACCESSOR(NAME, VALUE) \
  function int NAME(); \
    return VALUE; \
  endfunction

class counter extends uvm_component;
  `uvm_component_utils(counter)
  int total;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  `FSIM_ACCESSOR(answer, total + 100)
  function int add(int a, int b);
    total += a + b;
    return total;
  endfunction
  uvm_object m_cfg;
  function void set_cfg(uvm_object o);
    m_cfg = o;
  endfunction
  function uvm_object get_cfg();
    return m_cfg;
  endfunction
  function string label();
    return get_name();
  endfunction
  function string summary();
    return $sformatf("%s t=%0d", label(), total);
  endfunction
endclass

class mytest extends uvm_test;
  `uvm_component_utils(mytest)
  counter t;
  function new(string name, uvm_component parent);
    super.new(name, parent);
  endfunction
  function void build_phase(uvm_phase phase);
    super.build_phase(phase);
    t = counter::type_id::create("t", this);
  endfunction
  function void report_phase(uvm_phase phase);
    base_item a = new("a");
    base_item b;
    derived_item d = new("d");
    int r1, r2;
    r1 = t.add(2, 3);
    r2 = t.add(10, 20);
    $display("add=%0d,%0d", r1, r2);
    a.v = 7;
    d.v = 21;
    b = d;
    $display("a: %s twice=%0d", a.describe(), a.twice());
    $display("b: %s", b.describe());
    $display("%s", t.summary());
    $display("answer=%0d", t.answer());
    $display("scale=%0d type=%s/%s", base_item::scale(4), a.get_type_name(),
      b.get_type_name());
    $display("registry=%0d%0d%0d", base_item::registered(),
      derived_item::type_id::get() == derived_item::get_type(),
      uvm_object_registry#(derived_item, "derived_item")::get()
        != base_item::get_type());
    t.set_cfg(d);
    if ($cast(b, t.get_cfg()))
      $display("cfg v=%0d", b.v);
  endfunction
endclass

module top;
  initial run_test("mytest");
endmodule
