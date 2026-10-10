-- fsim: top=constant_bounds_and_file_images(sim) std=1993
-- IEEE 1076-1993 3.2.1.1 and 4.3.1.1: an unconstrained constant takes its
-- index range from its value; 14.1: the bounds of a floating subtype; 4.3.1.2:
-- signal initial values of record and TIME types; 3.3: array attributes of
-- an access value's designated object; 3.4.1: typed files of records, arrays
-- and strings written and read back, including READ with a LENGTH.
entity constant_bounds_and_file_images is
end entity;

architecture sim of constant_bounds_and_file_images is
  constant bits : bit_vector := "0101";
  constant text : string := "abc";
  subtype unit_interval is real range 0.0 to 1.0;
  subtype falling is real range 20.0 downto 0.0;
  type entry is record
    count : integer;
    ratio : real;
    mark  : character;
    level : severity_level;
    delay : time;
  end record;
  constant seed : entry := (7, 0.5, ' ', warning, 3 ns);
  signal current : entry := seed;
  signal earlier : time := -5 ns;
  type entry_file is file of entry;
  type integer_list is array (natural range <>) of integer;
  type list_file is file of integer_list;
  subtype word is string(1 to 5);
  type word_file is file of word;
  type cells is array (1 to 6) of integer;
  type cells_access is access cells;
begin
  process
    file entries : entry_file;
    file lists : list_file;
    file words : word_file;
    variable e : entry;
    variable l : integer_list(0 to 7);
    variable n : natural;
    variable w : word;
    variable head : cells_access := new cells;
    variable failures : natural := 0;
  begin
    if bits'left /= 0 or bits'right /= 3 or not bits'ascending
      or bits'length /= 4 then
      failures := failures + 1;
    end if;
    if text'left /= 1 or text'right /= 3 then failures := failures + 1; end if;
    if unit_interval'high /= 1.0 or falling'left /= 20.0
      or falling'low /= 0.0 then
      failures := failures + 1;
    end if;
    if current.count /= 7 or current.ratio /= 0.5 or current.mark /= ' '
      or current.level /= warning or current.delay /= 3 ns then
      failures := failures + 1;
    end if;
    if earlier /= -5 ns then failures := failures + 1; end if;
    if head'low /= 1 or head'length /= 6 then failures := failures + 1; end if;

    file_open(entries, "entries.dat", write_mode);
    write(entries, seed);
    write(entries, (count => -12, ratio => 2.25, mark => 'q',
                    level => failure, delay => 2 us));
    file_close(entries);
    file_open(entries, "entries.dat", read_mode);
    read(entries, e);
    if e /= seed then failures := failures + 1; end if;
    read(entries, e);
    if e.count /= -12 or e.ratio /= 2.25 or e.mark /= 'q'
      or e.level /= failure or e.delay /= 2 us then
      failures := failures + 1;
    end if;
    if not endfile(entries) then failures := failures + 1; end if;
    file_close(entries);

    file_open(lists, "lists.dat", write_mode);
    write(lists, (4, -3, 2));
    file_close(lists);
    file_open(lists, "lists.dat", read_mode);
    read(lists, l, n);
    if n /= 3 or l(0) /= 4 or l(1) /= -3 or l(2) /= 2 then
      failures := failures + 1;
    end if;
    file_close(lists);

    file_open(words, "words.dat", write_mode);
    write(words, "hello");
    file_close(words);
    file_open(words, "words.dat", read_mode);
    read(words, w);
    if w /= "hello" then failures := failures + 1; end if;
    file_close(words);

    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
