-- fsim: top=d8_file_open_literals_2019(test) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 16.3, 5.5.2: FILE_OPEN_KIND has READ_WRITE_MODE and
-- FILE_OPEN_STATE has STATE_OPEN and STATE_CLOSED (FILE_STATE, FILE_MODE).
entity d8_file_open_literals_2019 is
end entity;

architecture test of d8_file_open_literals_2019 is
    type char_file is file of character;
    file f : char_file;
begin
    p1 : process is
        variable c : character;
        variable ok : boolean := true;
    begin
        file_open(f, "d8_file_open_literals_2019.txt", WRITE_MODE);
        write(f, 'x');
        file_close(f);
        ok := ok and file_state(f) = STATE_CLOSED;
        ok := ok and file_open(f, "d8_file_open_literals_2019.txt",
                               READ_WRITE_MODE) = OPEN_OK;
        ok := ok and file_state(f) = STATE_OPEN;
        ok := ok and file_mode(f) = READ_WRITE_MODE;
        read(f, c);
        ok := ok and c = 'x';
        file_close(f);
        assert ok severity failure;
        if ok then
            report "PASS";
        end if;
        wait;
    end process;
end architecture;
