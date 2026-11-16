// fsim: top=sv_mailbox_unpacked_messages std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 15.4: a mailbox holds messages of any type. Typed and
// typeless mailboxes carry unpacked structures (with string, queue and
// real members), unions, fixed-size, dynamic, queue and associative
// arrays and arrays of queues, between static and automatic variables, a
// task's output formal and a class type parameter's specialization; a
// typeless try_get or try_peek of a message whose type is not equivalent
// returns a negative value (15.4.9, 6.22.2), and blocked get and put
// calls resume with the message.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_mailbox_unpacked_messages;
  typedef struct { string s; int a; } named_t;
  typedef struct { int q[$]; byte b; } holder_t;
  typedef struct { real r; logic [3:0] l; } mixed_t;
  typedef struct { int a; byte b; } plain_t;
  typedef union { int i; bit [31:0] v; } word_t;
  typedef int fixed_t[3];
  typedef int queue_t[$];
  typedef int dynamic_t[];
  typedef string by_name_t[string];
  typedef int by_index_t[int];
  typedef int nested_t[$][$];

  class channel #(type T = int);
    mailbox #(T) box = new;
    task send(T value);
      box.put(value);
    endtask
    task receive(output T value);
      box.get(value);
    endtask
  endclass

  mailbox #(named_t) named_box = new;
  mailbox #(holder_t) holder_box = new;
  mailbox #(mixed_t) mixed_box = new;
  mailbox #(word_t) word_box = new;
  mailbox #(fixed_t) fixed_box = new;
  mailbox #(queue_t) queue_box = new;
  mailbox #(by_name_t) bounded = new(1);
  mailbox any = new;
  fixed_t fixed_static;
  plain_t plain_static;
  int failures;

  task automatic fixed_receive(output fixed_t value);
    fixed_box.get(value);
  endtask
  task automatic fixed_send(input fixed_t value);
    fixed_box.put(value);
  endtask
  task automatic plain_receive(output plain_t value);
    any.get(value);
  endtask

  initial begin
    named_t named, named_got;
    holder_t holder, holder_got;
    mixed_t mixed, mixed_got;
    word_t word, word_got;
    fixed_t fixed_got;
    queue_t queue, queue_got;
    dynamic_t dynamic, dynamic_got;
    by_name_t by_name, by_name_got;
    by_index_t by_index, by_index_got;
    nested_t nested, nested_got;
    plain_t plain_got;
    channel #(named_t) link = new;
    int got;

    named.s = "hi";
    named.a = 4;
    named_box.put(named);
    named_box.get(named_got);
    `CHECK("structure with a string", named_got.s == "hi" && named_got.a == 4)
    mixed.r = 2.5;
    mixed.l = 4'bx1z0;
    mixed_box.put(mixed);
    mixed_box.get(mixed_got);
    `CHECK("structure with a real", mixed_got.r == 2.5
      && mixed_got.l === 4'bx1z0)
    word.i = 9;
    word_box.put(word);
    word_box.get(word_got);
    `CHECK("union", word_got.i == 9)
    queue = {1, 2, 3, 4};
    queue_box.put(queue);
    queue_box.get(queue_got);
    `CHECK("queue", queue_got.size() == 4 && queue_got[3] == 4)

    fixed_static = '{1, 2, 3};
    fixed_box.put(fixed_static);
    fixed_receive(fixed_got);
    `CHECK("fixed array into an output formal", fixed_got == '{1, 2, 3})
    fixed_send(fixed_static);
    fixed_box.get(fixed_got);
    `CHECK("fixed array from an input formal", fixed_got == '{1, 2, 3})

    plain_static.a = 7;
    plain_static.b = 8;
    any.put(plain_static);
    plain_receive(plain_got);
    `CHECK("structure into an automatic formal", plain_got.a == 7
      && plain_got.b == 8)

    named.s = "k";
    link.send(named);
    link.receive(named_got);
    `CHECK("class type parameter", named_got.s == "k" && named_got.a == 4)

    any.put(named);
    got = any.try_get(holder_got);
    `CHECK("typeless structure mismatch", got < 0)
    got = any.try_peek(named_got);
    `CHECK("typeless peek", got > 0 && named_got.s == "k" && any.num() == 1)
    any.get(named_got);
    `CHECK("typeless get", named_got.a == 4 && any.num() == 0)
    queue = {5, 6};
    any.put(queue);
    got = any.try_get(dynamic_got);
    `CHECK("queue is not a dynamic array", got < 0)
    got = any.try_get(queue_got);
    `CHECK("typeless queue", got > 0 && queue_got.size() == 2
      && queue_got[1] == 6)
    dynamic = new[3];
    dynamic[2] = 8;
    any.put(dynamic);
    any.get(dynamic_got);
    `CHECK("typeless dynamic array", dynamic_got.size() == 3
      && dynamic_got[2] == 8)
    by_name["x"] = "y";
    any.put(by_name);
    got = any.try_get(by_index_got);
    `CHECK("associative index types differ", got < 0)
    any.get(by_name_got);
    `CHECK("typeless associative array", by_name_got.num() == 1
      && by_name_got["x"] == "y")
    by_index[7] = 70;
    any.put(by_index);
    any.get(by_index_got);
    `CHECK("typeless integral-keyed array", by_index_got[7] == 70)
    word.i = 11;
    any.put(word);
    got = any.try_get(plain_got);
    `CHECK("union is not a structure", got < 0)
    any.get(word_got);
    `CHECK("typeless union", word_got.i == 11)
    nested = '{'{1}, '{2, 3}};
    any.put(nested);
    any.get(nested_got);
    `CHECK("queue of queues", nested_got.size() == 2 && nested_got[1][1] == 3)

    holder.q = {1, 2, 3};
    holder.b = 9;
    fork
      begin
        holder_box.get(holder_got);
        `CHECK("blocked get", $time == 5 && holder_got.q.size() == 3
          && holder_got.b == 9)
      end
      begin
        #5 holder_box.put(holder);
      end
    join

    by_name["z"] = "w";
    bounded.put(by_name);
    got = bounded.try_put(by_name);
    `CHECK("bounded try_put", got == 0)
    fork
      begin
        #3 bounded.get(by_name_got);
        `CHECK("drain", $time == 8 && by_name_got.num() == 2)
      end
      begin
        bounded.put(by_name);
        `CHECK("blocked put", $time == 8)
      end
    join

    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
