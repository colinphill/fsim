-- fsim: top=integer_vectors_2019(sim) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.3.1: INTEGER covers at least the 64-bit range, and
-- the elements of INTEGER_VECTOR (16.3) are INTEGER values.
entity integer_vectors_2019 is
end entity;

architecture sim of integer_vectors_2019 is
  constant big : integer := 2**40;
  signal pair : integer_vector(0 to 1) := (others => 0);
  signal initial : integer_vector(0 to 1);
begin
  check : process
    variable values : integer_vector(0 to 1);
    variable failures : natural := 0;
  begin
    pair <= (big, -big);
    values := (big + 1, 3);
    wait for 1 ns;
    if pair(0) /= big or pair(1) /= -big then
      failures := failures + 1;
    end if;
    if values(0) /= big + 1 or values(1) /= 3 then
      failures := failures + 1;
    end if;
    if initial(0) /= integer'low then
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
