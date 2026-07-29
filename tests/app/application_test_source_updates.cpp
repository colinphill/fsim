// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <fstream>
#include <string_view>

namespace fsim::test {

void ApplicationTestFixture::write_provenance_source(
    const std::string_view comment) {
  std::ofstream output(provenance_source);
  output << R"(
module provenance;
  logic q;
  initial begin
    q = 1'b1;
    #1 $finish;
  end
endmodule
)";
  output << "// " << comment << '\n';
}

void ApplicationTestFixture::write_unused_source(
    const std::string_view comment) {
  std::ofstream output(unused_source);
  output << R"(
module unused;
  logic q;
  initial q = 1'b0;
endmodule
)";
  output << "// " << comment << '\n';
}

void ApplicationTestFixture::write_parameter_top(
    const std::uint64_t narrow_value) {
  std::ofstream output(parameter_top_source);
  output
      << "module parameter_top;\n"
      << "  logic [3:0] narrow;\n"
      << "  logic [7:0] wide;\n"
      << "  parameter_child #(.WIDTH(4), .VALUE("
      << narrow_value
      << ")) u_narrow(.q(narrow));\n"
      << "  parameter_child #(8, 3) u_wide(.q(wide));\n"
      << "  initial #1 $finish;\n"
      << "endmodule\n";
}

void ApplicationTestFixture::write_vhdl_generic_entity(
    const std::string_view revision) {
  std::ofstream output(vhdl_generic_entity_source);
  output << R"(
entity vhdl_generic_child is
  generic (
    width : positive := 1;
    value : natural := 1;
    last : integer := width - 1
  );
  port (
    q : out unsigned(last downto 0)
  );
end entity;
)";
  output << "-- " << revision << '\n';
}

void ApplicationTestFixture::write_vhdl_generic_top(
    const std::uint64_t narrow_value) {
  std::ofstream output(vhdl_generic_top_source);
  output << R"(
entity vhdl_generic_top is
end entity;

architecture rtl of vhdl_generic_top is
  signal narrow : unsigned(3 downto 0);
  signal wide : unsigned(7 downto 0);
begin
  narrow_child: entity work.vhdl_generic_child(rtl)
    generic map (
      width => 4,
      value => )"
         << narrow_value << R"(
    )
    port map (
      q => narrow
    );
  wide_child: entity work.vhdl_generic_child(rtl)
    generic map (
      8,
      value => 3
    )
    port map (
      q => wide
    );
  stopper: process
  begin
    wait for 1 ns;
  end process;
end architecture;
)";
}

}  // namespace fsim::test
