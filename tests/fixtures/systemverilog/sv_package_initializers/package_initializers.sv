// SPDX-License-Identifier: Apache-2.0
//
// A package variable whose declaration assignment reads other variables or
// calls a function is initialized before any initial or always procedure
// starts (IEEE 1800-2023 6.8, 6.21, 10.5): in declaration order within a
// package, and the packages one imports before the importing package. Strings,
// real values, fixed arrays, queues, dynamic and associative arrays take part.
package pa;
  int base = 3;
  int a = base + 1;
  int arr[3] = '{base, a, a + 1};
  typedef struct { int x; int y; } pt_t;
  pt_t p = '{base, a};
  typedef struct { int a[2]; int b; } sa_t;
  sa_t sw = '{'{base, a}, a + 1};
  pt_t ps[2] = '{'{base, 1}, '{a, 2}};
  int grid[2][2] = '{'{base, 1}, '{2, a}};
  int nq[2][$] = '{'{base}, '{base, 1}};
  string sq[2][$] = '{'{$sformatf("s%0d", base)}, '{"x", "y"}};
  typedef struct { string n; int v; } ns_t;
  ns_t sn = '{$sformatf("n%0d", base), base};
  ns_t sna[2] = '{'{"p", base}, '{"q", base + 1}};
  // Constant initializers of unpacked structs with a string or an array
  // member: container objects, not packed signals.
  ns_t snc = '{"c", 7};
  sa_t swc = '{'{1, 2}, 3};
  real r = 1.5;
  typedef real r_t;
  typedef shortreal sr_t;
  r_t tr = r + 0.25;
  sr_t tsr = 0.75;
  r_t tr2 = tr + 1.0;
  shortreal sr = 0.5;
  realtime rt = 2.25;
  real r2 = r * 2.0 + base;
  parameter real PR = 2.5;
  string name = $sformatf("n%0d", a);
  string tag = {name, "!"};
  int q[$] = '{base, a};
  int dyn[] = '{a, base};
  int aa[string] = '{"base": base, "a": a};
  function automatic int twice(int x);
    return x * 2;
  endfunction
  function automatic int plus_base(int x);
    return x + base;
  endfunction
  function automatic string nm(int x);
    return $sformatf("fn%0d", x);
  endfunction
  int fromfn = twice(a) + 1;
  int fnq[$] = '{twice(base), twice(a)};
  string fnstr = nm(base);
  int viapb = plus_base(10);
endpackage

package pb;
  import pa::*;
  int c = a + base;
  int cdyn[] = '{a, c};
  int carr[2] = '{a, c};
  real cr = r + c;
  string cs = {pa::name, "-", $sformatf("%0d", c)};
  int viaf = pa::twice(c);
endpackage

module top;
  import pb::*;
  int m = pb::viaf;
  initial begin
    $display("sns pa base=%0d a=%0d name=%s tag=%s q=%0d,%0d dyn=%0d,%0d aa=%0d,%0d",
        pa::base, pa::a, pa::name, pa::tag, pa::q[0], pa::q[1], pa::dyn[0], pa::dyn[1], pa::aa["base"], pa::aa["a"]);
    $display("sns pa-more fromfn=%0d fnq=%0d,%0d fnstr=%s viapb=%0d",
        pa::fromfn, pa::fnq[0], pa::fnq[1],
        pa::fnstr, pa::viapb);
    $display("sns pa-fixed arr=%0d,%0d,%0d p=%0d,%0d", pa::arr[0], pa::arr[1], pa::arr[2],
        pa::p.x, pa::p.y);
    $display("sns pa-aggregate sw=%0d,%0d,%0d ps=%0d,%0d grid=%0d,%0d", pa::sw.a[0], pa::sw.a[1], pa::sw.b,
        pa::ps[0].x, pa::ps[1].x, pa::grid[0][0], pa::grid[1][1]);
    $display("sns pa-nested nq=%0d,%0d sq=%s,%s sn=%s,%0d sna=%s,%0d,%s,%0d", pa::nq[1][0], pa::nq[1][1],
        pa::sq[0][0], pa::sq[1][1], pa::sn.n, pa::sn.v, pa::sna[0].n, pa::sna[0].v, pa::sna[1].n, pa::sna[1].v);
    $display("sns pa-constant snc=%s,%0d swc=%0d,%0d,%0d", pa::snc.n, pa::snc.v,
        pa::swc.a[0], pa::swc.a[1], pa::swc.b);
    $display("sns pa-real r=%0.2f sr=%0.2f rt=%0.2f r2=%0.2f PR=%0.2f sum=%0.2f g=%g e=%e",
        pa::r, pa::sr, pa::rt, pa::r2, pa::PR, pa::r + pa::PR, pa::r, pa::rt);
    $display("sns pa-typedef tr=%0.2f tsr=%0.2f tr2=%0.2f", pa::tr, pa::tsr, pa::tr2);
    $display("sns pb-more carr=%0d,%0d cr=%0.2f", pb::carr[0], pb::carr[1], pb::cr);
    $display("sns pb c=%0d cdyn=%0d,%0d cs=%s viaf=%0d m=%0d",
        pb::c, pb::cdyn[0], pb::cdyn[1], pb::cs, pb::viaf, m);
  end
endmodule
