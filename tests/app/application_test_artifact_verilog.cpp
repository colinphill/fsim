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

void ApplicationTestFixture::test_verilog_artifact_matrix(
    fsim::cli::Services& services)
{
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
    std::ostringstream output;
    std::ostringstream error;
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
        "--compatibility", "sizing",
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
    diagnostic::Engine verilog_object_metadata_diagnostics;
    const auto verilog_object_metadata = artifact::load_object_metadata(
        verilog_object, verilog_object_metadata_diagnostics);
    assert(verilog_object_metadata
        && !verilog_object_metadata_diagnostics.has_error()
        && verilog_object_metadata->standard == "2005"
        && verilog_object_metadata->compatibility_profile == "sizing"
        && std::ranges::all_of(
            verilog_object_metadata->units, [](const auto& unit) {
                return unit.standard == "2005"
                    && unit.compatibility_profile == "sizing";
            }));
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
    diagnostic::Engine verilog_design_metadata_diagnostics;
    const auto verilog_design_metadata = artifact::load_design_metadata(
        verilog_design, verilog_design_metadata_diagnostics);
    assert(verilog_design_metadata
        && !verilog_design_metadata_diagnostics.has_error()
        && verilog_design_metadata->verilog_unit_provenance.size() == 1
        && verilog_design_metadata->verilog_unit_provenance.front().language
            == "verilog"
        && verilog_design_metadata->verilog_unit_provenance.front().standard
            == "2005"
        && verilog_design_metadata->verilog_unit_provenance.front()
               .compatibility_profile
            == "sizing");

    const auto implicit_alias_design =
        directory / "artifact-verilog-implicit-alias.fsimdesign";
    const auto implicit_alias_design_text =
        support::path_to_utf8(implicit_alias_design);
    const std::vector<const char*> implicit_alias_elaborate {
        "fsim", "elaborate", "--object", verilog_object_text.c_str(),
        "--top", "legacy_phase", "--output",
        implicit_alias_design_text.c_str()
    };
    output.str({ });
    error.str({ });
    assert(cli::run(
               static_cast<int>(implicit_alias_elaborate.size()),
               implicit_alias_elaborate.data(), services, output, error)
        == 0);
    assert(error.str().empty());
    diagnostic::Engine implicit_alias_diagnostics;
    const auto implicit_alias_metadata = artifact::load_design_metadata(
        implicit_alias_design, implicit_alias_diagnostics);
    assert(implicit_alias_metadata
        && !implicit_alias_diagnostics.has_error()
        && implicit_alias_metadata->roots.size() == 1
        && implicit_alias_metadata->roots.front().alias == "legacy_phase");
    auto implicit_alias_project = app::load_design_artifact(
        implicit_alias_design, implicit_alias_diagnostics);
    assert(implicit_alias_project
        && !implicit_alias_diagnostics.has_error()
        && implicit_alias_project->systemverilog_uvm_checkpoint
        && implicit_alias_project->systemverilog_uvm_checkpoint
               ->provenance.roots
            == std::vector<std::string> { "legacy_phase" });

    struct VerilogArtifactCapture {
        std::string wide;
        std::string signed_value;
        std::string signed_shift;
        std::vector<std::string> keys;
        std::vector<std::string> provenance;
        std::string checkpoint;
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
              const auto public_provenance =
                  app::verilog_scope_provenance(*built);
              assert(public_provenance.size() == 1);
              const auto& owner = public_provenance.front();
              assert(owner.path == "legacy"
                  && owner.semantic_unit == "work::legacy_phase"
                  && owner.language == semantic::Language::verilog
                  && owner.standard == "verilog-2005"
                  && owner.compatibility_profile == "sizing"
                  && !std::filesystem::path(owner.source_path).is_absolute());
              built->cache_path = verilog_cache;
              VerilogArtifactCapture capture;
              capture.keys = built->specialization_cache_keys;
              capture.provenance = {
                  owner.path, owner.semantic_unit,
                  std::to_string(owner.unit.value()),
                  std::to_string(owner.source.value()), owner.source_path,
                  owner.standard, owner.compatibility_profile
              };
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
              const auto checkpoint = simulation.capture_uvm_checkpoint();
              const auto checkpoint_bytes = checkpoint
                  ? app::serialize_systemverilog_uvm_state(
                        checkpoint.artifact, diagnostics)
                  : std::nullopt;
              const auto restored_checkpoint = checkpoint_bytes
                  ? app::deserialize_systemverilog_uvm_state(
                        *checkpoint_bytes, "verilog-artifact-checkpoint",
                        diagnostics)
                  : std::nullopt;
              assert(checkpoint && checkpoint_bytes && restored_checkpoint
                  && *restored_checkpoint == checkpoint.artifact
                  && !diagnostics.has_error());
              capture.checkpoint = *checkpoint_bytes;
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
        assert(capture->provenance == verilog_interpreted.provenance);
        assert(capture->checkpoint == verilog_interpreted.checkpoint);
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
    const auto verilog_relocated_interpreted
        = run_verilog_artifact(app::SimulationEngine::interpreter);
    const auto verilog_relocated
        = run_verilog_artifact(app::SimulationEngine::compiled);
    assert(verilog_relocated_interpreted.wide == verilog_wide_value);
    assert(verilog_relocated_interpreted.signed_value
        == verilog_signed_value);
    assert(verilog_relocated_interpreted.signed_shift
        == std::string(137, '1'));
    assert(verilog_relocated_interpreted.keys == verilog_interpreted.keys);
    assert(verilog_relocated_interpreted.provenance
        == verilog_interpreted.provenance);
    assert(verilog_relocated_interpreted.checkpoint
        == verilog_interpreted.checkpoint);
    assert(verilog_relocated.wide == verilog_wide_value);
    assert(verilog_relocated.signed_value == verilog_signed_value);
    assert(verilog_relocated.signed_shift == std::string(137, '1'));
    assert(verilog_relocated.keys == verilog_interpreted.keys);
    assert(verilog_relocated.provenance == verilog_interpreted.provenance);
    assert(verilog_relocated.checkpoint == verilog_interpreted.checkpoint);
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
    assert(verilog_trace_bytes.find(
               "$comment fsim-verilog-scope path=legacy ")
        != std::string::npos);
    assert(verilog_trace_bytes.find("standard=verilog-2005")
        != std::string::npos);
    assert(verilog_trace_bytes.find("profile=sizing")
        != std::string::npos);

    struct DurableMode {
        const char* language;
        const char* standard;
        const char* canonical_standard;
        const char* module;
        const char* extension;
    };
    const std::array durable_modes {
        DurableMode { "verilog", "1995", "verilog-1995",
            "durable_v1995", ".v" },
        DurableMode { "verilog", "2001", "verilog-2001",
            "durable_v2001", ".v" },
        DurableMode { "verilog", "2001-noconfig",
            "verilog-2001-noconfig", "durable_v2001_noconfig", ".v" },
        DurableMode { "systemverilog", "2005", "systemverilog-2005",
            "durable_sv2005", ".sv" },
        DurableMode { "systemverilog", "2009", "systemverilog-2009",
            "durable_sv2009", ".sv" },
        DurableMode { "systemverilog", "2012", "systemverilog-2012",
            "durable_sv2012", ".sv" }
    };
    struct DurableCapture {
        std::string value;
        std::vector<std::string> provenance;
        std::vector<std::string> keys;
        std::string checkpoint;
        app::NativeCacheStatistics cache;
    };
    for (const auto& mode : durable_modes) {
        const auto mode_directory = directory / mode.module;
        std::filesystem::create_directories(mode_directory);
        const auto mode_source = mode_directory
            / (std::string { mode.module } + mode.extension);
        auto mode_object = mode_directory / "unit.fsimobj";
        auto mode_design = mode_directory / "design.fsimdesign";
        {
            std::ofstream source_output(mode_source);
            source_output << "module " << mode.module << ";\n"
                          << "  reg [7:0] value;\n"
                          << "  initial begin\n"
                          << "    value = 8'ha5;\n"
                          << "    #1 $finish;\n"
                          << "  end\n"
                          << "endmodule\n";
        }
        const auto source_text = support::path_to_utf8(mode_source);
        const auto object_text = support::path_to_utf8(mode_object);
        const std::array<const char*, 13> compile {
            "fsim", "compile", "--lang", mode.language, "--standard",
            mode.standard, "--compatibility", "sizing", "--library", "work",
            "--output", object_text.c_str(), source_text.c_str()
        };
        output.str({ });
        error.str({ });
        assert(cli::run(
                   static_cast<int>(compile.size()), compile.data(), services,
                   output, error)
            == 0);
        assert(error.str().empty());
        diagnostic::Engine object_diagnostics;
        const auto object_metadata = artifact::load_object_metadata(
            mode_object, object_diagnostics);
        assert(object_metadata && !object_diagnostics.has_error()
            && object_metadata->standard == mode.standard
            && object_metadata->compatibility_profile == "sizing");
        std::filesystem::rename(
            mode_source, mode_directory / "source.producer-unavailable");
        const auto mode_design_text = support::path_to_utf8(mode_design);
        const auto top = std::string { "root=" }
            + (std::string { mode.language } == "verilog"
                    ? "verilog:work." : "sv:work.")
            + mode.module;
        const std::array<const char*, 9> mode_elaborate {
            "fsim", "elaborate", "--object", object_text.c_str(), "--top",
            top.c_str(), "--output", mode_design_text.c_str(), nullptr
        };
        output.str({ });
        error.str({ });
        assert(cli::run(
                   8, mode_elaborate.data(), services, output, error)
            == 0);
        assert(error.str().empty());
        diagnostic::Engine design_diagnostics;
        const auto mode_design_metadata = artifact::load_design_metadata(
            mode_design, design_diagnostics);
        assert(mode_design_metadata && !design_diagnostics.has_error()
            && mode_design_metadata->verilog_unit_provenance.size() == 1
            && mode_design_metadata->verilog_unit_provenance.front().standard
                == mode.standard
            && mode_design_metadata->verilog_unit_provenance.front()
                   .compatibility_profile
                == "sizing");
        std::filesystem::rename(
            mode_object, mode_directory / "object.producer-unavailable");
        const auto cache = mode_directory / "native-cache";
        const auto run_mode = [&](const app::SimulationEngine engine) {
            diagnostic::Engine diagnostics;
            auto built = app::load_design_artifact(mode_design, diagnostics);
            assert(built && !diagnostics.has_error());
            const auto public_provenance =
                app::verilog_scope_provenance(*built);
            assert(public_provenance.size() == 1);
            const auto& owner = public_provenance.front();
            assert(owner.path == "root"
                && owner.semantic_unit
                    == std::string { "work::" } + mode.module
                && owner.standard == mode.canonical_standard
                && owner.compatibility_profile == "sizing"
                && !std::filesystem::path(owner.source_path).is_absolute());
            DurableCapture capture;
            capture.provenance = {
                owner.path, owner.semantic_unit,
                std::to_string(owner.unit.value()),
                std::to_string(owner.source.value()), owner.source_path,
                owner.standard, owner.compatibility_profile
            };
            capture.keys = built->specialization_cache_keys;
            built->cache_path = cache;
            app::Simulation simulation { std::move(*built), 1000, engine };
            capture.cache = simulation.native_cache_statistics();
            const auto value = simulation.find_signal("root.value");
            assert(value);
            const auto result = simulation.run();
            assert(result.status == runtime::RunStatus::stopped
                && result.time == 1);
            capture.value = simulation.read_signal(*value).to_msb_string();
            const auto checkpoint = simulation.capture_uvm_checkpoint();
            const auto encoded = checkpoint
                ? app::serialize_systemverilog_uvm_state(
                      checkpoint.artifact, diagnostics)
                : std::nullopt;
            const auto decoded = encoded
                ? app::deserialize_systemverilog_uvm_state(
                      *encoded, "durable-mode-checkpoint", diagnostics)
                : std::nullopt;
            assert(checkpoint && encoded && decoded
                && *decoded == checkpoint.artifact
                && !diagnostics.has_error());
            capture.checkpoint = *encoded;
            return capture;
        };
        const auto mode_interpreted =
            run_mode(app::SimulationEngine::interpreter);
        const auto cold = run_mode(app::SimulationEngine::compiled);
        const auto warm = run_mode(app::SimulationEngine::compiled);
        assert(mode_interpreted.value == "10100101"
            && mode_interpreted.value == cold.value
            && cold.value == warm.value
            && mode_interpreted.provenance == cold.provenance
            && cold.provenance == warm.provenance
            && mode_interpreted.keys == cold.keys && cold.keys == warm.keys
            && mode_interpreted.checkpoint == cold.checkpoint
            && cold.checkpoint == warm.checkpoint);
#if defined(FSIM_HAS_LLVM)
        assert(cold.cache.misses == 1 && warm.cache.hits == 1);
#endif
        const auto relocation = mode_directory / "relocated";
        std::filesystem::create_directories(relocation);
        const auto relocated_mode_design = relocation / "design.fsimdesign";
        copy_artifact_tree(mode_design, relocated_mode_design);
        std::filesystem::rename(
            mode_design, mode_directory / "design.producer-unavailable");
        mode_design = relocated_mode_design;
        const auto relocated_interpreted =
            run_mode(app::SimulationEngine::interpreter);
        const auto relocated_compiled =
            run_mode(app::SimulationEngine::compiled);
        assert(relocated_interpreted.value == mode_interpreted.value
            && relocated_compiled.value == mode_interpreted.value
            && relocated_interpreted.provenance
                == mode_interpreted.provenance
            && relocated_compiled.provenance == mode_interpreted.provenance
            && relocated_interpreted.keys == mode_interpreted.keys
            && relocated_compiled.keys == mode_interpreted.keys
            && relocated_interpreted.checkpoint
                == mode_interpreted.checkpoint
            && relocated_compiled.checkpoint
                == mode_interpreted.checkpoint);
#if defined(FSIM_HAS_LLVM)
        assert(relocated_compiled.cache.hits == 1);
#endif
    }
    std::cout
        << "FSIM-OLDER-STANDARD-ARTIFACT-MATRIX-PASS modes=6 "
           "stages=compile/object/elaborate/design/simulate/cache-cold/"
           "cache-warm/relocation/checkpoint-replay engines=interpreter-llvm "
           "profile=sizing producers=hidden\n";
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
