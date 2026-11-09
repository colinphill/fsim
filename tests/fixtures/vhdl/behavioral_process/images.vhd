-- SPDX-License-Identifier: Apache-2.0
--
-- String operations in VHDL behavioral processes (IEEE 1076-2019 5.2.4.2,
-- 16.2.2, 9.2.5): 'IMAGE of an integer, string concatenation and TIME
-- arithmetic in report messages, compiled in the kernel's threads (the
-- string registers are the thread's, the operations the reference's). The
-- expected lines (images.expected) are derived from the LRM by hand: a
-- changes at 10 and 15 ns, the ticker resumes every 7 ns.
entity images is
end entity images;

architecture tb of images is
  signal a : integer := 0;
begin
  drv : process
  begin
    wait for 10 ns;
    a <= 1;
    wait for 5 ns;
    a <= 2;
    wait;
  end process drv;

  mon : process
  begin
    loop
      wait on a;
      report "a=" & integer'image(a) & " @" & integer'image(now / 1 ns) & " ns";
    end loop;
  end process mon;

  tick : process
  begin
    for i in 1 to 3 loop
      wait for 7 ns;
      report "tick" & integer'image(i) & " @" & integer'image(now / 1 ns) & " ns";
    end loop;
    wait;
  end process tick;
end architecture tb;
