-- fsim: top=typed_files(sim) std=1993
-- IEEE 1076-1993 3.4 and 14.1: files of TIME, REAL, BOOLEAN, enumeration,
-- BIT_VECTOR and record elements are written and read back unchanged.
entity typed_files is
end entity;

architecture sim of typed_files is
  type colour is (red, green, blue);
  type sample is record
    level : integer;
    flag  : boolean;
    tag   : bit_vector(3 downto 0);
  end record;
  type time_file is file of time;
  type real_file is file of real;
  type colour_file is file of colour;
  type bool_file is file of boolean;
  subtype byte is bit_vector(7 downto 0);
  type word_file is file of byte;
  type sample_file is file of sample;
begin
  process
    file times   : time_file;
    file reals   : real_file;
    file colours : colour_file;
    file bools   : bool_file;
    file words   : word_file;
    file samples : sample_file;
    variable t : time;
    variable r : real;
    variable c : colour;
    variable b : boolean;
    variable w : byte;
    variable s : sample;
    variable failures : natural := 0;
  begin
    file_open(times, "typed_times.dat", write_mode);
    write(times, 15 ns);
    write(times, 2 us);
    file_close(times);
    file_open(reals, "typed_reals.dat", write_mode);
    write(reals, 3.25);
    write(reals, -0.5);
    file_close(reals);
    file_open(colours, "typed_colours.dat", write_mode);
    write(colours, blue);
    write(colours, green);
    file_close(colours);
    file_open(bools, "typed_bools.dat", write_mode);
    write(bools, true);
    file_close(bools);
    file_open(words, "typed_words.dat", write_mode);
    write(words, x"A5");
    file_close(words);
    file_open(samples, "typed_samples.dat", write_mode);
    write(samples, (level => -7, flag => true, tag => "1001"));
    file_close(samples);

    file_open(times, "typed_times.dat", read_mode);
    read(times, t);
    if t /= 15 ns then failures := failures + 1; end if;
    read(times, t);
    if t /= 2 us then failures := failures + 1; end if;
    if not endfile(times) then failures := failures + 1; end if;
    file_close(times);
    file_open(reals, "typed_reals.dat", read_mode);
    read(reals, r);
    if r /= 3.25 then failures := failures + 1; end if;
    read(reals, r);
    if r /= -0.5 then failures := failures + 1; end if;
    file_close(reals);
    file_open(colours, "typed_colours.dat", read_mode);
    read(colours, c);
    if c /= blue then failures := failures + 1; end if;
    read(colours, c);
    if c /= green then failures := failures + 1; end if;
    file_close(colours);
    file_open(bools, "typed_bools.dat", read_mode);
    read(bools, b);
    if not b then failures := failures + 1; end if;
    file_close(bools);
    file_open(words, "typed_words.dat", read_mode);
    read(words, w);
    if w /= x"A5" then failures := failures + 1; end if;
    file_close(words);
    file_open(samples, "typed_samples.dat", read_mode);
    read(samples, s);
    if s.level /= -7 or not s.flag or s.tag /= "1001" then
      failures := failures + 1;
    end if;
    file_close(samples);

    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
