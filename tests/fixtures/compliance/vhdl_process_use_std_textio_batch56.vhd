-- fsim: top=process_use_std_textio_batch56(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 12.4: a process-local use clause makes std.textio visible.
entity process_use_std_textio_batch56 is
end entity;

architecture sim of process_use_std_textio_batch56 is
begin
  writer : process
    use std.textio;
    variable line_buffer : textio.line;
  begin
    textio.write(line_buffer, string'("PASS"));
    textio.writeline(textio.output, line_buffer);
    wait;
  end process;
end architecture;
