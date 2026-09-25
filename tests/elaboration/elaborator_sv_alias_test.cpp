// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include "fsim/frontend/parser.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace fsim::tests::elaboration {

void test_systemverilog_aliases()
{
    const auto parsed = fsim::frontend::parse_text(
        "systemverilog-alias.sv",
        R"(
module systemverilog_alias;
  wire [7:0] first;
  wire [7:0] second;
  wire [7:0] third;
  wire [7:0] upper;
  wire [7:0] lower;
  wire [3:0] shuffled;
  alias first = second;
  alias second = third;
  alias upper[7:4] = lower[3:0];
  alias {upper[1:0], upper[3:2]} = shuffled;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "sv:work.systemverilog_alias");
    assert(elaborated.ok());
    const auto first = elaborated.design->find_signal(
        "systemverilog_alias.first");
    const auto second = elaborated.design->find_signal(
        "systemverilog_alias.second");
    const auto third = elaborated.design->find_signal(
        "systemverilog_alias.third");
    assert(first && second && third);
    assert(first == second && second == third);
    assert(elaborated.design->signals().size() == 4);
    const auto upper = elaborated.design->find_signal(
        "systemverilog_alias.upper");
    const auto lower = elaborated.design->find_signal(
        "systemverilog_alias.lower");
    const auto shuffled = elaborated.design->find_signal(
        "systemverilog_alias.shuffled");
    assert(upper && lower && shuffled);
    std::vector<fsim::runtime::simir::Process> aliases;
    for (const auto& process : elaborated.design->processes()) {
        if (process.switch_bidirectional) {
            aliases.push_back(process);
        }
    }
    assert(aliases.size() == 3);
    assert(aliases[0].switch_source == upper);
    assert(aliases[0].switch_target == lower);
    assert(aliases[0].switch_source_offset == 4);
    assert(aliases[0].switch_target_offset == 0);
    assert(aliases[0].switch_width == 4);
    assert(aliases[1].switch_source == upper);
    assert(aliases[1].switch_target == shuffled);
    assert(aliases[1].switch_source_offset == 2);
    assert(aliases[1].switch_target_offset == 0);
    assert(aliases[1].switch_width == 2);
    assert(aliases[2].switch_source == upper);
    assert(aliases[2].switch_target == shuffled);
    assert(aliases[2].switch_source_offset == 0);
    assert(aliases[2].switch_target_offset == 2);
    assert(aliases[2].switch_width == 2);

    const auto& paths = elaborated.design->hierarchy_paths();
    const auto first_path = paths.find("systemverilog_alias.first");
    assert(first_path);
    assert(paths.view(*first_path) == "systemverilog_alias.first");
    for (std::size_t index = 1; index < paths.size(); ++index) {
        const auto previous = fsim::semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index - 1));
        const auto current = fsim::semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        assert(paths.view(previous) < paths.view(current));
    }

    auto copied = *elaborated.design;
    auto restored = fsim::elaboration::ElaboratedDesign::from_state(
        std::move(copied).state());
    assert(restored);
    assert(restored->find_signal("systemverilog_alias.first") == first);
    assert(restored->hierarchy_paths().view(*first_path)
        == paths.view(*first_path));
    assert(restored->signal_paths() == elaborated.design->signal_paths());

    auto remapped_design = *restored;
    fsim::semantic::HierarchyPathTable::Builder missing_path;
    fsim::semantic::HierarchyPathTable::Builder interleaved;
    (void)missing_path.intern("aaa");
    (void)interleaved.intern("aaa");
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto id = fsim::semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        if (id != *first_path) {
            (void)missing_path.intern(paths.view(id));
        }
        (void)interleaved.intern(paths.view(id));
    }
    assert(!remapped_design.remap_path_table(
        std::move(missing_path).freeze()));
    assert(remapped_design.hierarchy_paths().find(
               "systemverilog_alias.first") == first_path);
    assert(remapped_design.find_signal(
               "systemverilog_alias.first") == first);
    assert(remapped_design.remap_path_table(
        std::move(interleaved).freeze()));
    assert(remapped_design.hierarchy_paths().find(
               "systemverilog_alias.first") != first_path);
    assert(remapped_design.find_signal(
               "systemverilog_alias.first") == first);
    assert(remapped_design.signal_paths()
        == elaborated.design->signal_paths());

    fsim::semantic::HierarchyPathTable::Builder changed_prefix;
    fsim::semantic::HierarchyPathTable::Builder extended;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto id = fsim::semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        (void)changed_prefix.intern(
            index == 0 ? "different_root" : paths.view(id));
        (void)extended.intern(paths.view(id));
    }
    (void)extended.intern("systemverilog_alias.derived");
    assert(!restored->rebind_path_table(
        std::move(changed_prefix).freeze()));
    assert(restored->rebind_path_table(std::move(extended).freeze()));
    assert(restored->hierarchy_paths().find("systemverilog_alias.first")
        == first_path);
    assert(restored->find_signal("systemverilog_alias.first") == first);

    const auto invalid = fsim::frontend::parse_text(
        "systemverilog-alias-invalid.sv",
        R"(
module systemverilog_alias_mismatch;
  wire [7:0] wide;
  wire [3:0] narrow;
  alias wide = narrow;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid.ok());
    const auto mismatch = compile_and_elaborate(
        invalid.design, "sv:work.systemverilog_alias_mismatch");
    assert(!mismatch.ok());
    assert(has_diagnostic(mismatch, "FSIM-ELAB-SVALIAS-003"));

    const auto variable = fsim::frontend::parse_text(
        "systemverilog-alias-variable.sv",
        R"(
module systemverilog_alias_variable;
  logic [7:0] left;
  logic [7:0] right;
  alias left = right;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(variable.ok());
    const auto invalid_variable = compile_and_elaborate(
        variable.design, "sv:work.systemverilog_alias_variable");
    assert(!invalid_variable.ok());
    assert(has_diagnostic(
        invalid_variable, "FSIM-ELAB-SVALIAS-001"));
}

} // namespace fsim::tests::elaboration
