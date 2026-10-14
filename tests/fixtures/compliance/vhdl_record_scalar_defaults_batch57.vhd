-- fsim: top=vhdl_record_scalar_defaults_batch57(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_record_scalar_defaults_batch57 is end;
architecture sim of vhdl_record_scalar_defaults_batch57 is
  type month_t is (jan, feb);
  type date_t is record
    day : integer range 0 to 31;
    month : month_t;
    year : integer range 4000 downto 0;
  end record;
  type dates_t is array (1 to 2) of date_t;
  signal value : date_t;
  signal dates : dates_t;
begin
  process
    variable local_value : date_t;
    variable local_dates : dates_t;
  begin
    assert value.day = 0 and value.month = jan and value.year = 4000
      report "FAIL signal record scalar defaults" severity failure;
    assert local_value.day = 0 and local_value.month = jan
      and local_value.year = 4000
      report "FAIL local record scalar defaults" severity failure;
    for index in dates'range loop
      assert dates(index).day = 0 and dates(index).year = 4000
        report "FAIL signal record array scalar defaults" severity failure;
      assert local_dates(index).day = 0 and local_dates(index).year = 4000
        report "FAIL local record array scalar defaults" severity failure;
    end loop;
    report "PASS";
    wait;
  end process;
end;
