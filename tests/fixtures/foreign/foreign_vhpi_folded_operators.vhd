-- SPDX-License-Identifier: Apache-2.0
-- An operator whose operands are literals is folded when the design is
-- built, but the static model keeps the call (IEEE 1076-2019 19.8): a
-- funcCall of the predefined operator with its operands, locally static
-- (9.4.2).
entity top is
end entity;

architecture t of top is
  signal q : integer;
  signal b : boolean;
begin
  q <= 1 + 1;
  b <= not true;
end architecture;
