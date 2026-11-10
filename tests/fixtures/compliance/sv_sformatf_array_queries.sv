// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 21.3.3, 7.5.2, 7.9.1-7.9.7, 7.10.2, 7.12.3, 20.8.1,
// 21.2.1.3: the array query and reduction methods that return integral
// values are ordinary $sformatf / $sformat arguments. exists, first, last,
// next and prev return int; size and num return int; a reduction returns
// the element type, or the with expression's type; $clog2 returns an
// integer. Each call assigns its result; %d pads to the value's size. The
// receiver may be a hierarchical reference to a container of an interface
// or a child instance elaborated after this process (23.6).
interface query_ifc;
  int iq[$] = '{1, 2, 3};
  int ia[int] = '{2: 20};
endinterface

module query_child;
  int cq[$] = '{4, 5};
endmodule

module top;
  query_ifc i();
  query_child c();
  int a[int];
  string sa[string];
  int q[$];
  int m[$];
  int d[];
  byte bq[$];
  string r;
  int k;
  int k2;
  int failures;

  function string query(int key);
    return $sformatf("%0d", a.exists(key));
  endfunction

  task automatic check(string label, string got, string expected);
    if (got != expected) begin
      $display("FAIL %s: got '%s' expected '%s'", label, got, expected);
      failures++;
    end
  endtask

  initial begin
    a[3] = 30;
    a[7] = 70;
    sa["x"] = "y";
    q = '{1, 2, 3};
    m = '{7, 3, 11};
    d = new[4];
    d[1] = 5;
    d[3] = 2;
    bq = '{100, 100, 100};
    k = 3;

    r = "unset"; r = $sformatf("e=%0d", a.exists(k));
    check("exists", r, "e=1");
    r = "unset"; r = $sformatf("e=%0d", a.exists(3) + 0);
    check("exists+0", r, "e=1");
    r = "unset"; r = $sformatf("e=%0d", a.exists(5));
    check("exists missing", r, "e=0");
    r = "unset"; r = $sformatf("e=%0d", sa.exists("x"));
    check("string exists", r, "e=1");
    r = "unset"; r = $sformatf("[%d]", a.exists(k));
    check("exists padding", r, "[          1]");
    r = "unset"; r = $sformatf("n=%0d s=%0d q=%0d d=%0d sa=%0d",
      a.num(), a.size(), q.size(), d.size(), sa.exists("x"));
    check("mixed", r, "n=2 s=2 q=3 d=4 sa=1");

    k2 = 0;
    r = "unset"; r = $sformatf("%0d", a.first(k2));
    check("first", $sformatf("%s:%0d", r, k2), "1:3");
    r = "unset"; r = $sformatf("%0d", a.last(k2));
    check("last", $sformatf("%s:%0d", r, k2), "1:7");
    r = "unset"; r = $sformatf("%0d", a.prev(k2));
    check("prev", $sformatf("%s:%0d", r, k2), "1:3");
    r = "unset"; r = $sformatf("%0d", a.next(k2));
    check("next", $sformatf("%s:%0d", r, k2), "1:7");
    r = "unset"; r = $sformatf("%0d", a.next(k2));
    check("next at end", $sformatf("%s:%0d", r, k2), "0:7");

    r = "unset"; r = $sformatf("%0d %0d %0d", q.sum(), q.product(), d.sum());
    check("sum product", r, "6 6 7");
    r = "unset"; r = $sformatf("%0d %0d %0d", m.and(), m.or(), m.xor());
    check("and or xor", r, "3 15 15");
    r = "unset"; r = $sformatf("%0d", q.sum() with (item * 2));
    check("sum with", r, "12");
    r = "unset"; r = $sformatf("%0d", q.sum() with (item.index));
    check("sum with index", r, "3");
    r = "unset"; r = $sformatf("[%d]", bq.sum());
    check("byte sum", r, "[  44]");
    r = "unset"; r = $sformatf("%0d", bq.sum() with (int'(item)));
    check("byte sum widened", r, "300");

    r = "unset"; r = $sformatf("%0d %0d", $unsigned(a.exists(k)),
      $clog2(q.size()));
    check("system functions", r, "1 2");
    r = "unset"; $sformat(r, "%0d/%0d/%0d", a.exists(k), q.sum(), a.next(k));
    check("$sformat", r, "1/6/1");
    check("function", {query(3), query(4)}, "10");
    r = "unset"; r = $sformatf("%0d %0d %0d %0d", i.iq.size(), c.cq.size(),
      i.ia.exists(2), i.iq.sum());
    check("hierarchical receivers", r, "3 2 1 6");

    if (failures == 0) begin
      $display("PASS");
    end
  end
endmodule
