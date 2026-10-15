-- fsim: top=vhdl_block_nested_record_input_constraints_batch59(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
package b59_block_nested_record_pkg is
  type address_channel_t is record
    valid   : bit;
    address : bit_vector;
  end record;

  type write_channel_t is record
    valid : bit;
    data  : bit_vector;
  end record;

  type bus_t is record
    address_write_ch : address_channel_t;
    write_ch         : write_channel_t;
    direct_payload   : bit_vector;
  end record;

  subtype constrained_bus_t is bus_t(
    address_write_ch(address(7 downto 0)),
    write_ch(data(0 to 15)),
    direct_payload(3 downto 0)
  );
  subtype reindexed_bus_t is bus_t(
    address_write_ch(address(0 to 7)),
    write_ch(data(15 downto 0)),
    direct_payload(0 to 3)
  );
  -- Subtype chains must carry nested record-element constraints through.
  subtype constrained_alias_bus_t is constrained_bus_t;
  subtype reindexed_bus_alias_t is reindexed_bus_t;
  subtype reindexed_bus_alias2_t is reindexed_bus_alias_t;
  constant package_payload : bit_vector(5 downto 2) := "1010";
end package;

package b59_block_identity_control_pkg is
  -- Keep a same-named object in another visible package to prove that
  -- qualified attribute prefixes retain their exact package declaration.
  constant package_payload : bit_vector(0 to 2) := "101";
end package;

use work.b59_block_nested_record_pkg.all;
entity vhdl_block_nested_record_input_constraints_batch59 is
end entity;

architecture test of vhdl_block_nested_record_input_constraints_batch59 is
  signal source_bus : constrained_bus_t := (
    address_write_ch => (valid => '1', address => x"A5"),
    write_ch => (valid => '1', data => x"CAFE"),
    direct_payload => x"A"
  );
  signal reindexed_source : reindexed_bus_alias2_t := (
    address_write_ch => (valid => '1', address => x"A5"),
    write_ch => (valid => '1', data => x"CAFE"),
    direct_payload => x"A"
  );
  alias aliased_source_bus : constrained_bus_t is source_bus;
begin
  b : block
    port (bus_master_p : in bus_t);
    port map (
      bus_master_p.address_write_ch => source_bus.address_write_ch,
      bus_master_p.write_ch         => source_bus.write_ch,
      bus_master_p.direct_payload   => source_bus.direct_payload
    );
  begin
    process
    begin
      wait for 1 ns;
      assert bus_master_p.address_write_ch.address'length = 8
        report "nested address field lost its actual constraint"
        severity failure;
      assert bus_master_p.write_ch.data'length = 16
        report "nested data field lost its actual constraint"
        severity failure;
      assert bus_master_p.address_write_ch.address'left = 7
        and bus_master_p.address_write_ch.address'right = 0
        and not bus_master_p.address_write_ch.address'ascending
        report "nested address field lost its descending bounds"
        severity failure;
      assert bus_master_p.write_ch.data'left = 0
        and bus_master_p.write_ch.data'right = 15
        and bus_master_p.write_ch.data'ascending
        report "nested data field lost its ascending bounds"
        severity failure;
      assert bus_master_p.direct_payload'length = 4
        and bus_master_p.direct_payload'left = 3
        and bus_master_p.direct_payload'right = 0
        and not bus_master_p.direct_payload'ascending
        report "direct array field lost its actual bounds"
        severity failure;
      assert bus_master_p.address_write_ch.valid = '1'
        and bus_master_p.address_write_ch.address = x"A5"
        report "nested address field did not receive its actual value"
        severity failure;
      assert bus_master_p.write_ch.valid = '1'
        and bus_master_p.write_ch.data = x"CAFE"
        report "nested write field did not receive its actual value"
        severity failure;
      assert bus_master_p.direct_payload = x"A"
        report "direct array field did not receive its actual value"
        severity failure;
      assert b59_block_nested_record_pkg.package_payload'length = 4
        and b59_block_nested_record_pkg.package_payload'left = 5
        and b59_block_nested_record_pkg.package_payload'right = 2
        report "package-qualified object subtype was not retained"
        severity failure;
      assert work.b59_block_identity_control_pkg.package_payload'length = 3
        and work.b59_block_identity_control_pkg.package_payload'left = 0
        and work.b59_block_identity_control_pkg.package_payload'right = 2
        report "same-named package object lost its selected identity"
        severity failure;
      report "PASS" severity note;
      wait;
    end process;
  end block;

  a : block
    port (bus_master_p : in bus_t);
    port map (
      bus_master_p.address_write_ch => aliased_source_bus.address_write_ch,
      bus_master_p.write_ch         => aliased_source_bus.write_ch,
      bus_master_p.direct_payload   => aliased_source_bus.direct_payload
    );
  begin
    process
    begin
      wait for 1 ns;
      assert bus_master_p.address_write_ch.address'left = 7
        and bus_master_p.address_write_ch.address'right = 0
        and bus_master_p.write_ch.data'left = 0
        and bus_master_p.write_ch.data'right = 15
        and bus_master_p.direct_payload'length = 4
        report "aliased object lost nested port bounds"
        severity failure;
      assert bus_master_p.address_write_ch.address = x"A5"
        and bus_master_p.write_ch.data = x"CAFE"
        and bus_master_p.direct_payload = x"A"
        report "aliased object lost nested port values"
        severity failure;
      report "PASS" severity note;
      wait;
    end process;
  end block;

  -- The formal keeps its declared bounds; equal-length actual fields match
  -- positionally even when their bounds and directions differ.
  c : block
    port (bus_master_p : in constrained_alias_bus_t);
    port map (
      bus_master_p.address_write_ch => reindexed_source.address_write_ch,
      bus_master_p.write_ch         => reindexed_source.write_ch,
      bus_master_p.direct_payload   => reindexed_source.direct_payload
    );
  begin
    process
    begin
      wait for 1 ns;
      assert bus_master_p.address_write_ch.address'left = 7
        and bus_master_p.address_write_ch.address'right = 0
        and not bus_master_p.address_write_ch.address'ascending
        report "constrained formal address range was not retained"
        severity failure;
      assert bus_master_p.write_ch.data'left = 0
        and bus_master_p.write_ch.data'right = 15
        and bus_master_p.write_ch.data'ascending
        report "constrained formal data range was not retained"
        severity failure;
      assert bus_master_p.direct_payload'left = 3
        and bus_master_p.direct_payload'right = 0
        and not bus_master_p.direct_payload'ascending
        report "constrained direct array range was not retained"
        severity failure;
      assert bus_master_p.address_write_ch.address = x"A5"
        and bus_master_p.write_ch.data = x"CAFE"
        and bus_master_p.direct_payload = x"A"
        report "shifted/reversed actuals were not positionally associated"
        severity failure;
      report "PASS" severity note;
      wait;
    end process;
  end block;
end architecture;
