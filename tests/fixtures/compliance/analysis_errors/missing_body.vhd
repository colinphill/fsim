-- fsim-expect: FSIM-ELAB-VHBODY-001 std=1993
-- IEEE 1076-2008 4.2.1: a subprogram declared in an architecture has its
-- body in the same declarative part.
entity missing_body is
end entity;

architecture a of missing_body is
  function twice(value : integer) return integer;
begin
end architecture;
