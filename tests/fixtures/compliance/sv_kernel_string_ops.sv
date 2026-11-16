// fsim: top=sv_kernel_string_ops std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.16, 11.4.13: string variables of automatic tasks, held
// by the compiled kernel. Assignment, literals, concatenation (into a new
// string, appended to its first operand, and with the target as a later
// operand), equality, len(), indexing (out of range and unknown indices
// read 0), byte replacement (out of range and zero bytes leave the string
// unchanged), and strings kept across a suspension. 6.16.1-6.16.12: the
// string methods getc, putc, toupper, tolower, compare, icompare, substr,
// atoi, atohex, atooct, atobin, itoa, hextoa, octtoa and bintoa, with
// unknown and out-of-range arguments.
module sv_kernel_string_ops;
  int failures;

  task automatic check(input string what, input string got, input string want);
    if (got != want) begin
      $display("FAIL %s: got \"%s\" want \"%s\"", what, got, want);
      failures++;
    end
  endtask

  task automatic check_int(input string what, input longint got, input longint want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  task automatic strings();
    string a;
    string b;
    string c;
    string e;
    integer unknown;
    int sum;
    a = "hello";
    b = a;
    c = {a, " ", b};
    check("concatenation", c, "hello hello");
    check_int("length", c.len(), 11);
    a = {a, a};
    check("appended to itself", a, "hellohello");
    b = {"<", b, ">", b};
    check("target as a later operand", b, "<hello>hello");
    check_int("equal", a == "hellohello", 1);
    check_int("not equal", a != c, 1);
    e = "";
    check_int("empty", e == "", 1);
    for (int i = 0; i < 40; i++) e = {e, "x"};
    check_int("grown", e.len(), 40);
    sum = 0;
    for (int i = 0; i < c.len(); i++) sum += c[i];
    check_int("bytes", sum, 1096);
    check_int("negative index", c[-1], 0);
    check_int("index past the end", c[99], 0);
    unknown = 'x;
    check_int("unknown index", c[unknown], 0);
    c[0] = "J";
    c[1] = 0;
    c[50] = "Q";
    check("replaced byte", c, "Jello hello");
    #10;
    check("kept across a wait", c, "Jello hello");
    check("kept appended", a, "hellohello");
    check_int("kept length", e.len(), 40);
  endtask

  task automatic methods();
    string s;
    string t;
    string n;
    integer unknown;
    int value;
    unknown = 'x;
    s = "Hello_World 123";
    check_int("getc", s.getc(0), 72);
    check_int("getc negative", s.getc(-1), 0);
    check_int("getc past the end", s.getc(15), 0);
    check_int("getc unknown", s.getc(unknown), 0);
    check("toupper", s.toupper(), "HELLO_WORLD 123");
    check("tolower", s.tolower(), "hello_world 123");
    t = s;
    t = t.toupper();
    check("toupper in place", t, "HELLO_WORLD 123");
    check_int("compare after", s.compare("Hello"), 1);
    check_int("compare equal", s.compare("Hello_World 123"), 0);
    check_int("compare before", s.compare("Hellp"), -1);
    check_int("compare shorter", s.compare("Hello_World 1234"), -1);
    check_int("icompare equal", s.icompare("HELLO_WORLD 123"), 0);
    check_int("icompare folds to lower case", s.icompare("hello^"), 1);
    check("substr", s.substr(6, 14), "World 123");
    check("substr reversed", s.substr(3, 2), "");
    check("substr negative", s.substr(-1, 2), "");
    check("substr past the end", s.substr(5, 15), "");
    check("substr unknown", s.substr(unknown, 3), "");
    t = s;
    t = t.substr(6, 10);
    check("substr in place", t, "World");
    n = "-1_234xyz";
    value = n.atoi();
    check_int("atoi", value, -1234);
    n = "  42";
    value = n.atoi();
    check_int("atoi leading space", value, 0);
    n = "99999999999";
    value = n.atoi();
    check_int("atoi wraps", value, 1215752191);
    n = "ff_FFg";
    value = n.atohex();
    check_int("atohex", value, 65535);
    n = "777";
    value = n.atooct();
    check_int("atooct", value, 511);
    n = "1012";
    value = n.atobin();
    check_int("atobin", value, 5);
    value = -2147483648;
    n.itoa(value);
    check("itoa", n, "-2147483648");
    value = -255;
    n.hextoa(value);
    check("hextoa", n, "ffffff01");
    value = 8;
    n.octtoa(value);
    check("octtoa", n, "10");
    value = 5;
    n.bintoa(value);
    check("bintoa", n, "101");
    n.itoa(unknown);
    check("itoa unknown", n, "0");
    t = "abc";
    t.putc(1, "Z");
    t.putc(5, "Q");
    t.putc(-1, "Q");
    t.putc(0, 0);
    t.putc(2, unknown);
    t.putc(unknown, "Q");
    check("putc", t, "aZc");
    #10;
    check("kept after a wait", t, "aZc");
  endtask

  initial begin
    strings();
    methods();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
