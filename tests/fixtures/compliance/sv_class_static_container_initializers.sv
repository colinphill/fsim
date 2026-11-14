// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.9, 6.21: a static property's initializer runs before any
// procedure starts, and the class code it calls may write the static arrays,
// queues and associative arrays of its own class and of the classes of the
// packages it depends on, element by element or whole. Here the
// initializers write a string- and an integer-keyed associative array, a
// fixed array, a dynamic array and queues: reg_c's initializer its own
// class's containers and K's log, the package variable initializers K's
// log and reg_c's array, reg_q's initializer (package q imports p) K's log
// and its own queue, and the initializer of U (outside every package) its
// own associative array. Each of the three calls of K::hit counts once,
// in some startup order: the counts and the set of tags are checked, not
// the order.
package p;
  class K;
    static int hits;
    static int log[$];
    static function int hit(int tag);
      hits++;
      log.push_back(tag);
      return hits;
    endfunction
  endclass
  class reg_c;
    static int m[string];
    static int k[int];
    static int a[4];
    static int d[];
    static int registered = add();
    static function int add();
      m["c"] = 1;
      k[3] = 30;
      a[1] = 5;
      d = new[2];
      d[1] = 7;
      return K::hit(1);
    endfunction
  endclass
  function automatic int set_k();
    reg_c::k[5] = 50;
    return 4;
  endfunction
  int pv = K::hit(2);
  int pk = set_k();
endpackage

package q;
  import p::*;
  class reg_q;
    static string names[$];
    static int registered = enroll();
    static function int enroll();
      names.push_back("q");
      return K::hit(3);
    endfunction
  endclass
endpackage

class U;
  static int u[string];
  static int registered = init();
  static function int init();
    u["x"] = 9;
    return 1;
  endfunction
endclass

module top;
  int failures;
  int tags[$];
  initial begin
    if (p::K::hits != 3 || p::K::log.size() != 3) begin
      $display("FAIL hits=%0d log=%0d", p::K::hits, p::K::log.size());
      failures++;
    end
    tags = p::K::log;
    tags.sort();
    if (tags.size() != 3 || tags[0] != 1 || tags[1] != 2 || tags[2] != 3) begin
      $display("FAIL tags");
      failures++;
    end
    if (p::reg_c::registered + p::pv + q::reg_q::registered != 6
        || p::reg_c::registered < 1 || p::pv < 1
        || q::reg_q::registered < 1) begin
      $display("FAIL counts %0d %0d %0d", p::reg_c::registered, p::pv,
        q::reg_q::registered);
      failures++;
    end
    if (p::reg_c::m.num() != 1 || p::reg_c::m["c"] != 1) begin
      $display("FAIL m");
      failures++;
    end
    if (p::reg_c::k.num() != 2 || p::reg_c::k[3] != 30
        || p::reg_c::k[5] != 50 || p::pk != 4) begin
      $display("FAIL k");
      failures++;
    end
    if (p::reg_c::a[0] != 0 || p::reg_c::a[1] != 5 || p::reg_c::a[3] != 0) begin
      $display("FAIL a");
      failures++;
    end
    if (p::reg_c::d.size() != 2 || p::reg_c::d[0] != 0
        || p::reg_c::d[1] != 7) begin
      $display("FAIL d");
      failures++;
    end
    if (q::reg_q::names.size() != 1 || q::reg_q::names[0] != "q") begin
      $display("FAIL names");
      failures++;
    end
    if (U::registered != 1 || U::u.num() != 1 || U::u["x"] != 9) begin
      $display("FAIL u");
      failures++;
    end
    if (failures == 0)
      $display("PASS");
  end
endmodule
