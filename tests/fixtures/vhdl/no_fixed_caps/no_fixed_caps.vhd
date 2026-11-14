-- SPDX-License-Identifier: Apache-2.0
--
-- No fixed size caps in elaboration: an enumeration type of 91 character
-- literals is folded in generate conditions and generic actuals like a small one
-- (IEEE 1076-2019 5.2.2, 11.8, 14.4.3), and a chain of 100 subtypes of a type
-- is followed to its base (6.3).
entity leaf is
  generic (g : character);
end entity;

architecture a of leaf is
begin
  l1 : if g = 'Z' generate
    process begin report "nocap leaf Z"; wait; end process;
  end generate;
  l2 : if g = '~' generate
    process begin report "nocap leaf tilde"; wait; end process;
  end generate;
end architecture;

entity top is
end entity;

architecture a of top is
  type big is ('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '!', '#', '$', '%', '&', '(', ')', '*', '+', ',', '-', '.', '/', ':', ';', '<', '=', '>', '?', '@', '[', ']', '^', '_', '`', '{', '|', '}', '~');
  subtype c000 is big;
  subtype c001 is c000;
  subtype c002 is c001;
  subtype c003 is c002;
  subtype c004 is c003;
  subtype c005 is c004;
  subtype c006 is c005;
  subtype c007 is c006;
  subtype c008 is c007;
  subtype c009 is c008;
  subtype c010 is c009;
  subtype c011 is c010;
  subtype c012 is c011;
  subtype c013 is c012;
  subtype c014 is c013;
  subtype c015 is c014;
  subtype c016 is c015;
  subtype c017 is c016;
  subtype c018 is c017;
  subtype c019 is c018;
  subtype c020 is c019;
  subtype c021 is c020;
  subtype c022 is c021;
  subtype c023 is c022;
  subtype c024 is c023;
  subtype c025 is c024;
  subtype c026 is c025;
  subtype c027 is c026;
  subtype c028 is c027;
  subtype c029 is c028;
  subtype c030 is c029;
  subtype c031 is c030;
  subtype c032 is c031;
  subtype c033 is c032;
  subtype c034 is c033;
  subtype c035 is c034;
  subtype c036 is c035;
  subtype c037 is c036;
  subtype c038 is c037;
  subtype c039 is c038;
  subtype c040 is c039;
  subtype c041 is c040;
  subtype c042 is c041;
  subtype c043 is c042;
  subtype c044 is c043;
  subtype c045 is c044;
  subtype c046 is c045;
  subtype c047 is c046;
  subtype c048 is c047;
  subtype c049 is c048;
  subtype c050 is c049;
  subtype c051 is c050;
  subtype c052 is c051;
  subtype c053 is c052;
  subtype c054 is c053;
  subtype c055 is c054;
  subtype c056 is c055;
  subtype c057 is c056;
  subtype c058 is c057;
  subtype c059 is c058;
  subtype c060 is c059;
  subtype c061 is c060;
  subtype c062 is c061;
  subtype c063 is c062;
  subtype c064 is c063;
  subtype c065 is c064;
  subtype c066 is c065;
  subtype c067 is c066;
  subtype c068 is c067;
  subtype c069 is c068;
  subtype c070 is c069;
  subtype c071 is c070;
  subtype c072 is c071;
  subtype c073 is c072;
  subtype c074 is c073;
  subtype c075 is c074;
  subtype c076 is c075;
  subtype c077 is c076;
  subtype c078 is c077;
  subtype c079 is c078;
  subtype c080 is c079;
  subtype c081 is c080;
  subtype c082 is c081;
  subtype c083 is c082;
  subtype c084 is c083;
  subtype c085 is c084;
  subtype c086 is c085;
  subtype c087 is c086;
  subtype c088 is c087;
  subtype c089 is c088;
  subtype c090 is c089;
  subtype c091 is c090;
  subtype c092 is c091;
  subtype c093 is c092;
  subtype c094 is c093;
  subtype c095 is c094;
  subtype c096 is c095;
  subtype c097 is c096;
  subtype c098 is c097;
  subtype c099 is c098;
  constant k : c099 := '~';
  constant z : c099 := 'Z';
begin
  u1 : entity work.leaf generic map (g => 'Z');
  u2 : entity work.leaf generic map (g => '~');
  g1 : if k = '~' generate
    process begin report "nocap top tilde"; wait; end process;
  end generate;
  g2 : if z = 'Z' and z /= 'z' generate
    process begin report "nocap top Z"; wait; end process;
  end generate;
  g3 : if c099'pos(k) = big'pos('~') and big'pos('~') > 64 generate
    process begin report "nocap pos above 64"; wait; end process;
  end generate;
end architecture;
