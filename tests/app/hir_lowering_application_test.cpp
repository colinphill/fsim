// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include "../../src/app/application_internal.hpp"
#include "../../src/elaboration/elaboration_targets.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write_source(
    const std::filesystem::path& path,
    const std::string_view contents)
{
    std::ofstream output { path, std::ios::binary | std::ios::trunc };
    output << contents;
    assert(output.good());
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-hir-lowering-" + std::to_string(nonce)),
    };
    std::filesystem::create_directories(directory.path);
    const auto systemverilog = directory.path / "direct.sv";
    const auto fast_systemverilog = directory.path / "fast.sv";
    const auto slow_systemverilog = directory.path / "slow.sv";
    const auto vhdl = directory.path / "direct.vhd";
    write_source(
        systemverilog,
        "module direct_leaf #(parameter int W = 2) "
        "(input logic [W-1:0] i, output logic o); "
        "endmodule "
        "module direct_sv #(parameter int SELECT = 1) "
        "(input logic [1:0] a, output logic q, "
        "output logic r); "
        "import \"DPI-C\" pure dpi_identity_c = function int "
        "dpi_identity(input int value); "
        "direct_leaf #(.W(2)) leaf(.i(a), .o()); "
        "if (SELECT) begin : compiled_gen "
        "direct_leaf #(.W(2)) generated(.i(a), .o()); "
        "if (SELECT == 1) begin : nested_gen "
        "direct_leaf #(.W(2)) nested_generated(.i(a), .o()); "
        "end "
        "end "
        "if (!SELECT) begin : rejected_gen "
        "direct_leaf #(.W(2)) rejected(.i(a), .o()); "
        "end "
        "initial begin "
        "q = dpi_identity(a[0]); "
        "q = a[1] ? {2{a[0]}} : a[1:0]; "
        "repeat (2) q = 1'b1; "
        "for (int i = 0; i < 2; i++) q = 1'b1; "
        "while (1'b0) q = 1'b0; "
        "end "
        "always @* r = q; endmodule "
        "module duplicate_top; duplicate_leaf selected(); endmodule "
        "config duplicate_configuration; "
        "design work.duplicate_top; "
        "default liblist slow fast; "
        "endconfig : duplicate_configuration\n");
    write_source(fast_systemverilog,
        "module duplicate_leaf; endmodule\n");
    write_source(slow_systemverilog,
        "module duplicate_leaf; endmodule\n");
    write_source(
        vhdl,
        R"(
entity direct_vhdl_leaf is
  generic (width : positive := 2);
  port (i : in bit; o : out bit);
end entity;
architecture rtl of direct_vhdl_leaf is
begin
end architecture;

entity direct_vhdl is
  generic (enabled : boolean := true);
  port (
    a : in bit;
    lanes : in bit_vector(1 downto 0) := "00";
    q : out bit
  );
end entity;
architecture rtl of direct_vhdl is
begin
  selected_gen : if enabled generate
    generated_leaf : entity work.direct_vhdl_leaf(rtl)
      generic map (width => 2)
      port map (i => a, o => open);
  end generate;
  rejected_gen : if enabled = false generate
    rejected_leaf : entity work.direct_vhdl_leaf(rtl)
      generic map (width => 2)
      port map (i => a, o => open);
  end generate;
  leaf : entity work.direct_vhdl_leaf(rtl)
    generic map (width => 2)
    port map (i => a, o => open);
  drive : process(a)
  begin
    q <= a;
  end process;
end architecture;
)");

    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "direct-hir-lowering";
    config.project.tops = {
        { "sv:work.direct_sv", "sv_root" },
        { "vhdl:work.direct_vhdl(rtl)", "vhdl_root" },
    };
    fsim::project::SourceSet sv_sources;
    sv_sources.language = fsim::project::Language::system_verilog;
    sv_sources.standard = "2017";
    sv_sources.library = "work";
    sv_sources.files = { systemverilog };
    config.source_sets.push_back(std::move(sv_sources));
    fsim::project::SourceSet fast_sources;
    fast_sources.language = fsim::project::Language::system_verilog;
    fast_sources.standard = "2017";
    fast_sources.library = "fast";
    fast_sources.files = { fast_systemverilog };
    config.source_sets.push_back(std::move(fast_sources));
    fsim::project::SourceSet slow_sources;
    slow_sources.language = fsim::project::Language::system_verilog;
    slow_sources.standard = "2017";
    slow_sources.library = "slow";
    slow_sources.files = { slow_systemverilog };
    config.source_sets.push_back(std::move(slow_sources));
    fsim::project::SourceSet vhdl_sources;
    vhdl_sources.language = fsim::project::Language::vhdl;
    vhdl_sources.standard = "2008";
    vhdl_sources.library = "work";
    vhdl_sources.files = { vhdl };
    config.source_sets.push_back(std::move(vhdl_sources));

    fsim::diagnostic::Engine diagnostics;
    auto checked = fsim::app::check_project(config, diagnostics);
    if (!checked) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(checked);
    const auto source_mappings
        = fsim::app::application_detail::compiled_cache_source_mappings(
            *checked, config.base_directory, diagnostics);
    assert(source_mappings);
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        *checked, *source_mappings, diagnostics));
    const auto bytes = fsim::app::serialize_compiled_hir_bundle(
        *checked, diagnostics);
    if (!bytes) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(bytes && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_compiled_hir_bundle(
        *bytes, "direct-hir-bundle", diagnostics);
    assert(restored && restored->valid() && !diagnostics.has_error());
    const auto restored_configuration = std::ranges::find(
        restored->systemverilog_units(),
        std::string { "duplicate_configuration" },
        &fsim::semantic::sv::Unit::name);
    assert(restored_configuration != restored->systemverilog_units().end());
    assert(restored_configuration->configuration);
    assert(restored_configuration->configuration->designs.front().target);
    assert(restored_configuration->configuration->default_liblist
        == std::vector<std::string>({ "slow", "fast" }));

    using fsim::elaboration::elaboration_detail::CompiledActualKind;
    using fsim::elaboration::elaboration_detail::CompiledPortMode;
    using fsim::elaboration::elaboration_detail::instance_parameters;
    using fsim::elaboration::elaboration_detail::instance_ports;
    using fsim::elaboration::elaboration_detail::unit_ports;
    const auto systemverilog_unit
        = fsim::elaboration::elaboration_detail::choose_top_unit(
            *restored, "sv:work.direct_sv");
    assert(systemverilog_unit);
    assert(systemverilog_unit->systemverilog != nullptr);
    assert(systemverilog_unit->systemverilog->dpi_declarations.size() == 1U);
    const auto& decoded_dpi
        = systemverilog_unit->systemverilog->dpi_declarations.front();
    assert(decoded_dpi.direction
        == fsim::semantic::sv::DpiDirection::import);
    assert(decoded_dpi.owner_kind
        == fsim::semantic::sv::DpiOwnerKind::design_unit);
    assert(decoded_dpi.qualifier
        == fsim::semantic::sv::DpiQualifier::pure);
    assert(decoded_dpi.callable_kind
        == fsim::semantic::sv::DpiCallableKind::function);
    assert(decoded_dpi.systemverilog_name == "dpi_identity");
    assert(decoded_dpi.c_identifier
        == std::optional<std::string> { "dpi_identity_c" });
    assert(decoded_dpi.linkage_name == "dpi_identity_c");
    assert(decoded_dpi.validated);
    assert(decoded_dpi.formals.size() == 1U);
    assert(decoded_dpi.formals.front().name == "value");
    assert(decoded_dpi.source.valid() && decoded_dpi.origin.valid());
    const auto decoded_specialization
        = fsim::semantic::make_specialized_hir_unit(
            *restored, systemverilog_unit->identity->id,
            std::array<fsim::semantic::SpecializedHirNamedIdentity, 1> {
                { { "SELECT", "1" } } },
            std::span<const fsim::semantic::SpecializedHirNamedIdentity> { });
    assert(decoded_specialization);
    const auto decoded_active_instances
        = decoded_specialization->active_instances();
    const auto has_active_systemverilog_instance
        = [&](const std::string_view name) {
              return std::ranges::any_of(decoded_active_instances,
                  [&](const auto instance) {
                      return instance.systemverilog != nullptr
                          && instance.systemverilog->name == name;
                  });
          };
    assert(has_active_systemverilog_instance("leaf"));
    assert(has_active_systemverilog_instance("generated"));
    assert(has_active_systemverilog_instance("nested_generated"));
    assert(!has_active_systemverilog_instance("rejected"));
    assert(decoded_specialization->selected_generates().size() == 2U);
    const auto systemverilog_ports
        = unit_ports(*restored, *systemverilog_unit);
    assert(systemverilog_ports && systemverilog_ports->size() == 3U);
    assert((*systemverilog_ports)[0]
        && (*systemverilog_ports)[0].name() == "a"
        && (*systemverilog_ports)[0].mode() == CompiledPortMode::input
        && (*systemverilog_ports)[0].declaration().valid()
        && (*systemverilog_ports)[0].source().valid()
        && (*systemverilog_ports)[0].origin().valid()
        && (*systemverilog_ports)[0].systemverilog_type() != nullptr
        && (*systemverilog_ports)[0].systemverilog_packed_range() != nullptr
        && (*systemverilog_ports)[0].vhdl_subtype() == nullptr);
    assert((*systemverilog_ports)[1].name() == "q"
        && (*systemverilog_ports)[1].mode() == CompiledPortMode::output);
    assert(systemverilog_unit->systemverilog != nullptr
        && systemverilog_unit->systemverilog->instances.size() == 1U);
    const auto systemverilog_instance = restored->find_instance(
        systemverilog_unit->systemverilog->instances.front());
    assert(systemverilog_instance
        && systemverilog_instance->systemverilog != nullptr);
    const auto systemverilog_parameters = instance_parameters(
        *systemverilog_instance);
    const auto systemverilog_actuals = instance_ports(
        *systemverilog_instance);
    assert(systemverilog_parameters.size() == 1U
        && systemverilog_parameters.front()
        && systemverilog_parameters.front().formal() == "W"
        && systemverilog_parameters.front().kind()
            == CompiledActualKind::expression
        && systemverilog_parameters.front().expression()
        && systemverilog_parameters.front().source().valid());
    assert(systemverilog_actuals.size() == 2U
        && systemverilog_actuals[0].formal() == "i"
        && systemverilog_actuals[0].kind()
            == CompiledActualKind::expression
        && systemverilog_actuals[0].expression()
        && systemverilog_actuals[1].formal() == "o"
        && systemverilog_actuals[1].kind() == CompiledActualKind::open
        && !systemverilog_actuals[1].expression());

    const auto vhdl_architecture
        = fsim::elaboration::elaboration_detail::choose_top_unit(
            *restored, "vhdl:work.direct_vhdl(rtl)");
    assert(vhdl_architecture);
    const auto decoded_vhdl_specialization
        = fsim::semantic::make_specialized_hir_unit(
            *restored, vhdl_architecture->identity->id,
            std::array<fsim::semantic::SpecializedHirNamedIdentity, 1> {
                { { "enabled", "1" } } },
            std::span<const fsim::semantic::SpecializedHirNamedIdentity> { });
    assert(decoded_vhdl_specialization);
    const auto decoded_active_vhdl_instances
        = decoded_vhdl_specialization->active_instances();
    const auto has_active_vhdl_instance
        = [&](const std::string_view name) {
              return std::ranges::any_of(decoded_active_vhdl_instances,
                  [&](const auto instance) {
                      return instance.vhdl != nullptr
                          && instance.vhdl->name == name;
                  });
          };
    assert(has_active_vhdl_instance("leaf"));
    assert(has_active_vhdl_instance("generated_leaf"));
    assert(!has_active_vhdl_instance("rejected_leaf"));
    const auto decoded_vhdl_default_specialization
        = fsim::semantic::make_specialized_hir_unit(
            *restored, vhdl_architecture->identity->id,
            std::span<const fsim::semantic::SpecializedHirActualIdentity> { });
    assert(decoded_vhdl_default_specialization);
    const auto decoded_default_active_vhdl_instances
        = decoded_vhdl_default_specialization->active_instances();
    const auto has_default_active_vhdl_instance
        = [&](const std::string_view name) {
              return std::ranges::any_of(
                  decoded_default_active_vhdl_instances,
                  [&](const auto instance) {
                      return instance.vhdl != nullptr
                          && instance.vhdl->name == name;
                  });
          };
    assert(has_default_active_vhdl_instance("leaf"));
    assert(has_default_active_vhdl_instance("generated_leaf"));
    assert(!has_default_active_vhdl_instance("rejected_leaf"));
    const auto vhdl_ports = unit_ports(*restored, *vhdl_architecture);
    assert(vhdl_ports && vhdl_ports->size() == 3U);
    assert((*vhdl_ports)[0]
        && (*vhdl_ports)[0].name() == "a"
        && (*vhdl_ports)[0].mode() == CompiledPortMode::input
        && (*vhdl_ports)[0].declaration().valid()
        && (*vhdl_ports)[0].source().valid()
        && (*vhdl_ports)[0].origin().valid()
        && (*vhdl_ports)[0].vhdl_subtype() != nullptr
        && (*vhdl_ports)[0].systemverilog_type() == nullptr);
    assert((*vhdl_ports)[1].name() == "lanes"
        && (*vhdl_ports)[1].mode() == CompiledPortMode::input
        && (*vhdl_ports)[1].default_expression()
        && !(*vhdl_ports)[1].vhdl_constraints().empty());
    assert((*vhdl_ports)[2].name() == "q"
        && (*vhdl_ports)[2].mode() == CompiledPortMode::output);
    assert(vhdl_architecture->vhdl != nullptr
        && vhdl_architecture->vhdl->instances.size() == 1U);
    const auto vhdl_instance = restored->find_instance(
        vhdl_architecture->vhdl->instances.front());
    assert(vhdl_instance && vhdl_instance->vhdl != nullptr);
    const auto vhdl_generics = instance_parameters(*vhdl_instance);
    const auto vhdl_actuals = instance_ports(*vhdl_instance);
    assert(vhdl_generics.size() == 1U
        && vhdl_generics.front().formal() == "width"
        && vhdl_generics.front().kind()
            == CompiledActualKind::expression
        && vhdl_generics.front().expression()
        && vhdl_generics.front().source().valid());
    assert(vhdl_actuals.size() == 2U
        && vhdl_actuals[0].formal() == "i"
        && vhdl_actuals[0].kind() == CompiledActualKind::expression
        && vhdl_actuals[0].expression()
        && vhdl_actuals[1].formal() == "o"
        && vhdl_actuals[1].kind() == CompiledActualKind::open
        && !vhdl_actuals[1].expression());
    std::vector<fsim::library::SourceNameMapping> consumer_mappings;
    consumer_mappings.reserve(source_mappings->size());
    for (const auto& mapping : *source_mappings) {
        consumer_mappings.push_back(
            { mapping.logical_name, mapping.producer_name });
    }
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        *restored, consumer_mappings, diagnostics));

    const std::array roots {
        fsim::elaboration::Root { "sv:work.direct_sv", "sv_root" },
        fsim::elaboration::Root {
            "vhdl:work.direct_vhdl(rtl)", "vhdl_root" },
        fsim::elaboration::Root {
            "sv:work.duplicate_configuration", "configured_root" },
    };
    auto matched = fsim::elaboration::elaborate(
        *restored,
        roots,
        { },
        { },
        nullptr,
        { });
    if (!matched.ok()) {
        for (const auto& diagnostic : matched.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(matched.ok() && matched.design);
    assert(matched.design->processes().size() == 3U);
    const auto has_specialization_parameter
        = [&](const std::string_view instance,
              const std::string_view name,
              const std::string_view value) {
              const auto specialization = std::ranges::find(
                  matched.design->specializations(), instance,
                  &fsim::elaboration::SpecializationInfo::instance);
              return specialization
                      != matched.design->specializations().end()
                  && std::ranges::find(
                         specialization->parameter_values,
                         std::pair {
                             std::string { name }, std::string { value } })
                      != specialization->parameter_values.end();
          };
    assert(has_specialization_parameter("sv_root", "SELECT", "1"));
    assert(has_specialization_parameter(
        "vhdl_root", "enabled", "true"));
    const auto configured_leaf = std::ranges::find(
        matched.design->specializations(),
        std::string { "configured_root.selected" },
        &fsim::elaboration::SpecializationInfo::instance);
    assert(configured_leaf != matched.design->specializations().end());
    assert(configured_leaf->library == "slow");
    const auto generated_leaf = std::ranges::find_if(
        matched.design->specializations(), [](const auto& specialization) {
            return specialization.instance.ends_with(
                ".compiled_gen.generated");
        });
    assert(generated_leaf != matched.design->specializations().end());
    assert(generated_leaf->source_instance
        && generated_leaf->source_instance->valid());
    const auto nested_generated_leaf = std::ranges::find_if(
        matched.design->specializations(), [](const auto& specialization) {
            return specialization.instance.ends_with(
                ".compiled_gen.nested_gen.nested_generated");
        });
    assert(nested_generated_leaf
        != matched.design->specializations().end());
    assert(nested_generated_leaf->source_instance
        && nested_generated_leaf->source_instance->valid());
    const auto vhdl_generated_leaf = std::ranges::find_if(
        matched.design->specializations(), [](const auto& specialization) {
            return specialization.instance.ends_with(
                ".selected_gen.generated_leaf");
        });
    assert(vhdl_generated_leaf != matched.design->specializations().end());
    assert(vhdl_generated_leaf->source_instance
        && vhdl_generated_leaf->source_instance->valid());
    assert(std::ranges::none_of(
        matched.design->specializations(), [](const auto& specialization) {
            return specialization.instance.find("rejected_gen")
                != std::string::npos;
        }));

    auto elaborated = fsim::elaboration::elaborate(
        *restored,
        roots,
        { },
        { },
        nullptr,
        { });
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->processes().size() == 3U);
    const auto sv_q = elaborated.design->find_signal("sv_root.q");
    const auto sv_r = elaborated.design->find_signal("sv_root.r");
    const auto vh_a = elaborated.design->find_signal("vhdl_root.a");
    const auto vh_q = elaborated.design->find_signal("vhdl_root.q");
    assert(sv_q && sv_r && vh_a && vh_q);
    const auto wildcard = std::ranges::find_if(
        elaborated.design->processes(),
        [&](const fsim::runtime::simir::Process& process) {
            return std::ranges::any_of(
                process.driver_regions,
                [&](const fsim::runtime::simir::Process::DriverRegion& region) {
                    return region.signal == *sv_r;
                });
        });
    assert(wildcard != elaborated.design->processes().end());
    assert((wildcard->static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity> {
            { *sv_q, fsim::runtime::simir::EdgeKind::any }
        }));
    assert(!wildcard->operations.empty());
    assert(fsim::runtime::simir::operation_get_if<
               fsim::runtime::simir::WaitSensitivity>(
               &wildcard->operations.front())
        != nullptr);
    const auto has_operation = [&](const auto operation_type) {
        return std::ranges::any_of(
            elaborated.design->processes(),
            [&](const fsim::runtime::simir::Process& process) {
                return std::ranges::any_of(
                    process.operations,
                    [&](const fsim::runtime::simir::Operation& operation) {
                        return operation_type(operation);
                    });
            });
    };
    assert(has_operation([](const auto& operation) {
        return fsim::runtime::simir::operation_get_if<
                   fsim::runtime::simir::Extract>(&operation)
            != nullptr;
    }));
    assert(has_operation([](const auto& operation) {
        return fsim::runtime::simir::operation_get_if<
                   fsim::runtime::simir::Concatenate>(&operation)
            != nullptr;
    }));
    assert(has_operation([](const auto& operation) {
        return fsim::runtime::simir::operation_get_if<
                   fsim::runtime::simir::ConditionalSelect>(&operation)
            != nullptr;
    }));
    assert(has_operation([](const auto& operation) {
        const auto* call = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::ClassStaticMethodCall>(&operation);
        return call != nullptr
            && call->method_identity == "@dpi:dpi_identity_c";
    }));
    auto interpreter = elaborated.design->create_interpreter();
    bool invoked_dpi { };
    interpreter->set_dpi_function_call_hook(
        [&](const std::string_view identity,
            std::vector<fsim::runtime::PackedLogic4>& actuals,
            std::vector<std::string>& strings,
            const std::span<const std::string> names,
            const std::span<const std::uint8_t> directions) {
            assert(identity == "dpi_identity_c");
            assert(actuals.size() == 1U && strings.size() == 1U);
            assert(names.size() == 1U && names.front() == "value");
            assert(directions.size() == 1U);
            invoked_dpi = true;
            return actuals.front();
        });
    interpreter->deposit_signal(
        *vh_a, fsim::runtime::PackedLogic4::from_msb_string("1"));
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*sv_q).to_msb_string() == "1");
    assert(interpreter->signal_value(*sv_r).to_msb_string() == "1");
    assert(interpreter->signal_value(*vh_q).to_msb_string() == "1");
    assert(invoked_dpi);
    std::cout << "decoded compiled-HIR lowering tests passed\n";
}
