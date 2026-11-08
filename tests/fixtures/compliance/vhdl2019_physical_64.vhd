-- fsim: top=physical_64_2019(sim) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.2.3.1: INTEGER covers -(2**63) to (2**63)-1 at least;
-- 5.2.4.1: a physical type's range may be any range within those bounds.
entity physical_64_2019 is
end entity;

architecture sim of physical_64_2019 is
  type distance is range -(2**62) to 2**62
    units
      um;
      mm = 1000 um;
      km = 1000000 mm;
    end units;
  constant far : distance := 4000000 km;
  signal s : distance := 0 um;
begin
  check : process
    variable failures : natural := 0;
  begin
    s <= far + 1 um;
    wait for 1 ns;
    if s /= 4000000000000001 um then
      failures := failures + 1;
    end if;
    if distance'pos(distance'high) /= 2**62 then
      failures := failures + 1;
    end if;
    if far / 1 um /= 4000000000000000 then
      failures := failures + 1;
    end if;
    if integer'high /= 9223372036854775807 then
      failures := failures + 1;
    end if;
    if distance'image(far) /= "4000000000000000 um" then
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
