-- SPDX-License-Identifier: Apache-2.0
-- vhpiGenerateIndexP of a for-generate whose range is not locally static
-- (IEEE 1076-2019 11.8, 19.4.2, Figure 38): the range `1 to n` depends on a
-- generic, so it is globally static only; the elaborated occurrences are
-- g(1), g(2) and g(3) for the generic's default, and each reports the value of
-- its generate parameter.
entity top is
  generic (n : integer := 3);
end entity;

architecture rtl of top is
  signal x : bit_vector(0 to 5);
begin
  g : for i in 1 to n generate
    signal t : bit;
  begin
    t <= '1';
    x(i) <= t;
  end generate;
end architecture;
