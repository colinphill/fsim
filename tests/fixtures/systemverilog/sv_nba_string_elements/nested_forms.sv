// SPDX-License-Identifier: Apache-2.0
//
// Nonblocking assignments to string elements reached through nested
// containers, through a structure member, in a variable of a procedural block,
// and with a string key and an intra-assignment delay (IEEE 1800-2023 7.4.5,
// 7.8, 10.4.2, 6.21). The value and every index are those of the statement's
// execution; the elements change in the NBA region.
module top;
  typedef struct { string name; string items[$]; } box_t;
  string nested[$][$];
  string fx_of_q[2][$];
  box_t bx;
  box_t boxes[2];
  box_t ba[string];
  class holder;
    string items[$];
    function new();
      items.push_back("c0");
      items.push_back("c1");
    endfunction
  endclass
  holder obj;
  string as[string];

  task automatic show(string label, string sl_view);
    $display("sns %s nested=%s,%s,%s fx_of_q=%s,%s,%s sl=%s as=%s",
        label, nested[0][0], nested[0][1], nested[1][0],
        fx_of_q[0][0], fx_of_q[1][0], fx_of_q[1][1],
        sl_view,
        as.exists("k") ? as["k"] : "-");
  endtask

  initial begin
    string key;
    int i, j;
    string bq[$], b1[$];
    static string sl[2];
    nested = '{ '{"n00", "n01"}, '{"n10"} };
    fx_of_q[0] = '{"f00"};
    fx_of_q[1] = '{"f10", "f11"};
    bx.name = "box";
    bx.items.push_back("i0");
    bx.items.push_back("i1");
    boxes[1].items.push_back("k0");
    ba["k"].items.push_back("a0");
    obj = new;
    sl[0] = "s0"; sl[1] = "s1";
    as["k"] = "kay";
    show("start", {sl[0], ",", sl[1]});

    key = "k"; i = 1; j = 0;
    nested[i][j] <= "N10";
    nested[0][1] <= {"N01-", "x"};
    fx_of_q[i][i] <= "F11";
    bx.items[i] <= "I1";
    boxes[i].items[j] <= "K0";
    ba[key].items[j] <= "A0";
    sl[i] <= "S1";
    as[key] <= #2 "late";
    key = "other"; i = 0; j = 1;
    show("same-step", {sl[0], ",", sl[1]});
    #1;
    show("next-step", {sl[0], ",", sl[1]});
    bq = bx.items;
    b1 = boxes[1].items;
    $display("sns members %0d %0d %s,%s %s", bq.size(), b1.size(), bq[0], bq[1], b1[0]);
    $display("sns direct %s,%s %s %s %s", bx.items[0], bx.items[1], boxes[1].items[0],
        ba["k"].items[0], obj.items[i]);
    #2;
    show("after-delay", {sl[0], ",", sl[1]});
    $finish;
  end
endmodule
