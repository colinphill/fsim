// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.20, 8.25: a parameterized class may extend its type
// parameter (`class port #(type IF = root) extends IF`), so a specialization
// inherits the virtual methods of the class its actual names, and the
// methods the class declares itself are further virtual methods. A call
// through a handle runs the override the object's class selects: here a
// method of the parameterized class (connect), one of the actual's (put)
// and one both declare (name), through handles of each type.
module top;
  class root;
    virtual function string name(); return "root"; endfunction
  endclass

  class put_if #(type T = int);
    virtual function void put(T t); endfunction
    virtual function void get(output T t); t = 0; endfunction
    virtual function string name(); return "put_if"; endfunction
  endclass

  class port #(type IF = root) extends IF;
    typedef port #(IF) this_type;
    this_type provider;
    int connected;
    virtual function void connect(this_type p);
      provider = p;
      connected++;
    endfunction
    virtual function string name(); return "port"; endfunction
  endclass

  class put_port #(type T = int) extends port #(put_if #(T));
    T last;
    virtual function void put(T t); last = t; endfunction
  endclass

  int failures;

  initial begin
    put_port #(int) a = new;
    put_port #(int) b = new;
    port #(put_if #(int)) as_port = a;
    put_if #(int) as_if = a;
    port #(root) plain = new;
    root as_root = plain;
    as_port.connect(b);
    a.connect(b);
    as_if.put(5);
    if (a.connected != 2 || a.provider != b || a.last != 5)
      failures++;
    if (as_if.name() != "port" || as_port.name() != "port"
        || as_root.name() != "port")
      failures++;
    plain.connect(plain);
    if (plain.connected != 1 || plain.provider != plain)
      failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
