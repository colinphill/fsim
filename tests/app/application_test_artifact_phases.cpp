// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "governed_process_limits.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/support/path.hpp"
#include "fsim/version.hpp"

#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_artifact_phase_semantics()
{
    install_governed_process_address_space_ceiling();
    static_assert(app::kRuntimeStateSchema == 48);
    static_assert(app::kSemanticStateSchema == 3);
    static_assert(app::kDesignIrStateSchema == 3);
    static_assert(app::kClassStateSchema == 10);
    static_assert(app::kSystemVerilogConstraintHirStateSchema == 6);
    static_assert(app::kSystemVerilogCoverageStateSchema == 4);
    static_assert(app::kSystemVerilogUvmStateSchema == 2);
    static_assert(app::kVhdlHirStateSchema == 1);
    const auto copy_artifact_tree = [](
                                        const std::filesystem::path& source_root,
                                        const std::filesystem::path& destination) {
        std::filesystem::create_directories(destination);
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(source_root)) {
            const auto target = destination
                / entry.path().lexically_relative(source_root);
            if (entry.is_directory()) {
                std::filesystem::create_directories(target);
            } else if (entry.is_regular_file()) {
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy_file(entry.path(), target);
            }
        }
    };
    const auto sv_source = directory / "artifact_phase.sv";
    const auto vhdl_source = directory / "artifact_phase.vhd";
    const auto sv_object = directory / "artifact-sv.fsimobj";
    const auto vhdl_object = directory / "artifact-vhdl.fsimobj";
    const auto design = directory / "artifact-mixed.fsimdesign";
    const auto trace = directory / "artifact-mixed.vcd";
    {
        std::ofstream output(sv_source);
        output << R"(
package artifact_class_pkg;
  class process_box;
    process current;
    extern function bit same_process(process observed);
  endclass

  function bit process_box::same_process(process observed);
    return observed == current;
  endfunction
endpackage

module phase_child #(
  parameter logic [7:0] MASK = 8'hff
) (
  input logic clk,
  input logic [7:0] value,
  output logic [7:0] transformed
);
  reg notifier;
  specify
    (value => transformed) = 0;
    $setup(posedge value[0], posedge clk, 0, notifier);
  endspecify
  assign transformed = value ^ MASK;
endmodule

module phase_tb;
  logic clk;
  logic reset;
  logic [7:0] counter_q;
  logic [7:0] transformed;
  phase_counter counter (
    .clk(clk), .reset(reset), .q(counter_q));
  phase_child #(.MASK(8'h0f)) child (
    .clk(clk), .value(counter_q), .transformed(transformed));
  initial begin
    clk = 1'b0;
    reset = 1'b1;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 reset = 1'b0;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 $finish;
  end
endmodule

module phase_watch;
  logic watched;
  initial begin
    watched = 1'b0;
    #2 watched = 1'b1;
  end
endmodule

module scalar_artifact;
  localparam logic signed [136:0] WIDE_SEED =
      {1'b1, 62'b0, 1'bx, 69'b0, 4'b10z0};
  typedef enum logic [136:0] {
    WIDE_ZERO = 137'b0,
    WIDE_ENUM = {1'b1, 62'b0, 1'b1, 69'b0, 4'b1000}
  } wide_enum_t;
  typedef struct packed {
    logic [72:0] high = {1'b1, 71'b0, 1'b1};
    logic [63:0] low = 64'h8;
  } initialized_wide_t;
  typedef union tagged packed {
    logic [135:0] wide;
    logic [7:0] narrow;
  } tagged_wide_t;
  real r;
  shortreal s;
  realtime rt;
  time ticks;
  chandle handle;
  logic signed [136:0] wide_value;
  logic signed [136:0] wide_shift;
  wide_enum_t wide_enum;
  initialized_wide_t initialized_wide;
  tagged_wide_t tagged_wide;
  logic [5:0] checks;
  class ArtifactCoverageOwner;
    covergroup artifact_coverage with function sample(
      input logic [7:0] sample_value
    );
      option.goal = 75;
      type_option.merge_instances = 1;
      value_point: coverpoint sample_value {
        bins low[] = {[0:3]} with (item <= 3);
        bins path = (1 => 2);
        illegal_bins rejected = {8};
      }
    endgroup : artifact_coverage
  endclass : ArtifactCoverageOwner
  assign wide_value = WIDE_SEED;
  initial begin
    r = 1.25;
    s = -2.5;
    rt = 3.75;
    ticks = 64'd9007199254740993;
    handle = null;
    wide_shift = WIDE_SEED >>> 136;
    checks[0] = r == 1.25;
    checks[1] = s == -2.5;
    checks[2] = rt == 3.75;
    checks[3] = ticks == 64'd9007199254740993;
    checks[4] = handle == null;
    checks[5] = wide_shift == {137{1'b1}};
  end
endmodule

interface artifact_if #(parameter int WIDTH = 4);
  logic clock;
  logic [WIDTH-1:0] data;
  initial begin
    clock = 1'b0;
    data = 4'ha;
    #1 clock = 1'b1;
  end
  clocking cb @(posedge clock);
    input #0 data;
  endclocking
  modport view(input data, clocking cb);
endinterface

module artifact_virtual_leaf(artifact_if.view bus);
  virtual artifact_if #(.WIDTH(4)).view selected = bus;
endmodule

program interface_artifact;
  artifact_if #(.WIDTH(4)) link();
  artifact_virtual_leaf leaf(link);
  virtual artifact_if #(.WIDTH(4)).view selected = link;
endprogram
)";
    }
    {
        std::ofstream output(vhdl_source);
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.std_logic_unsigned.all;

package phase_values is
  constant reset_value : integer := 0;
end package;

context phase_context is
  library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;
  use ieee.std_logic_unsigned.all;
  use ieee.vital_timing.all;
  use ieee.vital_primitives.all;
  library work;
  use work.phase_values.all;
end context;

context work.phase_context;
entity phase_counter is
  generic (step : natural := 1);
  port (
    clk : in std_logic;
    reset : in std_logic;
    q : out unsigned(7 downto 0));
end entity;

context work.phase_context;
library ieee;
use ieee.std_logic_unsigned.all;
architecture rtl of phase_counter is
  signal attribute_source : std_logic;
  signal stable_probe : boolean;
  signal vital_probe : std_logic;
  signal wide_source : std_logic_vector(136 downto 0);
  signal wide_result : std_logic_vector(136 downto 0);
  -- psl default clock is rising_edge(clk);
begin
  -- psl ARTIFACT_CLOCK: cover clk = '1';
  vital_probe <= VitalMUX2('H', '1', 'X');
  wide_source <= (0 => '1', 136 => '1', others => '0');
  wide_result <= wide_source + 137;
  process (clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        q <= "00000000";
      else
        q <= q + step;
      end if;
    end if;
  end process;
  attribute_stimulus: process
  begin
    attribute_source <= '0';
    wait for 1 ns;
    attribute_source <= '1';
    wait;
  end process;
  attribute_probe: process
  begin
    wait for 3 ns;
    wait for 0 ns;
    stable_probe <= attribute_source'stable(1 ns);
    wait;
  end process;
end architecture;

configuration phase_counter_configuration of phase_counter is
  for rtl
  end for;
end configuration;

entity phase_watch is
end entity;
architecture rtl of phase_watch is
begin
end architecture;
)";
    }

    const auto sv_source_text = support::path_to_utf8(sv_source);
    const auto vhdl_source_text = support::path_to_utf8(vhdl_source);
    const auto sv_object_text = support::path_to_utf8(sv_object);
    const auto vhdl_object_text = support::path_to_utf8(vhdl_object);
    const auto design_text = support::path_to_utf8(design);
    const auto trace_text = support::path_to_utf8(trace);
    auto services = app::make_cli_services();
    std::ostringstream output;
    std::ostringstream error;

    const std::vector<const char*> vhdl_compile {
        "fsim", "compile", "--lang", "vhdl", "--standard", "2008",
        "--library", "work", "--output", vhdl_object_text.c_str(),
        vhdl_source_text.c_str()
    };
    const auto vhdl_result = cli::run(
        static_cast<int>(vhdl_compile.size()), vhdl_compile.data(), services,
        output, error);
    if (vhdl_result != 0) {
        std::cerr << error.str();
    }
    assert(vhdl_result == 0);
    assert(error.str().empty());
    diagnostic::Engine vhdl_inspection_diagnostics;
    const auto vhdl_inspection = app::inspect_artifact(
        vhdl_object, vhdl_inspection_diagnostics);
    assert(vhdl_inspection && !vhdl_inspection_diagnostics.has_error());
    assert(vhdl_inspection->phase == app::ArtifactPhaseKind::compilation);
    assert(vhdl_inspection->language == "vhdl");
    assert(vhdl_inspection->library == "work");
    assert(vhdl_inspection->units.size() >= 5);
    diagnostic::Engine vhdl_metadata_diagnostics;
    const auto vhdl_metadata = artifact::load_object_metadata(
        vhdl_object, vhdl_metadata_diagnostics);
    assert(vhdl_metadata && !vhdl_metadata_diagnostics.has_error());
    assert(vhdl_metadata->standard == "2008");
    assert(vhdl_metadata->compatibility_profile
        == "fsim-synopsys-ieee-compat-v2");
    assert(vhdl_metadata->vhdl_package_dependencies.size() == 2U);
    assert(vhdl_metadata->vhdl_package_dependencies.front().package
        == "ieee.std_logic_arith");
    assert(vhdl_metadata->vhdl_package_dependencies.back().package
        == "ieee.std_logic_unsigned");
    assert(std::ranges::all_of(
        vhdl_metadata->vhdl_package_dependencies,
        [](const auto& dependency) {
            return dependency.standard == "2008"
                && dependency.predefined_environment
                    == "ieee-1076-standard:2008:fsim-v1"
                && dependency.revision.starts_with(
                    "synopsys-legacy-ieee:1990-1992:"
                    "fsim-synopsys-ieee-compat-v2:")
                && dependency.revision.ends_with(":vhdl-2008")
                && dependency.source_digest.size() == 64U;
        }));
    assert(vhdl_metadata->vhdl_package_dependencies.front().revision
        == "synopsys-legacy-ieee:1990-1992:"
           "fsim-synopsys-ieee-compat-v2:std_logic_arith:vhdl-2008");
    assert(vhdl_metadata->vhdl_package_dependencies.back().revision
        == "synopsys-legacy-ieee:1990-1992:"
           "fsim-synopsys-ieee-compat-v2:std_logic_unsigned:vhdl-2008");
    const auto vhdl_compilation_digest = artifact::compute_object_compilation_digest(*vhdl_metadata);
    auto changed_vhdl_revision = *vhdl_metadata;
    changed_vhdl_revision.standard = "1993";
    assert(artifact::compute_object_compilation_digest(changed_vhdl_revision)
        != vhdl_compilation_digest);
    auto changed_vhdl_compatibility = *vhdl_metadata;
    changed_vhdl_compatibility.compatibility_profile += "-changed";
    assert(artifact::compute_object_compilation_digest(
               changed_vhdl_compatibility)
        != vhdl_compilation_digest);
    auto changed_vhdl_dependency = *vhdl_metadata;
    changed_vhdl_dependency.vhdl_package_dependencies.front().source_digest
        = std::string(64, '0');
    changed_vhdl_dependency.compilation_digest =
        artifact::compute_object_compilation_digest(changed_vhdl_dependency);
    assert(changed_vhdl_dependency.compilation_digest
        != vhdl_compilation_digest);
    const auto stale_vhdl_object =
        directory / "artifact-vhdl-stale.fsimobj";
    copy_artifact_tree(vhdl_object, stale_vhdl_object);
    const auto stale_vhdl_metadata_path = stale_vhdl_object
        / artifact::kObjectMetadataFilename;
    std::filesystem::permissions(
        stale_vhdl_metadata_path, std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
    {
        std::ofstream stale_output(
            stale_vhdl_metadata_path, std::ios::binary | std::ios::trunc);
        stale_output << artifact::serialize_object_metadata(
            changed_vhdl_dependency);
        assert(stale_output.good());
    }
    const std::array stale_vhdl_inputs { stale_vhdl_object };
    diagnostic::Engine stale_vhdl_diagnostics;
    assert(!app::load_objects(stale_vhdl_inputs, stale_vhdl_diagnostics));
    assert(std::ranges::any_of(
        stale_vhdl_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-VHDEP-001";
        }));
    auto future_vhdl_object = *vhdl_metadata;
    ++future_vhdl_object.format;
    diagnostic::Engine future_vhdl_object_diagnostics;
    assert(!artifact::deserialize_object_metadata(
        artifact::serialize_object_metadata(future_vhdl_object),
        "future-vhdl-object", future_vhdl_object_diagnostics));
    assert(std::ranges::any_of(
        future_vhdl_object_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-0001";
        }));
    output.str({ });
    error.str({ });
    const std::vector<const char*> sv_compile {
        "fsim", "compile", "--lang", "systemverilog", "--standard", "2017",
        "--library", "work", "--output", sv_object_text.c_str(),
        sv_source_text.c_str()
    };
    const auto sv_result = cli::run(
        static_cast<int>(sv_compile.size()), sv_compile.data(), services,
        output, error);
    if (sv_result != 0) {
        std::cerr << error.str();
    }
    assert(sv_result == 0);
    assert(error.str().empty());

    output.str({ });
    error.str({ });
    const std::vector<const char*> elaborate {
        "fsim", "elaborate", "--object", vhdl_object_text.c_str(),
        "--object", sv_object_text.c_str(), "--top",
        "main=sv:work.phase_tb", "--top", "observer=sv:work.phase_watch",
        "--top", "scalar=sv:work.scalar_artifact",
        "--top", "virtual=sv:work.interface_artifact",
        "--output", design_text.c_str(), "--seed", "23"
    };
    const auto elaborate_result = cli::run(
        static_cast<int>(elaborate.size()), elaborate.data(), services,
        output, error);
    if (elaborate_result != 0) {
        std::cerr << error.str();
    }
    assert(elaborate_result == 0);
    assert(error.str().empty());
    diagnostic::Engine design_inspection_diagnostics;
    const auto design_inspection = app::inspect_artifact(
        design, design_inspection_diagnostics);
    assert(design_inspection && !design_inspection_diagnostics.has_error());
    assert(design_inspection->phase == app::ArtifactPhaseKind::elaboration);
    assert(design_inspection->compatible);
    assert(design_inspection->runtime_abi == runtime_abi_version);
    assert(design_inspection->roots
        == std::vector<std::string>(
            { "main", "observer", "scalar", "virtual" }));
    assert(design_inspection->process_count != 0);
    diagnostic::Engine design_metadata_diagnostics;
    const auto design_metadata = artifact::load_design_metadata(
        design, design_metadata_diagnostics);
    assert(design_metadata && !design_metadata_diagnostics.has_error());
    const auto vhdl_design_input = std::ranges::find_if(
        design_metadata->objects,
        [](const auto& object) { return object.language == "vhdl"; });
    assert(vhdl_design_input != design_metadata->objects.end());
    assert(vhdl_design_input->standard == "2008");
    assert(vhdl_design_input->compatibility_profile
        == "fsim-synopsys-ieee-compat-v2");
    assert(vhdl_design_input->vhdl_package_dependencies
        == vhdl_metadata->vhdl_package_dependencies);
    assert(!design_metadata->vhdl_unit_provenance.empty());
    assert(std::ranges::all_of(
        design_metadata->vhdl_unit_provenance,
        [](const auto& provenance) {
            return provenance.standard == "2008"
                && provenance.predefined_environment
                    == "ieee-1076-standard:2008:fsim-v1"
                && provenance.compatibility_profile
                    == "fsim-synopsys-ieee-compat-v2";
        }));
    const auto package_unit = std::ranges::find_if(
        design_metadata->vhdl_unit_provenance,
        [](const auto& provenance) {
            return std::ranges::any_of(
                provenance.package_dependencies,
                [](const auto& dependency) {
                    return dependency.package
                        == "ieee.std_logic_unsigned";
                });
        });
    assert(package_unit != design_metadata->vhdl_unit_provenance.end());
    const auto design_digest = artifact::compute_design_digest(*design_metadata);
    auto changed_design_revision = *design_metadata;
    std::ranges::find_if(
        changed_design_revision.objects,
        [](const auto& object) { return object.language == "vhdl"; })
        ->standard = "1993";
    assert(artifact::compute_design_digest(changed_design_revision)
        != design_digest);
    auto changed_design_compatibility = *design_metadata;
    std::ranges::find_if(
        changed_design_compatibility.objects,
        [](const auto& object) { return object.language == "vhdl"; })
        ->compatibility_profile += "-changed";
    assert(artifact::compute_design_digest(changed_design_compatibility)
        != design_digest);
    auto changed_unit_revision = *design_metadata;
    std::ranges::find_if(
        changed_unit_revision.vhdl_unit_provenance,
        [](const auto& provenance) {
            return !provenance.package_dependencies.empty();
        })->package_dependencies.front().revision += "-changed";
    assert(artifact::compute_design_digest(changed_unit_revision)
        != design_digest);
    auto changed_design_dependency = *design_metadata;
    std::ranges::find_if(
        changed_design_dependency.objects,
        [](const auto& object) { return object.language == "vhdl"; })
        ->vhdl_package_dependencies.front().source_digest =
            std::string(64, '0');
    changed_design_dependency.design_digest =
        artifact::compute_design_digest(changed_design_dependency);
    const auto stale_design =
        directory / "artifact-vhdl-stale.fsimdesign";
    copy_artifact_tree(design, stale_design);
    const auto stale_design_metadata_path = stale_design
        / artifact::kDesignMetadataFilename;
    std::filesystem::permissions(
        stale_design_metadata_path, std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
    {
        std::ofstream stale_output(
            stale_design_metadata_path, std::ios::binary | std::ios::trunc);
        stale_output << artifact::serialize_design_metadata(
            changed_design_dependency);
        assert(stale_output.good());
    }
    diagnostic::Engine stale_design_diagnostics;
    assert(!app::load_design_artifact(stale_design, stale_design_diagnostics));
    assert(std::ranges::any_of(
        stale_design_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-VHDEP-001";
        }));
    auto future_design_metadata = *design_metadata;
    ++future_design_metadata.format;
    diagnostic::Engine future_design_metadata_diagnostics;
    assert(!artifact::deserialize_design_metadata(
        artifact::serialize_design_metadata(future_design_metadata),
        "future-vhdl-design", future_design_metadata_diagnostics));
    assert(std::ranges::any_of(
        future_design_metadata_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-0010";
        }));
    auto active_design = design;

    diagnostic::Engine coverage_load_diagnostics;
    auto coverage_checkpoint = app::load_design_artifact(
        active_design, coverage_load_diagnostics);
    assert(coverage_checkpoint && !coverage_load_diagnostics.has_error());
    assert(std::ranges::any_of(
        coverage_checkpoint->design.processes(),
        [](const auto& process) {
            return process.language_standard == "2008"
                && process.compatibility_profile
                == "fsim-synopsys-ieee-compat-v2";
        }));
    assert(!coverage_checkpoint->vhdl_hir.units().empty());
    assert(std::ranges::any_of(
        coverage_checkpoint->vhdl_hir.units(), [](const auto& unit) {
            return std::ranges::any_of(
                unit.psl_directives, [](const auto& directive) {
                    return directive.label == "artifact_clock";
                });
        }));
    diagnostic::Engine vhdl_hir_codec_diagnostics;
    const auto vhdl_hir_bytes = app::serialize_vhdl_hir_state(
        coverage_checkpoint->vhdl_hir, coverage_checkpoint->semantics,
        vhdl_hir_codec_diagnostics);
    assert(vhdl_hir_bytes && !vhdl_hir_codec_diagnostics.has_error());
    auto restored_vhdl_hir = app::deserialize_vhdl_hir_state(
        *vhdl_hir_bytes, "vhdl-hir.bin", coverage_checkpoint->semantics,
        vhdl_hir_codec_diagnostics);
    assert(restored_vhdl_hir && !vhdl_hir_codec_diagnostics.has_error());
    assert(restored_vhdl_hir->units().size()
        == coverage_checkpoint->vhdl_hir.units().size());
    assert(restored_vhdl_hir->declarations().size()
        == coverage_checkpoint->vhdl_hir.declarations().size());
    assert(restored_vhdl_hir->processes().size()
        == coverage_checkpoint->vhdl_hir.processes().size());
    const auto repeated_vhdl_hir = app::serialize_vhdl_hir_state(
        *restored_vhdl_hir, coverage_checkpoint->semantics,
        vhdl_hir_codec_diagnostics);
    assert(repeated_vhdl_hir == vhdl_hir_bytes);
    auto future_vhdl_hir = *vhdl_hir_bytes;
    future_vhdl_hir[8] = static_cast<char>(app::kVhdlHirStateSchema + 1U);
    diagnostic::Engine future_vhdl_hir_diagnostics;
    assert(!app::deserialize_vhdl_hir_state(
        future_vhdl_hir, "future-vhdl-hir.bin",
        coverage_checkpoint->semantics, future_vhdl_hir_diagnostics));
    diagnostic::Engine truncated_vhdl_hir_diagnostics;
    assert(!app::deserialize_vhdl_hir_state(
        vhdl_hir_bytes->substr(0, vhdl_hir_bytes->size() - 1U),
        "truncated-vhdl-hir.bin", coverage_checkpoint->semantics,
        truncated_vhdl_hir_diagnostics));
    auto invalid_vhdl_hir = *restored_vhdl_hir;
    invalid_vhdl_hir.mutable_units().front().id = semantic::UnitId::from_index(
        static_cast<std::uint32_t>(
            coverage_checkpoint->semantics.units().size()));
    diagnostic::Engine invalid_vhdl_hir_diagnostics;
    assert(!app::serialize_vhdl_hir_state(
        invalid_vhdl_hir, coverage_checkpoint->semantics,
        invalid_vhdl_hir_diagnostics));
    assert(coverage_checkpoint->systemverilog_uvm_checkpoint);
    const auto& uvm_checkpoint = *coverage_checkpoint->systemverilog_uvm_checkpoint;
    assert(
        uvm_checkpoint.schema
            == runtime::systemverilog_uvm_checkpoint_schema
        && uvm_checkpoint.foreign_abi == FSIM_UVM_FOREIGN_ABI_VERSION
        && uvm_checkpoint.provenance.roots
            == std::vector<std::string>(
                { "main", "observer", "scalar", "virtual" })
        && uvm_checkpoint.records.size()
            == runtime::kSystemVerilogUvmStandardPhaseCount
        && uvm_checkpoint.external_state.phase_processes == 0
        && uvm_checkpoint.external_state.callbacks == 0);
    diagnostic::Engine uvm_codec_diagnostics;
    const auto uvm_bytes = app::serialize_systemverilog_uvm_state(
        uvm_checkpoint, uvm_codec_diagnostics);
    assert(uvm_bytes && !uvm_codec_diagnostics.has_error());
    const auto restored_uvm = app::deserialize_systemverilog_uvm_state(
        *uvm_bytes, "sv-uvm.bin", uvm_codec_diagnostics);
    assert(restored_uvm == uvm_checkpoint);
    const auto repeated_uvm_bytes = app::serialize_systemverilog_uvm_state(
        *restored_uvm, uvm_codec_diagnostics);
    assert(repeated_uvm_bytes == uvm_bytes);
    auto future_uvm = *uvm_bytes;
    future_uvm[8] = static_cast<char>(app::kSystemVerilogUvmStateSchema + 1U);
    diagnostic::Engine future_uvm_diagnostics;
    assert(!app::deserialize_systemverilog_uvm_state(
        future_uvm, "future-sv-uvm.bin", future_uvm_diagnostics));
    const auto truncated_uvm = uvm_bytes->substr(0, uvm_bytes->size() - 1U);
    diagnostic::Engine truncated_uvm_diagnostics;
    assert(!app::deserialize_systemverilog_uvm_state(
        truncated_uvm, "truncated-sv-uvm.bin", truncated_uvm_diagnostics));
    auto nonportable_uvm = uvm_checkpoint;
    nonportable_uvm.records.front().kind = FSIM_UVM_FOREIGN_PHASE_PROCESS;
    diagnostic::Engine nonportable_uvm_diagnostics;
    assert(!app::serialize_systemverilog_uvm_state(
        nonportable_uvm, nonportable_uvm_diagnostics));
    assert(std::ranges::any_of(
        nonportable_uvm_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-UVM-STATE-001";
        }));
    const auto corrupt_uvm_design = directory / "artifact-corrupt-uvm.fsimdesign";
    std::filesystem::create_directories(corrupt_uvm_design);
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(active_design)) {
        const auto destination = corrupt_uvm_design
            / entry.path().lexically_relative(active_design);
        if (entry.is_directory()) {
            std::filesystem::create_directories(destination);
        } else if (entry.is_regular_file()) {
            std::filesystem::copy_file(entry.path(), destination);
            std::filesystem::permissions(
                destination, std::filesystem::perms::owner_write,
                std::filesystem::perm_options::add);
        }
    }
    {
        std::ofstream corrupt(
            corrupt_uvm_design / "state" / "sv-uvm.bin",
            std::ios::binary | std::ios::app);
        corrupt.put('\0');
    }
    diagnostic::Engine corrupt_uvm_diagnostics;
    assert(!app::load_design_artifact(
        corrupt_uvm_design, corrupt_uvm_diagnostics));
    assert(corrupt_uvm_diagnostics.has_error());
    std::filesystem::remove_all(corrupt_uvm_design);
    const auto corrupt_vhdl_hir_design = directory / "artifact-corrupt-vhdl-hir.fsimdesign";
    std::filesystem::create_directories(corrupt_vhdl_hir_design);
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(active_design)) {
        const auto destination = corrupt_vhdl_hir_design
            / entry.path().lexically_relative(active_design);
        if (entry.is_directory()) {
            std::filesystem::create_directories(destination);
        } else if (entry.is_regular_file()) {
            std::filesystem::copy_file(entry.path(), destination);
            std::filesystem::permissions(
                destination, std::filesystem::perms::owner_write,
                std::filesystem::perm_options::add);
        }
    }
    {
        std::ofstream corrupt(
            corrupt_vhdl_hir_design / "state" / "vhdl-hir.bin",
            std::ios::binary | std::ios::app);
        corrupt.put('\0');
    }
    diagnostic::Engine corrupt_vhdl_hir_diagnostics;
    assert(!app::load_design_artifact(
        corrupt_vhdl_hir_design, corrupt_vhdl_hir_diagnostics));
    assert(corrupt_vhdl_hir_diagnostics.has_error());
    std::filesystem::remove_all(corrupt_vhdl_hir_design);
    assert(std::filesystem::remove(vhdl_source));
    auto coverage_state = coverage_checkpoint->systemverilog_coverage;
    assert(coverage_state.declarations.size() == 1);
    assert(coverage_state.instances.size() == 1);
    assert(coverage_state.reports.size() == 1);
    const auto& coverage_declaration = coverage_state.declarations.front();
    assert(coverage_declaration.name == "artifact_coverage");
    assert(coverage_declaration.effective_instance_goal == 75);
    assert(coverage_declaration.effective_merge_instances);
    assert(coverage_declaration.coverage_declarations.size() == 1);
    assert(coverage_declaration.coverage_declarations.front().bins.size() == 6);
    auto& coverage_instance = coverage_state.instances.front();
    assert(coverage_instance.runtime_identity.find("0x") == std::string::npos);
    auto& first_bin
        = coverage_state.declarations.front().coverage_declarations.front().bins.front();
    assert(!first_bin.values.empty());
    auto& wide_bin_value = first_bin.values.front();
    wide_bin_value.width = 137;
    wide_bin_value.exact_value.reset();
    wide_bin_value.range_left.reset();
    wide_bin_value.range_right.reset();
    wide_bin_value.exact_bits = "1" + std::string(135, '0') + "1";
    wide_bin_value.range_left_bits = std::string(136, '0') + "1";
    wide_bin_value.range_right_bits = "1" + std::string(136, '0');
    wide_bin_value.wildcard_value_bits
        = "10" + std::string(133, '0') + "01";
    wide_bin_value.wildcard_mask_bits
        = "01" + std::string(133, '0') + "10";
    wide_bin_value.exact_signed = true;
    wide_bin_value.range_left_signed = true;
    wide_bin_value.range_right_signed = false;
    frontend::SystemVerilogCoverageBinHit hit;
    hit.coverage_declaration_index = 0;
    hit.bin_declaration_index = first_bin.declaration_index;
    hit.identity = first_bin.name;
    hit.hit_count = 3;
    hit.at_least = first_bin.at_least;
    hit.covered = true;
    coverage_instance.bin_hits.push_back(hit);
    auto automatic_hit = hit;
    automatic_hit.identity = "automatic-wide";
    automatic_hit.automatic_value = 1;
    automatic_hit.automatic_value_bits = wide_bin_value.exact_bits;
    automatic_hit.automatic_unknown_bits = std::string(137, '0');
    automatic_hit.automatic_width = 137;
    automatic_hit.automatic_signed = true;
    coverage_instance.bin_hits.push_back(automatic_hit);
    coverage_instance.transition_progress.push_back({ 0, 4, 0, 1, 2, 3 });
    frontend::SystemVerilogCoveragePreviousSample previous_sample;
    previous_sample.coverage_declaration_index = 0;
    previous_sample.width = 137;
    previous_sample.value_bits = wide_bin_value.exact_bits;
    previous_sample.unknown_bits = std::string(64, '0') + "1"
        + std::string(72, '0');
    previous_sample.signed_value = true;
    coverage_instance.previous_samples.push_back(previous_sample);
    coverage_instance.cross_bin_state.push_back(
        { 0, std::nullopt, "tuple", { first_bin.name }, 4, 1, 1, 100, 1,
            true, false });
    frontend::SystemVerilogCoverageIllegalBinReport illegal_report;
    illegal_report.bin_identity = "rejected";
    illegal_report.sampled_width = 137;
    illegal_report.sampled_value_bits = wide_bin_value.exact_bits;
    illegal_report.sampled_unknown_bits = previous_sample.unknown_bits;
    illegal_report.sampled_signed = true;
    illegal_report.span = first_bin.span;
    coverage_instance.illegal_bin_reports.push_back(illegal_report);
    frontend::SystemVerilogCoverageCallbackEvent callback;
    callback.sequence = 9;
    callback.kind = frontend::SystemVerilogCoverageCallbackKind::Hit;
    callback.trigger = frontend::SystemVerilogCoverageSampleTrigger::Procedural;
    callback.mode = frontend::SystemVerilogCoverageExecutionMode::LlvmO2;
    callback.runtime_identity = coverage_instance.runtime_identity;
    callback.bin_identity = first_bin.name;
    callback.value = 2;
    coverage_state.callback_events.push_back(callback);
    coverage_state.trace_events.push_back(
        { 9, 42, 7, "alias.coverage.hit", "coverage.hit",
            frontend::SystemVerilogCoverageCallbackKind::Hit, 2, true });
    coverage_state.aliases.push_back({ "alias.coverage", "coverage" });
    frontend::refresh_systemverilog_coverage_reports(coverage_state);
    diagnostic::Engine coverage_codec_diagnostics;
    const auto coverage_bytes = app::serialize_systemverilog_coverage_state(
        coverage_state, coverage_codec_diagnostics);
    assert(coverage_bytes && !coverage_codec_diagnostics.has_error());
    auto restored_coverage = app::deserialize_systemverilog_coverage_state(
        *coverage_bytes, "coverage-state.bin", coverage_codec_diagnostics);
    assert(restored_coverage && !coverage_codec_diagnostics.has_error());
    const auto restored_bytes = app::serialize_systemverilog_coverage_state(
        *restored_coverage, coverage_codec_diagnostics);
    assert(restored_bytes == coverage_bytes);
    const auto& restored_wide_bin_value = restored_coverage->declarations.front()
                                              .coverage_declarations.front()
                                              .bins.front()
                                              .values.front();
    assert(restored_wide_bin_value.width == 137);
    assert(
        restored_coverage->declarations.front()
                .coverage_declarations.front()
                .bins.front()
                .with_tokens.size()
            == 3U
        && restored_coverage->declarations.front()
                .coverage_declarations.front()
                .bins.front()
                .with_tokens[1]
                .text
            == "<=");
    assert(restored_wide_bin_value.exact_bits == wide_bin_value.exact_bits);
    assert(restored_wide_bin_value.range_left_bits
        == wide_bin_value.range_left_bits);
    assert(restored_wide_bin_value.range_right_bits
        == wide_bin_value.range_right_bits);
    assert(restored_wide_bin_value.wildcard_value_bits
        == wide_bin_value.wildcard_value_bits);
    assert(restored_wide_bin_value.wildcard_mask_bits
        == wide_bin_value.wildcard_mask_bits);
    assert(restored_wide_bin_value.exact_signed);
    assert(restored_wide_bin_value.range_left_signed);
    assert(!restored_wide_bin_value.range_right_signed);
    assert(restored_coverage->instances.front().bin_hits.front().hit_count == 3);
    assert(restored_coverage->instances.front().bin_hits.size() == 2);
    assert(restored_coverage->instances.front().bin_hits[1].automatic_value_bits
        == automatic_hit.automatic_value_bits);
    assert(restored_coverage->instances.front().bin_hits[1].automatic_width
        == 137);
    assert(restored_coverage->instances.front().transition_progress.size() == 1);
    assert(restored_coverage->instances.front().previous_samples.size() == 1);
    assert(restored_coverage->instances.front()
               .previous_samples.front()
               .value_bits
        == previous_sample.value_bits);
    assert(restored_coverage->instances.front()
               .previous_samples.front()
               .unknown_bits
        == previous_sample.unknown_bits);
    assert(restored_coverage->instances.front().cross_bin_state.size() == 1);
    assert(restored_coverage->instances.front().illegal_bin_reports.size() == 1);
    assert(restored_coverage->instances.front()
               .illegal_bin_reports.front()
               .sampled_value_bits
        == illegal_report.sampled_value_bits);
    assert(restored_coverage->instances.front()
               .illegal_bin_reports.front()
               .sampled_unknown_bits
        == illegal_report.sampled_unknown_bits);
    assert(restored_coverage->callback_events.front().runtime_identity
        == coverage_instance.runtime_identity);
    assert(restored_coverage->trace_events.front().time == 42);
    assert(restored_coverage->aliases.front().canonical_root == "coverage");
    assert(frontend::render_systemverilog_coverage_report(
               restored_coverage->reports.front())
        == frontend::render_systemverilog_coverage_report(
            coverage_state.reports.front()));
    auto future_coverage = *coverage_bytes;
    future_coverage[8]
        = static_cast<char>(app::kSystemVerilogCoverageStateSchema + 1U);
    diagnostic::Engine future_coverage_diagnostics;
    assert(!app::deserialize_systemverilog_coverage_state(
        future_coverage, "future-coverage-state.bin",
        future_coverage_diagnostics));
    diagnostic::Engine truncated_coverage_diagnostics;
    assert(!app::deserialize_systemverilog_coverage_state(
        coverage_bytes->substr(0, coverage_bytes->size() - 1U),
        "truncated-coverage-state.bin", truncated_coverage_diagnostics));

    const auto missing_design = directory / "artifact-missing.fsimdesign";
    const auto missing_design_text = support::path_to_utf8(missing_design);
    const std::vector<const char*> missing_elaborate {
        "fsim", "elaborate", "--object", sv_object_text.c_str(), "--top",
        "missing=sv:work.absent", "--output", missing_design_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(
               static_cast<int>(missing_elaborate.size()), missing_elaborate.data(),
               services, output, error)
        != 0);
    assert(error.str().find("absent") != std::string::npos);
    assert(!std::filesystem::exists(missing_design));

    const auto ambiguous_design = directory / "artifact-ambiguous.fsimdesign";
    const auto ambiguous_design_text = support::path_to_utf8(ambiguous_design);
    const std::vector<const char*> ambiguous_elaborate {
        "fsim", "elaborate", "--object", vhdl_object_text.c_str(),
        "--object", sv_object_text.c_str(), "--top",
        "collision=phase_watch", "--output", ambiguous_design_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(
               static_cast<int>(ambiguous_elaborate.size()),
               ambiguous_elaborate.data(), services, output, error)
        != 0);
    assert(error.str().find("ambiguous") != std::string::npos);
    assert(!std::filesystem::exists(ambiguous_design));
    std::filesystem::rename(
        vhdl_object,
        directory / "artifact-vhdl.fsimobj.producer-hidden");
    assert(!std::filesystem::exists(vhdl_object));

    std::vector<runtime::SystemVerilogUvmCheckpointArtifact>
        replay_checkpoints;
    const auto run_engine = [&](const app::SimulationEngine engine) {
        diagnostic::Engine diagnostics;
        auto built = app::load_design_artifact(active_design, diagnostics);
        assert(built && !diagnostics.has_error());
        assert(built->design.roots()
            == std::vector<std::string>(
                { "main", "observer", "scalar", "virtual" }));
        assert(built->semantics.source_files().size() >= 2);
        assert(built->design.verilog_specify_paths().size() == 1);
        assert(built->design.verilog_timing_checks().size() == 1);
        assert(built->systemverilog_coverage.declarations.size() == 1);
        assert(built->systemverilog_coverage.instances.size() == 1);
        assert(built->systemverilog_coverage.reports.size() == 1);
        assert(built->vhdl_unit_provenance.size()
            == design_metadata->vhdl_unit_provenance.size());
        for (std::size_t index = 0;
            index < built->vhdl_unit_provenance.size(); ++index) {
            const auto& actual = built->vhdl_unit_provenance[index];
            const auto& expected =
                design_metadata->vhdl_unit_provenance[index];
            assert(actual.unit.value() == expected.unit);
            assert(actual.standard == expected.standard);
            assert(actual.predefined_environment
                == expected.predefined_environment);
            assert(actual.compatibility_profile
                == expected.compatibility_profile);
            assert(actual.package_dependencies
                == expected.package_dependencies);
        }
        assert(std::ranges::any_of(
            built->vhdl_hir.units(), [](const auto& unit) {
                return std::ranges::any_of(
                    unit.psl_directives, [](const auto& directive) {
                        return directive.label == "artifact_clock";
                    });
            }));
        for (const auto& file : built->semantics.source_files()) {
            assert(!std::filesystem::path(file.physical_name).is_absolute());
        }
        built->cache_path = directory / "artifact-phase-cache";
        std::filesystem::create_directories(built->cache_path);
        app::Simulation simulation { std::move(*built), 1000, engine };
        assert(simulation.vhdl_unit_provenance().size()
            == design_metadata->vhdl_unit_provenance.size());
        const auto provenance_comments =
            simulation.vhdl_provenance_comments();
        assert(std::ranges::any_of(
            provenance_comments, [](const auto& comment) {
                return comment.find("standard=2008") != std::string::npos
                    && comment.find(
                           "environment=ieee-1076-standard:2008:fsim-v1")
                        != std::string::npos
                    && comment.find(
                           "profile=fsim-synopsys-ieee-compat-v2")
                        != std::string::npos
                    && comment.find("package=ieee.std_logic_unsigned@")
                        != std::string::npos;
            }));
        const auto counter = simulation.find_signal("main.counter_q");
        const auto watch = simulation.find_signal("observer.watched");
        const auto stable_probe = simulation.find_signal("main.counter.stable_probe");
        const auto vital_probe = simulation.find_signal("main.counter.vital_probe");
        const auto scalar_checks = simulation.find_signal("scalar.checks");
        const auto scalar_real = simulation.find_signal("scalar.r");
        const auto scalar_short = simulation.find_signal("scalar.s");
        const auto scalar_realtime = simulation.find_signal("scalar.rt");
        const auto scalar_time = simulation.find_signal("scalar.ticks");
        const auto scalar_handle = simulation.find_signal("scalar.handle");
        const auto scalar_wide = simulation.find_signal("scalar.wide_value");
        const auto virtual_selected = simulation.find_signal("virtual.selected");
        const auto forwarded_selected = simulation.find_signal("virtual.leaf.selected");
        const auto clocking_sample = simulation.find_signal("virtual.leaf.bus.cb.data");
        assert(counter && watch && stable_probe && vital_probe && scalar_checks
            && scalar_real && scalar_short && scalar_realtime && scalar_time
            && scalar_handle && scalar_wide && virtual_selected
            && forwarded_selected && clocking_sample);
        std::size_t callbacks { };
        simulation.set_signal_change_hook(
            [&](runtime::simir::SignalId, const runtime::PackedLogic4&,
                runtime::SimulationTick, std::uint64_t) { ++callbacks; });
        const auto result = simulation.run();
        assert(result.status == runtime::RunStatus::stopped);
        assert(result.time == 6);
        const auto replay_checkpoint = simulation.capture_uvm_checkpoint();
        assert(
            replay_checkpoint
            && replay_checkpoint.artifact.time == result.time
            && replay_checkpoint.artifact.delta == simulation.delta()
            && !replay_checkpoint.artifact.provenance.content_identity.empty()
            && !replay_checkpoint.artifact.provenance.cache_identity.empty()
            && !replay_checkpoint.artifact.provenance.artifact_identity.empty()
            && replay_checkpoint.artifact.external_state.phase_processes == 0);
        replay_checkpoints.push_back(replay_checkpoint.artifact);
        assert(callbacks != 0);
        assert(simulation.read_signal(*stable_probe).to_msb_string() == "1");
        assert(simulation.read_signal(*vital_probe).to_msb_string() == "1");
        assert(simulation.read_signal(*scalar_checks).to_msb_string() == "111111");
        auto expected_wide = runtime::PackedLogic4 {
            137, runtime::Logic4::zero
        };
        expected_wide.set(136, runtime::Logic4::one);
        expected_wide.set(73, runtime::Logic4::x);
        expected_wide.set(3, runtime::Logic4::one);
        expected_wide.set(1, runtime::Logic4::z);
        assert(simulation.read_signal(*scalar_wide) == expected_wide);
        assert(simulation.read_scalar_signal(*scalar_real).as_real() == 1.25);
        assert(simulation.read_scalar_signal(*scalar_short).as_shortreal() == -2.5F);
        assert(simulation.read_scalar_signal(*scalar_realtime).as_real() == 3.75);
        assert(simulation.read_scalar_signal(*scalar_time).as_time()
            == UINT64_C(9007199254740993));
        assert(simulation.read_scalar_signal(*scalar_handle).as_chandle() == 0);
        const auto virtual_handle = simulation.read_signal(*virtual_selected).low_word();
        assert(virtual_handle.aval != 0 && virtual_handle.bval == 0);
        assert(simulation.read_signal(*forwarded_selected).low_word()
            == virtual_handle);
        assert(simulation.read_signal(*clocking_sample).to_msb_string()
            == "1010");
        std::ostringstream debugger_output;
        std::ostringstream debugger_error;
        app::DebuggerControl debugger {
            simulation, debugger_output, debugger_error
        };
        debugger.execute({ "show", "main.counter.attribute_source'stable(1)" });
        assert(debugger_error.str().empty());
        assert(debugger_output.str().find("1") != std::string::npos);
        assert(!simulation.vhdl_psl_attempts().empty());
        return std::tuple {
            simulation.read_signal(*counter).to_msb_string(),
            simulation.read_signal(*watch).to_msb_string(),
            simulation.vhdl_psl_attempts()
        };
    };
    const auto interpreted = run_engine(app::SimulationEngine::interpreter);
    const auto compiled = run_engine(app::SimulationEngine::compiled);
    const auto compiled_warm = run_engine(app::SimulationEngine::compiled);
    assert(interpreted == compiled);
    assert(compiled == compiled_warm);
    assert(
        replay_checkpoints.size() == 3
        && replay_checkpoints[0] == replay_checkpoints[1]
        && replay_checkpoints[1] == replay_checkpoints[2]);
    assert(std::get<0>(interpreted) == "00000001");
    assert(std::get<1>(interpreted) == "1");

    const auto relocated_design = directory / "relocated.fsimdesign";
    std::filesystem::rename(active_design, relocated_design);
    active_design = relocated_design;
    const auto relocated = run_engine(app::SimulationEngine::interpreter);
    assert(relocated == interpreted);
    assert(replay_checkpoints.back() == replay_checkpoints.front());

    output.str({ });
    error.str({ });
    const auto active_design_text = support::path_to_utf8(active_design);
    const std::vector<const char*> simulate {
        "fsim", "simulate", "--design", active_design_text.c_str(), "--engine",
        "compiled", "--trace", trace_text.c_str(), "--trace-filter",
        "main.*", "--trace-filter", "observer.*", "--trace-filter",
        "scalar.*", "--trace-filter", "virtual.*"
    };
    const auto simulate_result = cli::run(
        static_cast<int>(simulate.size()), simulate.data(), services,
        output, error);
    if (simulate_result != 0) {
        std::cerr << output.str() << error.str();
    }
    assert(simulate_result == 0);
    assert(error.str().empty());
    const auto trace_bytes = [&] {
        std::ifstream input(trace, std::ios::binary);
        return std::string {
            std::istreambuf_iterator<char> { input },
            std::istreambuf_iterator<char> { }
        };
    }();
    assert(trace_bytes.find("main") != std::string::npos);
    assert(trace_bytes.find("observer") != std::string::npos);
    assert(trace_bytes.find("stable_probe") != std::string::npos);
    assert(trace_bytes.find("vital_probe") != std::string::npos);
    assert(trace_bytes.find("scalar") != std::string::npos);
    assert(trace_bytes.find("ticks") != std::string::npos);
    assert(trace_bytes.find("handle") != std::string::npos);
    assert(trace_bytes.find("virtual") != std::string::npos);
    assert(trace_bytes.find("selected") != std::string::npos);
    assert(
        trace_bytes.find("attribute_source'stable(1)")
        != std::string::npos);
    assert(trace_bytes.find("$comment fsim-vhdl-scope path=")
        != std::string::npos);
    assert(trace_bytes.find("standard=2008") != std::string::npos);
    assert(trace_bytes.find(
               "environment=ieee-1076-standard:2008:fsim-v1")
        != std::string::npos);
    assert(trace_bytes.find(
               "profile=fsim-synopsys-ieee-compat-v2")
        != std::string::npos);
    assert(trace_bytes.find("package=ieee.std_logic_unsigned@")
        != std::string::npos);

    const auto systemc_phase_source = directory / "artifact_phase.cpp";
    const auto systemc_phase_object = directory / "artifact-systemc.fsimobj";
    {
        std::ofstream systemc_output(systemc_phase_source);
        systemc_output << "SC_MODULE(ArtifactPhase) {};\n";
    }
    const auto systemc_source_text = support::path_to_utf8(systemc_phase_source);
    const auto systemc_object_text = support::path_to_utf8(systemc_phase_object);
    const std::vector<const char*> systemc_compile {
        "fsim", "compile", "--lang", "systemc", "--standard", "2023",
        "--library", "work", "--output", systemc_object_text.c_str(),
        systemc_source_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(
               static_cast<int>(systemc_compile.size()), systemc_compile.data(),
               services, output, error)
        != 0);
    assert(error.str().find("Batch 138") != std::string::npos);
    assert(!std::filesystem::exists(systemc_phase_object));

    project::Config api_compile_config;
    api_compile_config.manifest_path = "<artifact-api>";
    api_compile_config.base_directory = directory;
    api_compile_config.project.name = "artifact-api-compile";
    project::SourceSet api_sources;
    api_sources.language = project::Language::system_verilog;
    api_sources.standard = "2017";
    api_sources.library = "work";
    api_sources.compilation_unit = "source-set";
    api_sources.file_patterns = { sv_source };
    api_sources.files = { sv_source };
    api_compile_config.source_sets.push_back(std::move(api_sources));
    const auto api_object = directory / "artifact-api.fsimobj";
    diagnostic::Engine api_compile_diagnostics;
    assert(app::compile_artifact(
        api_compile_config, api_object, api_compile_diagnostics));
    assert(!api_compile_diagnostics.has_error());

    project::Config api_elaborate_config;
    api_elaborate_config.manifest_path = "<artifact-api>";
    api_elaborate_config.base_directory = directory;
    api_elaborate_config.project.name = "artifact-api-elaborate";
    api_elaborate_config.project.tops.push_back(
        { "sv:work.phase_watch", "api" });
    api_elaborate_config.project.time_resolution = "1ns";
    api_elaborate_config.build.cache_path = directory / "artifact-api-cache";
    const auto api_design = directory / "artifact-api.fsimdesign";
    const std::array api_objects { api_object };
    diagnostic::Engine api_elaborate_diagnostics;
    assert(app::elaborate_artifact(
        api_elaborate_config, api_objects, api_design,
        api_elaborate_diagnostics));
    assert(!api_elaborate_diagnostics.has_error());
    diagnostic::Engine api_load_diagnostics;
    auto api_loaded = app::load_design_artifact(
        api_design, api_load_diagnostics);
    assert(api_loaded && !api_load_diagnostics.has_error());
    assert(api_loaded->design.roots() == std::vector<std::string> { "api" });
    auto& malformed_scalar_signals = const_cast<std::vector<elaboration::SignalInfo>&>(
        api_loaded->design.signals());
    assert(!malformed_scalar_signals.empty());
    malformed_scalar_signals.front().systemverilog_scalar = static_cast<frontend::SystemVerilogScalarKind>(255);
    diagnostic::Engine malformed_scalar_artifact_diagnostics;
    assert(!app::serialize_runtime_state(
        api_loaded->design, malformed_scalar_artifact_diagnostics));
    assert(std::ranges::any_of(
        malformed_scalar_artifact_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-0013"
                && diagnostic.message.find("invalid scalar enumeration")
                != std::string::npos;
        }));

    auto scalar_export_config = api_compile_config;
    scalar_export_config.project.name = "scalar-artifact-library";
    scalar_export_config.project.top = "sv:work.scalar_artifact";
    scalar_export_config.project.time_resolution = "1ns";
    scalar_export_config.build.cache_path = directory / "scalar-export-cache";
    auto scalar_library = directory / "scalar-artifact.fsimlib";
    diagnostic::Engine scalar_export_diagnostics;
    assert(app::export_library(
        scalar_export_config, "work", scalar_library,
        scalar_export_diagnostics));
    assert(!scalar_export_diagnostics.has_error());
    struct ScalarLibraryCapture {
        std::string checks;
        std::string wide;
        std::string wide_shift;
        std::array<std::uint64_t, 5> payloads { };
        std::vector<std::string> keys;
        app::NativeCacheStatistics cache;
        std::size_t compiled_processes { };
        std::string coverage_identity;
        std::string coverage_report;
    };
    ScalarLibraryCapture scalar_o2_reference;
    const auto run_scalar_library = [&](
                                        const project::Optimization optimization,
                                        const app::SimulationEngine engine) {
        project::Config mapped;
        mapped.base_directory = directory;
        mapped.project.name = "scalar-artifact-consumer";
        mapped.project.top = "sv:work.scalar_artifact";
        mapped.project.time_resolution = "1ns";
        mapped.build.optimization = optimization;
        mapped.build.cache_path = directory
            / (optimization == project::Optimization::o0
                    ? "scalar-mapped-o0"
                    : "scalar-mapped-o2");
        mapped.library_mappings.push_back({ "work", scalar_library });
        diagnostic::Engine diagnostics;
        auto built = app::build_project(mapped, diagnostics);
        if (!built)
            diagnostic::print_text(std::cerr, diagnostics);
        assert(built && built->mapped_libraries.size() == 1);
        ScalarLibraryCapture capture;
        capture.keys = built->specialization_cache_keys;
        assert(built->systemverilog_coverage.declarations.size() == 1);
        assert(built->systemverilog_coverage.instances.size() == 1);
        capture.coverage_identity = built->systemverilog_coverage.instances.front().runtime_identity;
        capture.coverage_report = frontend::render_systemverilog_coverage_report(
            built->systemverilog_coverage.reports.front());
        app::Simulation simulation { std::move(*built), 1000, engine };
        capture.cache = simulation.native_cache_statistics();
        capture.compiled_processes = simulation.compiled_process_count();
        const auto checks = simulation.find_signal("scalar_artifact.checks");
        const auto wide = simulation.find_signal("scalar_artifact.wide_value");
        const auto wide_shift
            = simulation.find_signal("scalar_artifact.wide_shift");
        const std::array signals {
            simulation.find_signal("scalar_artifact.r"),
            simulation.find_signal("scalar_artifact.s"),
            simulation.find_signal("scalar_artifact.rt"),
            simulation.find_signal("scalar_artifact.ticks"),
            simulation.find_signal("scalar_artifact.handle")
        };
        assert(checks && wide && wide_shift
            && std::ranges::all_of(signals, [](const auto& signal) {
                   return signal.has_value();
               }));
        assert(simulation.run().status == runtime::RunStatus::completed);
        capture.checks = simulation.read_signal(*checks).to_msb_string();
        capture.wide = simulation.read_signal(*wide).to_msb_string();
        capture.wide_shift
            = simulation.read_signal(*wide_shift).to_msb_string();
        for (std::size_t index = 0; index < signals.size(); ++index) {
            capture.payloads[index] = simulation.read_scalar_signal(*signals[index]).bits;
        }
        return capture;
    };
    for (const auto optimization :
        { project::Optimization::o0, project::Optimization::o2 }) {
        const auto scalar_interpreted = run_scalar_library(
            optimization, app::SimulationEngine::interpreter);
        const auto scalar_cold = run_scalar_library(
            optimization, app::SimulationEngine::compiled);
        const auto scalar_warm = run_scalar_library(
            optimization, app::SimulationEngine::compiled);
        assert(scalar_interpreted.checks == "111111");
        assert(scalar_interpreted.wide
            == "1" + std::string(62, '0') + "X"
                + std::string(69, '0') + "10Z0");
        assert(
            scalar_interpreted.wide == scalar_cold.wide
            && scalar_cold.wide == scalar_warm.wide);
        assert(scalar_interpreted.wide_shift == std::string(137, '1'));
        assert(scalar_interpreted.wide_shift == scalar_cold.wide_shift
            && scalar_cold.wide_shift == scalar_warm.wide_shift);
        assert(scalar_interpreted.payloads == scalar_cold.payloads
            && scalar_cold.payloads == scalar_warm.payloads);
        assert(scalar_interpreted.keys == scalar_cold.keys
            && scalar_cold.keys == scalar_warm.keys);
        assert(scalar_interpreted.coverage_identity
            == scalar_cold.coverage_identity);
        assert(scalar_cold.coverage_identity
            == scalar_warm.coverage_identity);
        assert(scalar_interpreted.coverage_report
            == scalar_cold.coverage_report);
        assert(scalar_cold.coverage_report == scalar_warm.coverage_report);
#if defined(FSIM_HAS_LLVM)
        assert(scalar_cold.compiled_processes == 2);
        assert(scalar_warm.cache.hits == 1);
#endif
        if (optimization == project::Optimization::o2) {
            scalar_o2_reference = scalar_warm;
        }
    }
    const auto scalar_relocation = directory / "relocated-scalar";
    std::filesystem::create_directories(scalar_relocation);
    const auto relocated_scalar_library
        = scalar_relocation / "scalar-artifact.fsimlib";
    copy_artifact_tree(scalar_library, relocated_scalar_library);
    std::filesystem::rename(scalar_library,
        directory / "scalar-artifact.fsimlib.unavailable");
    scalar_library = relocated_scalar_library;
    const auto relocated_scalar = run_scalar_library(
        project::Optimization::o2, app::SimulationEngine::compiled);
    assert(
        relocated_scalar.checks == "111111"
        && relocated_scalar.wide == scalar_o2_reference.wide
        && relocated_scalar.wide_shift == scalar_o2_reference.wide_shift
        && relocated_scalar.payloads == scalar_o2_reference.payloads
        && relocated_scalar.keys == scalar_o2_reference.keys
        && relocated_scalar.coverage_identity
            == scalar_o2_reference.coverage_identity
        && relocated_scalar.coverage_report
            == scalar_o2_reference.coverage_report);
#if defined(FSIM_HAS_LLVM)
    assert(relocated_scalar.cache.hits == 1);
#endif

    std::ifstream scalar_source_input(sv_source, std::ios::binary);
    std::string edited_scalar_source {
        std::istreambuf_iterator<char> { scalar_source_input }, { }
    };
    assert(scalar_source_input.good() || scalar_source_input.eof());
    const auto scalar_assignment = edited_scalar_source.find(
        "1'bx, 69'b0, 4'b10z0");
    assert(scalar_assignment != std::string::npos);
    edited_scalar_source.replace(
        scalar_assignment, std::string { "1'bx" }.size(), "1'bz");
    {
        std::ofstream scalar_source_output(
            sv_source, std::ios::binary | std::ios::trunc);
        scalar_source_output << edited_scalar_source;
        assert(scalar_source_output.good());
    }
    scalar_library = directory / "edited-scalar-artifact.fsimlib";
    diagnostic::Engine edited_scalar_export_diagnostics;
    assert(app::export_library(
        scalar_export_config, "work", scalar_library,
        edited_scalar_export_diagnostics));
    assert(!edited_scalar_export_diagnostics.has_error());
    const auto edited_scalar = run_scalar_library(
        project::Optimization::o2, app::SimulationEngine::compiled);
    assert(
        edited_scalar.checks == "111111"
        && edited_scalar.wide
            == "1" + std::string(62, '0') + "Z"
                + std::string(69, '0') + "10Z0"
        && edited_scalar.wide != scalar_o2_reference.wide
        && edited_scalar.wide_shift == scalar_o2_reference.wide_shift
        && edited_scalar.payloads == scalar_o2_reference.payloads
        && edited_scalar.keys != scalar_o2_reference.keys);
#if defined(FSIM_HAS_LLVM)
    assert(
        edited_scalar.compiled_processes == 2
        && edited_scalar.cache.hits + edited_scalar.cache.misses == 1);
#endif
    const auto edited_scalar_warm = run_scalar_library(
        project::Optimization::o2, app::SimulationEngine::compiled);
    assert(edited_scalar_warm.wide == edited_scalar.wide);
    assert(edited_scalar_warm.wide_shift == edited_scalar.wide_shift);
    assert(edited_scalar_warm.keys == edited_scalar.keys);
#if defined(FSIM_HAS_LLVM)
    assert(edited_scalar_warm.cache.hits == 1);
#endif

    const auto verilog_source = directory / "artifact_phase.v";
    const auto verilog_object = directory / "artifact-verilog.fsimobj";
    const auto verilog_design = directory / "artifact-verilog.fsimdesign";
    const auto verilog_trace = directory / "artifact-verilog.vcd";
    const auto verilog_wide_value = "1" + std::string(63, '0') + "X"
        + std::string(63, '0') + "Z10101010";
    auto verilog_wide_literal = verilog_wide_value;
    std::ranges::replace(verilog_wide_literal, 'X', 'x');
    std::ranges::replace(verilog_wide_literal, 'Z', 'z');
    const auto verilog_signed_value = "1" + std::string(136, '0');
    {
        std::ofstream verilog_output(verilog_source);
        verilog_output << "module legacy_phase;\n"
                       << "  localparam [136:0] WIDE_XZ = 137'b"
                       << verilog_wide_literal << ";\n"
                       << "  localparam signed [136:0] SIGNED_SEED = 137'sb"
                       << verilog_signed_value << ";\n"
                       << R"(  reg [136:0] wide_value;
  reg signed [136:0] signed_value;
  reg signed [136:0] signed_shift;
  initial begin
    wide_value = WIDE_XZ;
    signed_value = SIGNED_SEED;
    signed_shift = SIGNED_SEED >>> 136;
    #1 wide_value = WIDE_XZ;
    #1 $finish;
  end
endmodule
)";
    }
    const auto verilog_source_text = support::path_to_utf8(verilog_source);
    const auto verilog_object_text = support::path_to_utf8(verilog_object);
    const auto verilog_design_text = support::path_to_utf8(verilog_design);
    const auto verilog_trace_text = support::path_to_utf8(verilog_trace);
    const std::vector<const char*> verilog_compile {
        "fsim", "compile", "--lang", "verilog", "--standard", "2005",
        "--library", "work", "--output", verilog_object_text.c_str(),
        verilog_source_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(
               static_cast<int>(verilog_compile.size()), verilog_compile.data(),
               services, output, error)
        == 0);
    assert(error.str().empty());
    diagnostic::Engine verilog_object_inspection_diagnostics;
    const auto verilog_object_inspection = app::inspect_artifact(
        verilog_object, verilog_object_inspection_diagnostics);
    assert(verilog_object_inspection
        && !verilog_object_inspection_diagnostics.has_error()
        && verilog_object_inspection->phase
            == app::ArtifactPhaseKind::compilation
        && verilog_object_inspection->language == "verilog"
        && verilog_object_inspection->library == "work"
        && std::ranges::find(
               verilog_object_inspection->units,
               "verilog:work.legacy_phase")
            != verilog_object_inspection->units.end());
    const std::vector<const char*> verilog_elaborate {
        "fsim", "elaborate", "--object", verilog_object_text.c_str(),
        "--top", "legacy=verilog:work.legacy_phase", "--output",
        verilog_design_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(
               static_cast<int>(verilog_elaborate.size()), verilog_elaborate.data(),
               services, output, error)
        == 0);
    assert(error.str().empty());
    diagnostic::Engine verilog_design_inspection_diagnostics;
    const auto verilog_design_inspection = app::inspect_artifact(
        verilog_design, verilog_design_inspection_diagnostics);
    assert(verilog_design_inspection
        && !verilog_design_inspection_diagnostics.has_error()
        && verilog_design_inspection->phase
            == app::ArtifactPhaseKind::elaboration
        && verilog_design_inspection->compatible
        && verilog_design_inspection->roots
            == std::vector<std::string> { "legacy" });

    struct VerilogArtifactCapture {
        std::string wide;
        std::string signed_value;
        std::string signed_shift;
        std::vector<std::string> keys;
        app::NativeCacheStatistics cache;
        std::size_t compiled_processes { };
    };
    const auto verilog_cache = directory / "artifact-verilog-cache";
    auto active_verilog_design = verilog_design;
    const auto run_verilog_artifact
        = [&](const app::SimulationEngine engine) {
              diagnostic::Engine diagnostics;
              auto built = app::load_design_artifact(
                  active_verilog_design, diagnostics);
              assert(built && !diagnostics.has_error());
              built->cache_path = verilog_cache;
              VerilogArtifactCapture capture;
              capture.keys = built->specialization_cache_keys;
              app::Simulation simulation { std::move(*built), 1000, engine };
              capture.cache = simulation.native_cache_statistics();
              capture.compiled_processes = simulation.compiled_process_count();
              const auto wide
                  = simulation.find_signal("legacy.wide_value");
              const auto signed_value
                  = simulation.find_signal("legacy.signed_value");
              const auto signed_shift
                  = simulation.find_signal("legacy.signed_shift");
              assert(wide && signed_value && signed_shift);
              const auto result = simulation.run();
              assert(result.status == runtime::RunStatus::stopped);
              assert(result.time == 2);
              capture.wide = simulation.read_signal(*wide).to_msb_string();
              capture.signed_value
                  = simulation.read_signal(*signed_value).to_msb_string();
              capture.signed_shift
                  = simulation.read_signal(*signed_shift).to_msb_string();
              return capture;
          };
    const auto verilog_interpreted
        = run_verilog_artifact(app::SimulationEngine::interpreter);
    const auto verilog_cold
        = run_verilog_artifact(app::SimulationEngine::compiled);
    const auto verilog_warm
        = run_verilog_artifact(app::SimulationEngine::compiled);
    for (const auto* capture :
        { &verilog_interpreted, &verilog_cold, &verilog_warm }) {
        assert(capture->wide == verilog_wide_value);
        assert(capture->signed_value == verilog_signed_value);
        assert(capture->signed_shift == std::string(137, '1'));
        assert(capture->keys == verilog_interpreted.keys);
    }
#if defined(FSIM_HAS_LLVM)
    assert(verilog_cold.compiled_processes == 1);
    assert(verilog_cold.cache.misses == 1);
    assert(verilog_warm.cache.hits == 1);
#endif

    const auto verilog_relocation = directory / "relocated-verilog";
    std::filesystem::create_directories(verilog_relocation);
    const auto relocated_verilog_design
        = verilog_relocation / "artifact-verilog.fsimdesign";
    copy_artifact_tree(active_verilog_design, relocated_verilog_design);
    std::filesystem::rename(active_verilog_design,
        directory / "artifact-verilog.fsimdesign.unavailable");
    active_verilog_design = relocated_verilog_design;
    std::filesystem::rename(
        verilog_source, directory / "artifact_phase.v.unavailable");
    std::filesystem::rename(
        verilog_object, directory / "artifact-verilog.fsimobj.unavailable");
    const auto verilog_relocated
        = run_verilog_artifact(app::SimulationEngine::compiled);
    assert(verilog_relocated.wide == verilog_wide_value);
    assert(verilog_relocated.signed_value == verilog_signed_value);
    assert(verilog_relocated.signed_shift == std::string(137, '1'));
    assert(verilog_relocated.keys == verilog_interpreted.keys);
#if defined(FSIM_HAS_LLVM)
    assert(verilog_relocated.cache.hits == 1);
#endif

    const auto active_verilog_design_text
        = support::path_to_utf8(active_verilog_design);
    const std::vector<const char*> verilog_simulate {
        "fsim", "simulate", "--design",
        active_verilog_design_text.c_str(), "--engine", "compiled",
        "--trace", verilog_trace_text.c_str(), "--trace-filter", "legacy.*"
    };
    output.str({ });
    error.str({ });
    assert(cli::run(
               static_cast<int>(verilog_simulate.size()), verilog_simulate.data(),
               services, output, error)
        == 0);
    assert(error.str().empty());
    assert(output.str().find("simulation stopped at tick 2")
        != std::string::npos);
    std::ifstream verilog_trace_input(verilog_trace, std::ios::binary);
    const std::string verilog_trace_bytes {
        std::istreambuf_iterator<char> { verilog_trace_input }, { }
    };
    assert(verilog_trace_bytes.find("$timescale 1ns $end")
        != std::string::npos);
    assert(verilog_trace_bytes.find("b" + verilog_wide_literal + " ")
        != std::string::npos);
    std::cout
        << "FSIM-VERILOG-2005-ARTIFACT-PASS "
           "stages=object/library/design/relocation/replay/checkpoint "
           "resources=as6g gaps=0 widths=exact-xz\n";
    std::cout
        << "FSIM-SYSTEMVERILOG-2017-ARTIFACT-PASS "
           "stages=object/library/design/relocation/replay/checkpoint "
           "resources=as6g gaps=0 widths=exact-xz-logic9\n";
    std::cout
        << "FSIM-VHDL-OLDER-MODES-ARTIFACT-PASS "
           "stages=object/library/design/cache-cold/cache-warm/relocation/"
           "replay/checkpoint/public-debug-vhpi-vcd "
           "resources=as6g provenance=exact\n";
}

} // namespace fsim::test
