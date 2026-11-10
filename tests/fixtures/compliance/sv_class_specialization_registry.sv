// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.7, 8.16, 8.25, 8.25.1, 7.8, 7.10: static methods that make
// and register one object per specialization, the pattern of UVM's
// uvm_callbacks#(T,CB)::get(), run in the class method executor
// (post_randomize). A bare `new` constructs the class type of its target in
// the target's specialization, and `C#(7)::new` the specialization it names
// (8.7, 8.8, 8.25); $cast writes a static property, and a handle of another
// specialization of a class does not convert (8.16, 8.25); static
// associative arrays and queues of handles take elements through the class
// scope, and an object's queue through a handle read from one (7.8, 7.10);
// a static void function called as a statement through a typedef, through
// the class name or unqualified runs in the caller's specialization or the
// base class that declares it (8.25.1, 13.4.1); `void'(f())` discards a
// result (6.24.1).
class tid #(int P = 1);
  static tid #(P) m_inst;
  static int made;
  function int p();
    return P;
  endfunction
  static function tid #(P) get();
    if (m_inst == null) begin
      m_inst = new;
      made++;
    end
    return m_inst;
  endfunction
endclass

class reg_base;
  static reg_base by_object[reg_base];
  static reg_base by_name[string];
  static reg_base order[$];
  static int initialized;
  reg_base children[$];
  virtual function int p();
    return 0;
  endfunction
  static function void m_initialize();
    initialized++;
  endfunction
endclass

class reg_cb #(int P = 1) extends reg_base;
  typedef reg_cb #(P) this_type;
  typedef reg_base super_type;
  static this_type m_inst;
  static reg_base m_base;
  static tid #(P) m_id;
  static string m_name;
  virtual function int p();
    return P;
  endfunction
  static function void note(string name);
    m_name = name;
  endfunction
  static function this_type get();
    if (m_inst == null) begin
      super_type::m_initialize();
      reg_base::m_initialize();
      m_initialize();
      void'(tid #(P)::get());
      m_id = tid #(P)::get();
      m_inst = new;
      $cast(m_base, m_inst);
      reg_base::by_object[m_inst] = m_base;
      reg_base::order.push_back(m_inst);
      if (reg_base::order.size() > 1)
        reg_base::order[0].children.push_back(m_inst);
      if (P == 4)
        note("four");
      else
        note("seven");
      reg_base::by_name[m_name] = m_inst;
    end
    return m_inst;
  endfunction
endclass

class R;
  rand bit [3:0] x;
  int failures;
  int calls;
  function void post_randomize();
    reg_cb #(4) a;
    reg_cb #(4) a2;
    reg_cb #(4) cast4;
    reg_cb #(7) b;
    reg_cb #(7) typed;
    reg_base found;
    calls++;
    if (calls != 1)
      return;
    a = reg_cb #(4)::get();
    a2 = reg_cb #(4)::get();
    b = reg_cb #(7)::get();
    if (a == null || b == null || a != a2 || a.p() != 4 || b.p() != 7)
      failures += 1;
    if (tid #(4)::made != 1 || tid #(7)::made != 1)
      failures += 2;
    if (reg_cb #(4)::m_id.p() != 4 || reg_cb #(7)::m_id.p() != 7)
      failures += 4;
    if (reg_base::initialized != 6)
      failures += 8;
    if (reg_base::order.size() != 2 || reg_base::order[0] != a
        || reg_base::order[1] != b)
      failures += 16;
    if (!reg_base::by_object.exists(a) || reg_base::by_object[b] != b
        || reg_base::by_object.num() != 2)
      failures += 32;
    if (reg_base::by_name["four"] != a || reg_base::by_name["seven"] != b)
      failures += 64;
    if (a.children.size() != 1 || a.children[0] != b)
      failures += 128;
    if (reg_cb #(4)::m_name != "four" || reg_cb #(7)::m_name != "seven")
      failures += 256;
    found = b;
    if ($cast(cast4, found) || cast4 != null)
      failures += 512;
    found = a;
    if (!$cast(cast4, found) || cast4 != a)
      failures += 1024;
    typed = reg_cb #(7)::new;
    if (typed.p() != 7)
      failures += 2048;
  endfunction
endclass

module top;
  R r;
  int ok;
  initial begin
    r = new;
    ok = r.randomize();
    if (ok != 1)
      $display("FAIL randomize %0d", ok);
    else if (r.failures != 0)
      $display("FAIL checks %0d", r.failures);
    else
      $display("PASS");
  end
endmodule
