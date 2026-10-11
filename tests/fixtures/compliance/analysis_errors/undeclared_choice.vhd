-- fsim-expect: FSIM-ELAB-VHNAME-002 std=1993
-- IEEE 1076-2008 10.9: a case choice is a value of the selector's type.
entity undeclared_choice is
end entity;

architecture a of undeclared_choice is
  type day is (sun, mon);
  signal s_day : day := sun;
  signal j : integer;
begin
  with s_day select
    j <= 1 when sun,
         0 when element,
         3 when others;
end architecture;
