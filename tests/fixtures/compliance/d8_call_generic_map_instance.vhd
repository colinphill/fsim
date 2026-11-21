-- fsim: top=d8_call_generic_map_instance(a) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.4: a call with a generic map stands for a call of an
-- implicit instance of the template. The instance is declared where the
-- statement or declaration holding the call begins, so the call itself,
-- and a subtype indication of the same declaration, see it (12.3).
entity d8_call_generic_map_instance is
end entity;

architecture a of d8_call_generic_map_instance is
  function reverse generic (type t is array (type is (<>)) of type is private)
      parameter (x : t) return t is
    subtype idx is x'index;
    variable rv    : t := x;
    variable inidx : idx := x'right;
  begin
    if x'length < 2 then
      return rv;
    end if;
    for outidx in x'range loop
      rv(inidx) := x(outidx);
      if inidx /= x'left then
        if x'ascending then
          inidx := idx'pred(inidx);
        else
          inidx := idx'succ(inidx);
        end if;
      end if;
    end loop;
    return rv;
  end function;
begin
  process
    variable x : integer_vector := (1, 2, 3);
    variable y : integer_vector := reverse generic map (x'subtype) (x);
  begin
    assert y(0) = 3 and y(1) = 2 and y(2) = 1 severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
