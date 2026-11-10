// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.9.11, 10.9.1, 10.9.2, 10.10, 6.8: a container
// declaration takes an assignment pattern or an unpacked array
// concatenation as its initializer, at every scope. An associative array
// takes `'{key: value}` entries, a `default:` value (what a read of a key
// with no entry returns, 7.8.6, 7.9.11) or both; a dynamic array or a
// queue takes elements, `{}` or a replication. String and packed elements
// alike, in a module, a package, an interface, a class (static and
// non-static properties), and a task (automatic and static variables).
package p;
  int pa[int] = '{2: 20, default: -1};
  string ps[$] = '{"m", "n"};
endpackage

interface ifc;
  int ia[int] = '{default: 9};
  byte iq[$] = '{2{8'h7f}};
endinterface

module top;
  import p::*;

  int a1[int] = '{default: 7};
  int a2[int] = '{1: 10, 5: 50};
  int a3[int] = '{1: 10, default: 7};
  string a4[string] = '{"x": "y", default: "d"};
  logic [7:0] a5[int] = '{3: 8'hab, default: 8'h5a};
  int d1[] = '{1, 2, 3};
  int d2[] = {};
  int d3[] = '{3{4}};
  string d4[] = '{2{"z"}};
  int q1[$] = '{1, 2, 3};
  int q2[$] = {};
  int q3[$] = {1, 2};
  string q4[$] = '{"p", "q"};
  logic [7:0] q5[$] = '{8'h12, 8'h34};

  ifc i();
  int failures;

  class C;
    int ca[int] = '{4: 40, default: 3};
    string cq[$] = '{"c", "d"};
    static int sa[int] = '{default: 11};
    static string sq[$] = '{2{"s"}};
    function string summary();
      return $sformatf("%0d,%0d,%0d %0d:%s %0d,%0d %0d:%s", ca.num(), ca[4],
        ca[0], cq.size(), cq[1], sa.num(), sa[5], sq.size(), sq[1]);
    endfunction
  endclass

  task automatic check(string label, string got, string expected);
    if (got != expected) begin
      $display("FAIL %s: got '%s' expected '%s'", label, got, expected);
      failures++;
    end
  endtask

  task automatic local_containers(output string result);
    int la[int] = '{6: 60, default: 2};
    string lq[$] = '{"l", "m", "n"};
    int ld[] = '{2{5}};
    result = $sformatf("%0d,%0d,%0d %0d:%s %0d:%0d", la.num(), la[6], la[1],
      lq.size(), lq[2], ld.size(), ld[1]);
  endtask

  task static_containers(output string result);
    static int sa[int] = '{1: 1, default: 8};
    static int sq[$] = '{3, 4};
    result = $sformatf("%0d,%0d,%0d %0d:%0d", sa.num(), sa[1], sa[2],
      sq.size(), sq[1]);
  endtask

  initial begin
    C c = new;
    string text;
    #1;
    check("default only", $sformatf("%0d,%0d", a1.num(), a1[9]), "0,7");
    check("keys", $sformatf("%0d,%0d,%0d,%0d", a2.num(), a2[1], a2[5], a2[2]),
      "2,10,50,0");
    check("keys and default",
      $sformatf("%0d,%0d,%0d", a3.num(), a3[1], a3[4]), "1,10,7");
    check("string keys and default",
      $sformatf("%0d,%s,%s", a4.num(), a4["x"], a4["q"]), "1,y,d");
    check("packed keys and default",
      $sformatf("%0d,%h,%h", a5.num(), a5[3], a5[0]), "1,ab,5a");
    check("dynamic elements", $sformatf("%0d,%0d", d1.size(), d1[2]), "3,3");
    check("dynamic empty", $sformatf("%0d", d2.size()), "0");
    check("dynamic replication",
      $sformatf("%0d,%0d", d3.size(), d3[2]), "3,4");
    check("dynamic strings", $sformatf("%0d,%s", d4.size(), d4[1]), "2,z");
    check("queue elements", $sformatf("%0d,%0d", q1.size(), q1[2]), "3,3");
    check("queue empty", $sformatf("%0d", q2.size()), "0");
    check("queue concatenation", $sformatf("%0d,%0d", q3.size(), q3[1]), "2,2");
    check("queue strings", $sformatf("%0d,%s", q4.size(), q4[1]), "2,q");
    check("queue packed", $sformatf("%0d,%h", q5.size(), q5[1]), "2,34");
    check("package", $sformatf("%0d,%0d,%0d %0d:%s", pa.num(), pa[2], pa[3],
      ps.size(), ps[0]), "1,20,-1 2:m");
    check("interface", $sformatf("%0d,%0d %0d:%h", i.ia.num(), i.ia[1],
      i.iq.size(), i.iq[1]), "0,9 2:7f");
    check("class", c.summary(), "1,40,3 2:d 0,11 2:s");
    local_containers(text);
    check("automatic task variables", text, "1,60,2 3:n 2:5");
    static_containers(text);
    check("static task variables", text, "1,1,8 2:4");
    if (failures == 0) begin
      $display("PASS");
    end
  end
endmodule
