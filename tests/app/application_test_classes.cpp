// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "application_test_uvm_virtual.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {
namespace {

    struct ForeignActivityCapture {
        std::vector<std::uint32_t> kinds;
        std::vector<std::string> identities;
        bool fail_next { };
    };

    fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL capture_foreign_activity(
        void* const context, const fsim_uvm_foreign_activity_v1* const event)
    {
        auto& capture = *static_cast<ForeignActivityCapture*>(context);
        capture.kinds.push_back(event->kind);
        capture.identities.emplace_back(event->identity, event->identity_size);
        if (capture.fail_next) {
            capture.fail_next = false;
            return FSIM_UVM_FOREIGN_CALLBACK_FAILED;
        }
        return FSIM_UVM_FOREIGN_OK;
    }

} // namespace

void ApplicationTestFixture::test_class_simulation_integration()
{
    std::cerr << "application classes: configure\n";
    auto config = base_config();
    config.project.name = "class-simulation-integration";
    config.project.top = "sv:work.class_top";
    config.build.cache_path = directory / "class-simulation-cache";
    config.source_sets.clear();
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(class_source);
    config.source_sets.push_back(std::move(sources));

    using EngineSnapshot = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t,
        std::uint64_t, std::uint64_t, std::uint64_t,
        std::uint64_t, std::size_t, std::uint64_t>;
    std::vector<EngineSnapshot> engine_snapshots;
    std::vector<fsim::runtime::SystemVerilogUvmCheckpointArtifact>
        portable_uvm_snapshots;

    for (const auto engine : { fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::compiled,
             fsim::app::SimulationEngine::debug }) {
        std::cerr << "application classes: engine " << static_cast<unsigned>(engine)
                  << " build\n";
        fsim::diagnostic::Engine diagnostics;
        auto built = fsim::app::build_project(config, diagnostics);
        if (!built) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
        assert(built);
        const auto derived = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with("::AppDerived");
            });
        assert(derived != built->systemverilog_class_specializations.end());
        const auto base = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with("::AppBase");
            });
        assert(base != built->systemverilog_class_specializations.end());
        const auto uvm_item = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with("::UvmItem");
            });
        const auto uvm_object = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with("::uvm_object");
            });
        const auto uvm_component = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with(
                    "::uvm_component");
            });
        const auto uvm_param_item = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with(
                    "::UvmParamItem");
            });
        const auto uvm_factory_item = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with(
                    "::UvmFactoryItem");
            });
        const auto test_uvm_component = std::ranges::find_if(
            built->systemverilog_class_specializations,
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with(
                    "::UvmComponent");
            });
        assert(
            uvm_item != built->systemverilog_class_specializations.end() && uvm_object != built->systemverilog_class_specializations.end() && uvm_component != built->systemverilog_class_specializations.end() && uvm_param_item != built->systemverilog_class_specializations.end() && uvm_factory_item != built->systemverilog_class_specializations.end() && test_uvm_component != built->systemverilog_class_specializations.end());
        const auto derived_method = std::ranges::find(
            derived->methods, std::string { "bump" },
            &fsim::frontend::SystemVerilogClassMethodProfile::name);
        assert(derived_method != derived->methods.end());
        assert(derived_method->virtual_slot);
        const auto base_method_profile = std::ranges::find(
            base->methods, std::string { "bump" },
            &fsim::frontend::SystemVerilogClassMethodProfile::name);
        assert(base_method_profile != base->methods.end());
        const auto derived_specialization = derived->specialization_identity;
        const auto derived_identity = derived->declaration_identity;
        const auto base_identity = base->declaration_identity;
        const auto uvm_item_specialization = uvm_item->specialization_identity;
        const auto uvm_item_identity = uvm_item->declaration_identity;
        const auto uvm_object_identity = uvm_object->declaration_identity;
        const auto uvm_component_identity = uvm_component->declaration_identity;
        const auto test_uvm_component_specialization = test_uvm_component->specialization_identity;
        const auto test_uvm_component_identity = test_uvm_component->declaration_identity;
        const auto uvm_param_item_specialization = uvm_param_item->specialization_identity;
        const auto uvm_factory_item_specialization = uvm_factory_item->specialization_identity;
        const auto derived_method_identity = derived_method->canonical_identity;
        const auto base_method_identity = base_method_profile->canonical_identity;
        if (engine == fsim::app::SimulationEngine::interpreter) {
            fsim::diagnostic::Engine class_state_diagnostics;
            const auto class_state = fsim::app::serialize_class_state(
                built->systemverilog_class_specializations, class_state_diagnostics);
            assert(class_state);
            const auto restored = fsim::app::deserialize_class_state(
                *class_state, "classes.bin", class_state_diagnostics);
            assert(restored);
            assert(restored->size() == built->systemverilog_class_specializations.size());
            const auto restored_derived = std::ranges::find(*restored, derived_specialization,
                &fsim::frontend::SystemVerilogClassSpecialization::
                    specialization_identity);
            assert(restored_derived != restored->end());
            const auto restored_random = std::ranges::find(
                restored_derived->properties, std::string { "generated_value" },
                &fsim::frontend::SystemVerilogClassPropertyLayout::name);
            assert(restored_random != restored_derived->properties.end());
            assert(restored_random->is_randc && !restored_random->is_rand);
            const auto restored_wide = std::ranges::find(
                restored_derived->properties, std::string { "wide_value" },
                &fsim::frontend::SystemVerilogClassPropertyLayout::name);
            assert(restored_wide != restored_derived->properties.end() && restored_wide->bit_width == 137 && restored_wide->type.width() == 137);
            const auto restored_interface = std::ranges::find(
                restored_derived->properties, std::string { "interface_view" },
                &fsim::frontend::SystemVerilogClassPropertyLayout::name);
            assert(
                restored_interface != restored_derived->properties.end() && restored_interface->bit_width == 64 && restored_interface->type.systemverilog_virtual_interface && restored_interface->type.systemverilog_interface_type == "class_view_if" && restored_interface->type.systemverilog_interface_modport == "view" && restored_interface->type.systemverilog_class_parameter_actuals.size() == 1 && restored_interface->type.systemverilog_class_parameter_actuals.front().name == std::optional<std::string> { "WIDTH" } && restored_interface->type.systemverilog_class_parameter_actuals.front().value.text == "4");
            auto malformed_classes = *restored;
            auto malformed_derived = std::ranges::find(malformed_classes, derived_specialization,
                &fsim::frontend::SystemVerilogClassSpecialization::
                    specialization_identity);
            assert(malformed_derived != malformed_classes.end());
            auto malformed_wide = std::ranges::find(
                malformed_derived->properties, std::string { "wide_value" },
                &fsim::frontend::SystemVerilogClassPropertyLayout::name);
            assert(malformed_wide != malformed_derived->properties.end());
            malformed_wide->type.packed_aggregate = static_cast<fsim::frontend::PackedAggregateKind>(255);
            fsim::diagnostic::Engine malformed_class_diagnostics;
            assert(!fsim::app::serialize_class_state(malformed_classes,
                malformed_class_diagnostics));
            auto trailing = *class_state;
            trailing.push_back('\0');
            fsim::diagnostic::Engine trailing_diagnostics;
            assert(!fsim::app::deserialize_class_state(
                trailing, "trailing-classes.bin", trailing_diagnostics));
            const auto truncated = class_state->substr(0, class_state->size() - 1U);
            fsim::diagnostic::Engine truncated_diagnostics;
            assert(!fsim::app::deserialize_class_state(
                truncated, "truncated-classes.bin", truncated_diagnostics));
            auto future = *class_state;
            future[8] = static_cast<char>(fsim::app::kClassStateSchema + 1U);
            fsim::diagnostic::Engine future_diagnostics;
            assert(!fsim::app::deserialize_class_state(future, "future-classes.bin",
                future_diagnostics));

            const auto constraint_hir_state = fsim::app::serialize_systemverilog_constraint_hir_state(
                built->systemverilog_hir, built->semantics,
                class_state_diagnostics);
            assert(constraint_hir_state);
            fsim::semantic::sv::Hir malformed_hir;
            malformed_hir.mutable_classes() = {
                built->systemverilog_hir.classes().begin(),
                built->systemverilog_hir.classes().end()
            };
            const auto malformed_hir_base = std::ranges::find_if(
                malformed_hir.mutable_classes(), [](const auto& declaration) {
                    return declaration.canonical_identity.ends_with("::AppBase");
                });
            assert(malformed_hir_base != malformed_hir.mutable_classes().end());
            const auto malformed_hir_wide = std::ranges::find(
                malformed_hir_base->properties, std::string { "wide_value" },
                &fsim::semantic::sv::ClassProperty::name);
            assert(malformed_hir_wide != malformed_hir_base->properties.end());
            malformed_hir_wide->type.value_form = static_cast<fsim::semantic::sv::TypeForm>(255);
            fsim::diagnostic::Engine malformed_hir_diagnostics;
            assert(!fsim::app::serialize_systemverilog_constraint_hir_state(
                malformed_hir, built->semantics, malformed_hir_diagnostics));
            const auto restored_constraint_hir = fsim::app::deserialize_systemverilog_constraint_hir_state(
                *constraint_hir_state, "sv-constraint-hir.bin",
                built->semantics,
                class_state_diagnostics);
            assert(restored_constraint_hir);
            assert(restored_constraint_hir->units().size()
                == built->systemverilog_hir.units().size());
            assert(restored_constraint_hir->declarations().size()
                == built->systemverilog_hir.declarations().size());
            assert(restored_constraint_hir->types().size()
                == built->systemverilog_hir.types().size());
            assert(restored_constraint_hir->expressions().size()
                == built->systemverilog_hir.expressions().size());
            assert(restored_constraint_hir->statements().size()
                == built->systemverilog_hir.statements().size());
            assert(restored_constraint_hir->processes().size()
                == built->systemverilog_hir.processes().size());
            assert(restored_constraint_hir->classes().size() == built->systemverilog_hir.classes().size());
            const auto restored_constraint_class = std::ranges::find(
                restored_constraint_hir->classes(), derived_identity,
                &fsim::semantic::sv::ClassDeclaration::canonical_identity);
            assert(restored_constraint_class != restored_constraint_hir->classes().end() && !restored_constraint_class->constraints.empty() && !restored_constraint_class->composed_constraints.empty());
            const auto restored_constraint_base = std::ranges::find_if(
                restored_constraint_hir->classes(), [](const auto& declaration) {
                    return declaration.canonical_identity.ends_with("::AppBase");
                });
            assert(restored_constraint_base != restored_constraint_hir->classes().end());
            const auto restored_constraint_wide = std::ranges::find(
                restored_constraint_base->properties, std::string { "wide_value" },
                &fsim::semantic::sv::ClassProperty::name);
            assert(restored_constraint_wide != restored_constraint_base->properties.end() && restored_constraint_wide->type.executable_width == 137);
            auto future_constraint_hir = *constraint_hir_state;
            future_constraint_hir[8] = static_cast<char>(
                fsim::app::kSystemVerilogConstraintHirStateSchema + 1U);
            fsim::diagnostic::Engine future_constraint_hir_diagnostics;
            assert(!fsim::app::deserialize_systemverilog_constraint_hir_state(
                future_constraint_hir, "future-sv-constraint-hir.bin",
                built->semantics,
                future_constraint_hir_diagnostics));
        }
        if (engine == fsim::app::SimulationEngine::interpreter) {
            fsim::diagnostic::Engine stale_debug_diagnostics;
            auto stale_debug_project = fsim::app::build_project(config, stale_debug_diagnostics);
            assert(stale_debug_project && !stale_debug_diagnostics.has_error());
            fsim::app::Simulation stale_debug_simulation(
                std::move(*stale_debug_project), config.run.max_deltas,
                fsim::app::SimulationEngine::interpreter);
            const auto stale_debug_root = stale_debug_simulation.create_uvm_root("stale-debug");
            const auto stale_debug_component = stale_debug_simulation.allocate_uvm_component(
                test_uvm_component_specialization, "component", 0,
                stale_debug_root, uvm_component_identity);
            const fsim::runtime::SystemVerilogUvmTlm1Profile stale_debug_profile {
                fsim::runtime::SystemVerilogUvmTlm1Interface::Bidirectional,
                fsim::runtime::SystemVerilogUvmTlm1Direction::Bidirectional,
                "work::stale_debug_payload",
                { }
            };
            (void)stale_debug_simulation.uvm_tlm1().register_endpoint(
                { fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Implementation,
                    stale_debug_profile, stale_debug_component, "implementation", 0, 0 });
            stale_debug_simulation.uvm_components().release(stale_debug_component);
            bool stale_debug_rejected { };
            try {
                (void)stale_debug_simulation.uvm_debug_snapshot();
            } catch (const fsim::app::UvmDebugError& error) {
                stale_debug_rejected = error.diagnostic_code() == "FSIM-UVM-DEBUG-001";
            }
            assert(stale_debug_rejected);
        }
        const auto virtual_slot = derived_method->virtual_slot;
        fsim::app::Simulation simulation(std::move(*built), config.run.max_deltas,
            engine);
        std::vector<fsim::runtime::SystemVerilogUvmActivityEvent>
            api_uvm_activity_callbacks;
        const auto api_uvm_activity_token = simulation.add_uvm_activity_hook([&](const auto& event) {
            api_uvm_activity_callbacks.push_back(event);
        });
        ForeignActivityCapture foreign_activity;
        std::uint64_t foreign_activity_token { };
        const auto foreign_host = fsim::runtime::make_systemverilog_uvm_foreign_host(
            simulation.uvm_foreign());
        assert(foreign_host.abi_version == FSIM_UVM_FOREIGN_ABI_VERSION && foreign_host.struct_size == sizeof(fsim_uvm_foreign_host_v1) && foreign_host.simulation_identity == simulation.uvm_foreign().simulation_identity() && foreign_host.add_callback(foreign_host.context, capture_foreign_activity, &foreign_activity, &foreign_activity_token) == FSIM_UVM_FOREIGN_OK);
        std::cerr << "application classes: engine " << static_cast<unsigned>(engine)
                  << " execute\n";
        const auto handle = simulation.allocate_class(derived_specialization, base_identity);
        const auto uvm_root = simulation.allocate_uvm_object(
            uvm_item_specialization, "api_root", uvm_object_identity);
        const auto uvm_child = simulation.allocate_uvm_object(
            uvm_item_specialization, "api_child", uvm_object_identity);
        const auto uvm_virtual_sequence_object = simulation.allocate_uvm_object(
            uvm_item_specialization, "api_virtual_sequence", uvm_object_identity);
        const auto uvm_domain_sequence_object = simulation.allocate_uvm_object(
            uvm_item_specialization, "api_domain_sequence", uvm_object_identity);
        const auto api_component_root = simulation.create_uvm_root("api-first");
        const auto api_component_second_root = simulation.create_uvm_root("api-second");
        const auto api_component_top = simulation.allocate_uvm_component(
            test_uvm_component_specialization, "api_top", 0, api_component_root,
            uvm_component_identity);
        const auto api_component_child = simulation.allocate_uvm_component(
            test_uvm_component_specialization, "api_child", api_component_top,
            api_component_root, uvm_component_identity);
        const auto api_component_isolated = simulation.allocate_uvm_component(
            test_uvm_component_specialization, "api_top", 0,
            api_component_second_root, uvm_component_identity);
        auto& api_sequences = simulation.uvm_sequences();
        const fsim::runtime::SystemVerilogUvmSequenceProfile api_sequence_profile {
            uvm_item_specialization, uvm_item_specialization
        };
        const auto api_sequencer = api_sequences.register_sequencer(
            { api_component_top, test_uvm_component_specialization,
                api_sequence_profile });
        ApplicationVirtualSequenceProbe api_virtual_probe {
            simulation,
            api_component_child,
            api_sequencer,
            uvm_virtual_sequence_object,
            uvm_domain_sequence_object,
            test_uvm_component_specialization,
            uvm_item_specialization,
            api_sequence_profile
        };
        std::size_t api_agent_role_callbacks { };
        std::size_t api_driver_role_callbacks { };
        std::size_t api_driver_role_tasks { };
        std::size_t api_passive_agent_role_callbacks { };
        fsim::runtime::SystemVerilogUvmSequenceRoleDescriptor
            api_agent_role_descriptor;
        api_agent_role_descriptor.component = api_component_top;
        api_agent_role_descriptor.kind = fsim::runtime::SystemVerilogUvmSequenceRoleKind::Agent;
        api_agent_role_descriptor.default_agent_mode = fsim::runtime::SystemVerilogUvmAgentMode::Passive;
        api_agent_role_descriptor.function_dispatch =
            [&](const auto, const auto, const auto) { ++api_agent_role_callbacks; };
        const auto api_agent_role = api_sequences.register_role(std::move(api_agent_role_descriptor));
        fsim::runtime::SystemVerilogUvmSequenceRoleDescriptor
            api_driver_role_descriptor;
        api_driver_role_descriptor.component = api_component_child;
        api_driver_role_descriptor.kind = fsim::runtime::SystemVerilogUvmSequenceRoleKind::Driver;
        api_driver_role_descriptor.agent = api_agent_role;
        api_driver_role_descriptor.sequencer = api_sequencer;
        api_driver_role_descriptor.automatic_objection = true;
        api_driver_role_descriptor.function_dispatch = [&](const auto, const auto,
                                                           const auto) {
            ++api_driver_role_callbacks;
        };
        api_driver_role_descriptor.task_dispatch = [&](const auto, const auto phase,
                                                       const auto process) {
            ++api_driver_role_tasks;
            return api_virtual_probe.dispatch(phase, process);
        };
        const auto api_driver_role = api_sequences.register_role(std::move(api_driver_role_descriptor));
        fsim::runtime::SystemVerilogUvmSequenceRoleDescriptor
            api_passive_agent_role_descriptor;
        api_passive_agent_role_descriptor.component = api_component_isolated;
        api_passive_agent_role_descriptor.kind = fsim::runtime::SystemVerilogUvmSequenceRoleKind::Agent;
        api_passive_agent_role_descriptor.default_agent_mode = fsim::runtime::SystemVerilogUvmAgentMode::Active;
        api_passive_agent_role_descriptor.function_dispatch =
            [&](const auto, const auto, const auto) {
                ++api_passive_agent_role_callbacks;
            };
        const auto api_passive_agent_role = api_sequences.register_role(
            std::move(api_passive_agent_role_descriptor));
        const auto configure_agent_mode =
            [&](const fsim::runtime::SystemVerilogClassHandle component,
                const fsim::runtime::SystemVerilogUvmAgentMode mode) {
                const auto snapshot = simulation.uvm_components().snapshot(component);
                const fsim::runtime::SystemVerilogUvmConfigContext context {
                    std::string {
                        simulation.uvm_components().root_identity(snapshot.root) }
                        + ":" + snapshot.full_name,
                    snapshot.depth
                };
                const auto resource = simulation.uvm_config_db().set(
                    context, { }, "is_active",
                    { "uvm_pkg::uvm_active_passive_enum",
                        fsim::runtime::SystemVerilogUvmResourceValueKind::Packed, 1 },
                    fsim::runtime::PackedLogic4::from_aval_bval(
                        1,
                        mode == fsim::runtime::SystemVerilogUvmAgentMode::Active ? 1
                                                                                 : 0,
                        0),
                    fsim::runtime::SystemVerilogUvmConfigPhase::Build);
                simulation.uvm_resources().set_auditing(resource, false);
            };
        configure_agent_mode(api_component_top,
            fsim::runtime::SystemVerilogUvmAgentMode::Active);
        configure_agent_mode(api_component_isolated,
            fsim::runtime::SystemVerilogUvmAgentMode::Passive);
        std::size_t api_sequence_callbacks { };
        fsim::runtime::SystemVerilogUvmSequenceDescriptor api_sequence_descriptor;
        api_sequence_descriptor.object = uvm_root;
        api_sequence_descriptor.name = "api_sequence";
        api_sequence_descriptor.nominal_type = uvm_item_specialization;
        api_sequence_descriptor.profile = api_sequence_profile;
        api_sequence_descriptor.sequencer = api_sequencer;
        const auto api_sequence_callback = [&](const auto) {
            ++api_sequence_callbacks;
        };
        api_sequence_descriptor.hooks = {
            api_sequence_callback, api_sequence_callback, api_sequence_callback,
            api_sequence_callback, api_sequence_callback
        };
        const auto api_sequence = api_sequences.register_sequence(std::move(api_sequence_descriptor));
        const auto api_sequence_item = api_sequences.register_item(
            { uvm_child,
                "api_request",
                uvm_item_specialization,
                fsim::runtime::SystemVerilogUvmSequenceItemRole::Request,
                api_sequence,
                { } });
        const fsim::runtime::SystemVerilogUvmTlm1Profile api_tlm_profile {
            fsim::runtime::SystemVerilogUvmTlm1Interface::Bidirectional,
            fsim::runtime::SystemVerilogUvmTlm1Direction::Bidirectional,
            "work::engine_payload",
            { }
        };
        auto& api_tlm1 = simulation.uvm_tlm1();
        const auto api_tlm_port = api_tlm1.register_endpoint(
            { fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Port, api_tlm_profile,
                api_component_top, "engine_port", 1, 1 });
        const auto api_tlm_imp = api_tlm1.register_endpoint(
            { fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Implementation,
                api_tlm_profile, api_component_child, "engine_imp", 0, 0 });
        api_tlm1.connect(api_tlm_port, api_tlm_imp);
        api_tlm1.resolve_all();
        api_tlm1.configure_fifo(api_tlm_imp, 2);
        fsim::runtime::PackedLogic4 api_tlm_value(96, fsim::runtime::Logic4::zero);
        for (std::size_t bit = 0; bit < 32; ++bit) {
            if (((0x160a'cce5U >> bit) & 1U) != 0) {
                api_tlm_value.set(bit, fsim::runtime::Logic4::one);
            }
        }
        assert(api_tlm1.try_put(
            api_tlm_port,
            { "work::engine_payload", std::move(api_tlm_value), 0, 0, { }, 0 }));
        const auto api_tlm_payload = api_tlm1.try_get(api_tlm_port);
        std::uint64_t api_tlm_low { };
        for (std::size_t bit = 0; bit < 32; ++bit) {
            if (api_tlm_payload && api_tlm_payload->value.get(bit) == fsim::runtime::Logic4::one) {
                api_tlm_low |= std::uint64_t { 1 } << bit;
            }
        }
        fsim::runtime::PackedLogic4 retained_tlm_value(96,
            fsim::runtime::Logic4::one);
        assert(api_tlm1.try_put(api_tlm_port, { "work::engine_payload", std::move(retained_tlm_value), 0, api_component_root, { }, 0 }));
        const fsim::runtime::SystemVerilogUvmTlm1Profile api_analysis_profile {
            fsim::runtime::SystemVerilogUvmTlm1Interface::Analysis,
            fsim::runtime::SystemVerilogUvmTlm1Direction::Forward,
            "work::engine_analysis",
            { }
        };
        const auto api_analysis_port = api_tlm1.register_endpoint(
            { fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Port,
                api_analysis_profile, api_component_top, "analysis_port", 1, 1 });
        std::size_t api_analysis_calls { };
        std::size_t api_analysis_width { };
        const auto api_analysis_imp = api_tlm1.register_analysis_implementation(
            api_component_child, "write_scoreboard", "work::engine_analysis",
            [&](fsim::runtime::SystemVerilogUvmTlm1Payload delivered) {
                ++api_analysis_calls;
                api_analysis_width = delivered.value.width();
            });
        api_tlm1.connect(api_analysis_port, api_analysis_imp);
        api_tlm1.resolve_all();
        fsim::runtime::PackedLogic4 api_analysis_value(73,
            fsim::runtime::Logic4::one);
        const auto api_analysis_result = api_tlm1.write_analysis(
            api_analysis_port,
            { "work::engine_analysis", std::move(api_analysis_value), 0, 0, { }, 0 });
        auto& api_tlm2 = simulation.uvm_tlm2();
        const fsim::runtime::SystemVerilogUvmTlm2Profile api_tlm2_profile {
            fsim::runtime::SystemVerilogUvmTlm2Protocol::Combined,
            "work::engine_generic_payload", "work::engine_phase", 16
        };
        const auto api_tlm2_initiator = api_tlm2.register_socket(
            { fsim::runtime::SystemVerilogUvmTlm2SocketKind::Initiator,
                api_tlm2_profile, api_component_top, "initiator_socket", 1, 1 });
        const auto api_tlm2_target = api_tlm2.register_socket(
            { fsim::runtime::SystemVerilogUvmTlm2SocketKind::Target,
                api_tlm2_profile, api_component_child, "target_socket", 0, 0 });
        api_tlm2.connect(api_tlm2_initiator, api_tlm2_target);
        api_tlm2.bind_all();
        api_tlm2.set_blocking_handler(
            api_tlm2_target, [](auto& transaction, auto& delay) {
                transaction.data.front() = 0xd2;
                transaction.response_status = fsim::runtime::SystemVerilogUvmTlm2ResponseStatus::Ok;
                delay += 4;
            });
        fsim::runtime::SystemVerilogUvmTlm2GenericPayload api_tlm2_payload;
        api_tlm2_payload.nominal_type = "work::engine_generic_payload";
        api_tlm2_payload.command = fsim::runtime::SystemVerilogUvmTlm2Command::Write;
        api_tlm2_payload.address = 0x1600;
        api_tlm2_payload.data.resize(37, 0x5a);
        api_tlm2_payload.streaming_width = 37;
        api_tlm2_payload.extensions.emplace("work::engine_extension",
            std::vector<std::uint8_t> { 1, 6, 0 });
        const auto api_tlm2_result = api_tlm2.b_transport(
            api_tlm2_initiator, std::move(api_tlm2_payload), 3);
        api_tlm2.set_forward_handler(api_tlm2_target, [](auto&, auto&, auto&) {
            return fsim::runtime::SystemVerilogUvmTlm2Sync::Accepted;
        });
        auto api_tlm2_nb_payload = api_tlm2_result.payload;
        api_tlm2_nb_payload.transaction_id = 0;
        api_tlm2_nb_payload.owner_root = 0;
        api_tlm2_nb_payload.owner_socket = { };
        const auto api_tlm2_nb_result = api_tlm2.nb_transport_fw(
            api_tlm2_initiator, std::move(api_tlm2_nb_payload),
            { fsim::runtime::SystemVerilogUvmTlm2PhaseKind::BeginRequest,
                { },
                "work::engine_phase" },
            1);
        auto& uvm_phases = simulation.uvm_phases();
        const auto api_schedule = *uvm_phases.standard_schedule();
        const auto api_phase_domain = api_schedule.common_domain;
        const auto api_build_phase = api_schedule.phase(fsim::runtime::SystemVerilogUvmPhaseKind::Build);
        assert((
            simulation.uvm_components().full_name(api_component_child) == "api_top.api_child" && api_tlm_payload && api_tlm_payload->nominal_type == "work::engine_payload" && api_tlm_payload->value.width() == 96 && api_tlm_low == 0x160a'cce5U && api_tlm_payload->owner_endpoint == api_tlm_port && api_analysis_result.success() && api_analysis_result.deliveries.size() == 1 && api_analysis_calls == 1 && api_analysis_width == 73 && api_tlm2_result.target == api_tlm2_target && api_tlm2_result.payload.data.size() == 37 && api_tlm2_result.payload.data.front() == 0xd2 && api_tlm2_result.payload.extensions.size() == 1 && api_tlm2_result.delay == 7 && api_tlm2_result.payload.response_status == fsim::runtime::SystemVerilogUvmTlm2ResponseStatus::Ok && api_tlm2_nb_result.transaction && api_tlm2_nb_result.sync == fsim::runtime::SystemVerilogUvmTlm2Sync::Accepted && simulation.uvm_components().parent(api_component_child) == api_component_top && simulation.uvm_components().lookup_root(api_component_root, "api_top.api_child") == api_component_child && simulation.uvm_components().lookup_root(api_component_second_root, "api_top") == api_component_isolated && simulation.uvm_components().children(api_component_top) == std::vector<fsim::runtime::SystemVerilogClassHandle> { api_component_child } && api_sequences.snapshot(api_sequencer).root == api_component_root && api_sequences.snapshot(api_sequence).sequencer == api_sequencer && api_sequences.snapshot(api_sequence_item).owner_sequence == api_sequence && api_sequences.sequence_count() == 3 && api_sequences.item_count() == 1 && api_sequences.role_count() == 3 && api_sequence_callbacks == 0 && uvm_phases.snapshot(api_phase_domain).roots == std::vector<fsim::runtime::SystemVerilogUvmRootHandle> { api_component_root, api_component_second_root } && uvm_phases.snapshot(api_build_phase).identity == "build"));
        const auto api_sequence_result = api_sequences.start(api_sequence);
        const auto api_sequence_lock = api_sequences.request_lock(api_sequence);
        assert((api_sequences.has_lock(api_sequence) && api_sequences.access_snapshot(api_sequence_lock).state == fsim::runtime::SystemVerilogUvmSequenceAccessState::Granted));
        api_sequences.unlock(api_sequence);
        api_sequences.configure_arbitration(
            api_sequencer,
            fsim::runtime::SystemVerilogUvmSequenceArbitrationMode::StrictFifo,
            0x1610'0003ULL);
        const auto api_sequence_request = api_sequences.macro_send(api_sequence_item, 160);
        const auto api_sequence_acquisition = api_sequences.get_next_item(api_sequencer);
        assert(api_sequence_acquisition.transaction);
        api_sequences.item_done(api_sequence_acquisition.transaction->handle);
        const auto api_sequence_transaction = api_sequences.transaction_snapshot(
            api_sequence_acquisition.transaction->handle);
        assert((
            api_sequence_result.success() && api_sequence_result.final_state == fsim::runtime::SystemVerilogUvmSequenceState::Finished && api_sequence_result.events.size() == 7 && api_sequence_callbacks == 5 && api_sequence_acquisition.status == fsim::runtime::SystemVerilogUvmSequenceAcquireStatus::Acquired && api_sequence_transaction.request.handle == api_sequence_request && api_sequence_transaction.sequence == api_sequence && api_sequence_transaction.request_item == api_sequence_item && api_sequence_transaction.state == fsim::runtime::SystemVerilogUvmSequenceTransactionState::Completed));
        const auto uvm_debug = simulation.uvm_debug_snapshot();
        const auto uvm_debug_text = fsim::app::format_uvm_debug_snapshot(
            uvm_debug, fsim::app::UvmDebugSection::all);
        assert((uvm_debug.domains.size() == 2 && uvm_debug.phases.size() == 21 && uvm_debug.tlm1_endpoints.size() == 4 && uvm_debug.tlm1_fifos.size() == 1 && uvm_debug.tlm2_sockets.size() == 2 && uvm_debug_text.find("api-first:api_top.engine_port") != std::string::npos && uvm_debug_text.find("api-first:api_top.initiator_socket") != std::string::npos));
        assert((
            std::ranges::any_of(
                simulation.uvm_activity().events(),
                [](const auto& event) {
                    return event.kind == fsim::runtime::SystemVerilogUvmActivityKind::Graph && event.action == fsim::runtime::SystemVerilogUvmActivityAction::Created;
                })
            && std::ranges::any_of(api_uvm_activity_callbacks, [](const auto& event) {
                   return event.kind == fsim::runtime::SystemVerilogUvmActivityKind::Transaction && event.action == fsim::runtime::SystemVerilogUvmActivityAction::Completed && event.detail == "tlm2-blocking";
               })));
        bool uvm_debug_bounded { };
        try {
            (void)simulation.uvm_debug_snapshot({ 1, 1, 1 });
        } catch (const fsim::app::UvmDebugError& error) {
            uvm_debug_bounded = error.diagnostic_code() == "FSIM-UVM-DEBUG-002";
        }
        assert(uvm_debug_bounded);

        fsim_uvm_foreign_snapshot_v1 foreign_snapshot { };
        const auto foreign_capture_status = foreign_host.capture(foreign_host.context, &foreign_snapshot);
        if (foreign_capture_status != FSIM_UVM_FOREIGN_OK) {
            std::cerr << simulation.uvm_foreign().diagnostic_code() << ": "
                      << simulation.uvm_foreign().diagnostic_message() << '\n';
        }
        assert(foreign_capture_status == FSIM_UVM_FOREIGN_OK && foreign_snapshot.abi_version == FSIM_UVM_FOREIGN_ABI_VERSION && foreign_snapshot.simulation_identity == simulation.uvm_foreign().simulation_identity() && foreign_snapshot.record_count >= 29);
        bool foreign_phase { };
        bool foreign_tlm1 { };
        bool foreign_tlm2_payload { };
        for (std::size_t index = 0; index < foreign_snapshot.record_count;
            ++index) {
            fsim_uvm_foreign_record_v1 record { };
            const auto sized = foreign_host.copy_record(
                foreign_host.context, &foreign_snapshot, index, &record, nullptr, 0,
                nullptr, 0, nullptr, 0);
            assert(sized == FSIM_UVM_FOREIGN_BUFFER_TOO_SMALL || sized == FSIM_UVM_FOREIGN_OK);
            std::vector<char> identity(record.identity_size);
            std::vector<char> detail(record.detail_size);
            std::vector<std::uint8_t> payload(record.payload_size);
            assert(foreign_host.copy_record(
                       foreign_host.context, &foreign_snapshot, index, &record,
                       identity.data(), identity.size(), detail.data(), detail.size(),
                       payload.data(), payload.size())
                == FSIM_UVM_FOREIGN_OK);
            const std::string identity_text { identity.begin(), identity.end() };
            foreign_phase = foreign_phase || (record.kind == FSIM_UVM_FOREIGN_PHASE && identity_text == "common.build");
            foreign_tlm1 = foreign_tlm1 || (record.kind == FSIM_UVM_FOREIGN_TLM1_FIFO && record.value == 1 && record.auxiliary == 2);
            foreign_tlm2_payload = foreign_tlm2_payload || (record.kind == FSIM_UVM_FOREIGN_TLM2_TRANSACTION && payload.size() == 37 && payload.front() == 0xd2);
        }
        assert(foreign_phase && foreign_tlm1 && foreign_tlm2_payload);
        const auto portable_uvm = simulation.capture_uvm_checkpoint();
        assert(portable_uvm && portable_uvm.artifact.time == simulation.now() && portable_uvm.artifact.delta == simulation.delta() && portable_uvm.artifact.external_state.callbacks == 1 && portable_uvm.artifact.external_state.phase_processes == 0 && std::ranges::none_of(portable_uvm.artifact.records, [](const auto& record) {
            return record.kind == FSIM_UVM_FOREIGN_PHASE_PROCESS;
        }) && std::ranges::any_of(portable_uvm.artifact.records, [](const auto& record) {
            return record.kind == FSIM_UVM_FOREIGN_PHASE && record.identity == "common.build" && record.detail.find("successor=") != std::string::npos;
        }) && std::ranges::any_of(portable_uvm.artifact.records, [](const auto& record) {
            return record.kind == FSIM_UVM_FOREIGN_TLM1_ENDPOINT && record.detail.find("outbound=") != std::string::npos;
        }) && std::ranges::any_of(portable_uvm.artifact.records, [](const auto& record) {
            return record.kind == FSIM_UVM_FOREIGN_TLM1_FIFO && record.value == 1 && record.payload.size() > sizeof(std::uint64_t);
        }) && std::ranges::any_of(portable_uvm.artifact.records, [](const auto& record) {
            return record.kind == FSIM_UVM_FOREIGN_TLM2_TRANSACTION && record.payload.size() == 37 && record.detail.find("target=") != std::string::npos;
        }));
        portable_uvm_snapshots.push_back(portable_uvm.artifact);
        fsim::runtime::SystemVerilogUvmForeignService foreign_peer {
            simulation.uvm_phases(), simulation.uvm_objections(),
            simulation.uvm_tlm1(), simulation.uvm_tlm2(),
            simulation.uvm_activity()
        };
        fsim_uvm_foreign_record_v1 foreign_record { };
        assert(foreign_peer.copy_record(foreign_snapshot, 0, foreign_record, { }, { },
                   { })
                == FSIM_UVM_FOREIGN_WRONG_SIMULATION
            && foreign_peer.diagnostic_code() == "FSIM-UVM-FOREIGN-001");
        ForeignActivityCapture teardown_activity;
        {
            fsim::runtime::SystemVerilogUvmForeignService teardown_foreign {
                simulation.uvm_phases(), simulation.uvm_objections(),
                simulation.uvm_tlm1(), simulation.uvm_tlm2(),
                simulation.uvm_activity()
            };
            std::uint64_t teardown_token { };
            assert(teardown_foreign.add_callback(
                       capture_foreign_activity, &teardown_activity,
                       teardown_token)
                == FSIM_UVM_FOREIGN_OK);
        }
        simulation.uvm_activity().publish(
            { fsim::runtime::SystemVerilogUvmActivityKind::Quiescence,
                fsim::runtime::SystemVerilogUvmActivityAction::Updated,
                "foreign-unload", "probe" });
        assert(teardown_activity.kinds.empty());
        fsim::runtime::SystemVerilogUvmForeignLimits bounded_foreign_limits;
        bounded_foreign_limits.maximum_snapshots = 1;
        fsim::runtime::SystemVerilogUvmForeignService bounded_foreign {
            simulation.uvm_phases(), simulation.uvm_objections(),
            simulation.uvm_tlm1(), simulation.uvm_tlm2(),
            simulation.uvm_activity(), bounded_foreign_limits
        };
        fsim_uvm_foreign_snapshot_v1 bounded_foreign_snapshot { };
        fsim_uvm_foreign_snapshot_v1 rejected_foreign_snapshot { };
        assert(bounded_foreign.capture(bounded_foreign_snapshot) == FSIM_UVM_FOREIGN_OK && bounded_foreign.capture(rejected_foreign_snapshot) == FSIM_UVM_FOREIGN_RESOURCE_LIMIT && bounded_foreign.diagnostic_code() == "FSIM-UVM-FOREIGN-002" && bounded_foreign.release(bounded_foreign_snapshot) == FSIM_UVM_FOREIGN_OK);
        api_tlm2.cancel_transaction(api_tlm2_nb_result.transaction);
        fsim_uvm_foreign_snapshot_v1 cancelled_foreign_snapshot { };
        assert(simulation.uvm_foreign().capture(cancelled_foreign_snapshot) == FSIM_UVM_FOREIGN_OK);
        bool foreign_cancelled { };
        for (std::size_t index = 0; index < cancelled_foreign_snapshot.record_count;
            ++index) {
            fsim_uvm_foreign_record_v1 record { };
            const auto status = simulation.uvm_foreign().copy_record(
                cancelled_foreign_snapshot, index, record, { }, { }, { });
            assert(status == FSIM_UVM_FOREIGN_BUFFER_TOO_SMALL || status == FSIM_UVM_FOREIGN_OK);
            foreign_cancelled = foreign_cancelled || (record.kind == FSIM_UVM_FOREIGN_TLM2_TRANSACTION && record.state == static_cast<std::uint32_t>(fsim::runtime::SystemVerilogUvmTlm2TransactionState::Cancelled));
        }
        assert(foreign_cancelled && simulation.uvm_foreign().release(cancelled_foreign_snapshot) == FSIM_UVM_FOREIGN_OK);
        foreign_activity.fail_next = true;

        const auto callback_order = [](const auto& phase_result) {
            std::vector<fsim::runtime::SystemVerilogClassHandle> result;
            for (const auto& event : phase_result.events) {
                if (event.callback == fsim::runtime::SystemVerilogUvmPhaseCallbackKind::Execute) {
                    result.push_back(event.component);
                }
            }
            return result;
        };
        const auto build_result = simulation.execute_uvm_function_phase(api_build_phase);
        assert((build_result.success() && build_result.final_state == fsim::runtime::SystemVerilogUvmPhaseState::Done && callback_order(build_result) == std::vector<fsim::runtime::SystemVerilogClassHandle> { api_component_top, api_component_child, api_component_isolated } && api_sequences.role_snapshot(api_agent_role).agent_mode == fsim::runtime::SystemVerilogUvmAgentMode::Active && api_sequences.role_snapshot(api_passive_agent_role).agent_mode == fsim::runtime::SystemVerilogUvmAgentMode::Passive));
        const std::array remaining_function_phases {
            fsim::runtime::SystemVerilogUvmPhaseKind::Connect,
            fsim::runtime::SystemVerilogUvmPhaseKind::EndOfElaboration,
            fsim::runtime::SystemVerilogUvmPhaseKind::StartOfSimulation,
            fsim::runtime::SystemVerilogUvmPhaseKind::Extract,
            fsim::runtime::SystemVerilogUvmPhaseKind::Check,
            fsim::runtime::SystemVerilogUvmPhaseKind::Report,
            fsim::runtime::SystemVerilogUvmPhaseKind::Final
        };
        for (const auto kind : remaining_function_phases) {
            const auto phase_result = simulation.execute_uvm_function_phase(api_schedule.phase(kind));
            assert((phase_result.success() && phase_result.final_state == fsim::runtime::SystemVerilogUvmPhaseState::Done && callback_order(phase_result) == std::vector<fsim::runtime::SystemVerilogClassHandle> { api_component_child, api_component_top, api_component_isolated }));
        }
        for (const auto component :
            { api_component_top, api_component_child, api_component_isolated }) {
            assert(
                simulation
                        .read_class_property(component, test_uvm_component_identity + "::phase_callbacks")
                        .packed.low_word()
                        .aval
                    == 32
                && simulation
                        .read_class_property(component, test_uvm_component_identity + "::phase_signature")
                        .packed.low_word()
                        .aval
                    == 36);
        }
        assert(
            std::ranges::any_of(api_uvm_activity_callbacks, [](const auto& event) {
                return event.kind == fsim::runtime::SystemVerilogUvmActivityKind::PhaseState && event.action == fsim::runtime::SystemVerilogUvmActivityAction::Completed;
            }));
        assert(std::ranges::any_of(
            simulation.uvm_activity().failures(), [](const auto& failure) {
                return failure.message.find("foreign activity callback failed") != std::string::npos;
            }));
        const std::array task_phases {
            fsim::runtime::SystemVerilogUvmPhaseKind::Run,
            fsim::runtime::SystemVerilogUvmPhaseKind::PreReset,
            fsim::runtime::SystemVerilogUvmPhaseKind::Reset,
            fsim::runtime::SystemVerilogUvmPhaseKind::PostReset,
            fsim::runtime::SystemVerilogUvmPhaseKind::PreConfigure,
            fsim::runtime::SystemVerilogUvmPhaseKind::Configure,
            fsim::runtime::SystemVerilogUvmPhaseKind::PostConfigure,
            fsim::runtime::SystemVerilogUvmPhaseKind::PreMain,
            fsim::runtime::SystemVerilogUvmPhaseKind::Main,
            fsim::runtime::SystemVerilogUvmPhaseKind::PostMain,
            fsim::runtime::SystemVerilogUvmPhaseKind::PreShutdown,
            fsim::runtime::SystemVerilogUvmPhaseKind::Shutdown,
            fsim::runtime::SystemVerilogUvmPhaseKind::PostShutdown
        };
        std::vector<fsim::runtime::SystemVerilogUvmPhaseProcessSnapshot>
            api_run_final_processes;
        for (const auto kind : task_phases) {
            const auto phase_result = simulation.execute_uvm_task_phase(api_schedule.phase(kind));
            if (kind == fsim::runtime::SystemVerilogUvmPhaseKind::Run) {
                api_run_final_processes = phase_result.final_processes;
            }
            assert(
                (phase_result.success() && phase_result.final_state == fsim::runtime::SystemVerilogUvmPhaseState::Done && callback_order(phase_result) == std::vector<fsim::runtime::SystemVerilogClassHandle> { api_component_top, api_component_child, api_component_isolated } && phase_result.processes.size() == 3 && phase_result.final_processes.size() == (kind == fsim::runtime::SystemVerilogUvmPhaseKind::Run ? 5U : 3U)));
            for (const auto& process : phase_result.final_processes) {
                assert(process.state == fsim::runtime::SystemVerilogUvmPhaseProcessState::Completed);
            }
        }
        assert((api_driver_role_tasks == 1 && api_agent_role_callbacks != 0 && api_driver_role_callbacks != 0 && api_passive_agent_role_callbacks != 0 && api_sequences.role_snapshot(api_driver_role).state == fsim::runtime::SystemVerilogUvmSequenceRoleState::Stopped && !api_sequences.role_snapshot(api_driver_role).objection_raised));
        assert(api_virtual_probe.verify(api_run_final_processes));
        for (const auto expected :
            { fsim::runtime::SystemVerilogUvmActivityKind::Graph,
                fsim::runtime::SystemVerilogUvmActivityKind::PhaseState,
                fsim::runtime::SystemVerilogUvmActivityKind::Connection,
                fsim::runtime::SystemVerilogUvmActivityKind::Transaction,
                fsim::runtime::SystemVerilogUvmActivityKind::Fifo }) {
            assert(std::ranges::any_of(
                simulation.uvm_activity().events(),
                [&](const auto& event) { return event.kind == expected; }));
        }
        for (const auto component :
            { api_component_top, api_component_child, api_component_isolated }) {
            assert(
                simulation
                        .read_class_property(component, test_uvm_component_identity + "::phase_callbacks")
                        .packed.low_word()
                        .aval
                    == 84
                && simulation
                        .read_class_property(component, test_uvm_component_identity + "::phase_signature")
                        .packed.low_word()
                        .aval
                    == 244);
        }
        const auto live_before_duplicate = simulation.class_heap().live_objects();
        bool duplicate_component_rejected { };
        try {
            (void)simulation.allocate_uvm_component(
                test_uvm_component_specialization, "api_child", api_component_top,
                api_component_root, uvm_component_identity);
        } catch (const std::invalid_argument&) {
            duplicate_component_rejected = true;
        }
        assert(duplicate_component_rejected && simulation.class_heap().live_objects() == live_before_duplicate);
        const auto& uvm_registry = simulation.uvm_registry();
        const auto item_wrapper = uvm_registry.wrapper_by_specialization(uvm_item_specialization);
        const auto component_wrapper = uvm_registry.wrapper_by_specialization(
            test_uvm_component_specialization);
        const auto param_item_wrapper = uvm_registry.wrapper_by_specialization(uvm_param_item_specialization);
        const auto factory_item_wrapper = uvm_registry.wrapper_by_specialization(uvm_factory_item_specialization);
        assert(uvm_registry.size() == 4 && item_wrapper != 0 && component_wrapper != 0 && param_item_wrapper != 0 && factory_item_wrapper != 0 && item_wrapper != component_wrapper && param_item_wrapper != item_wrapper && param_item_wrapper != component_wrapper && uvm_registry.wrapper_by_name("UvmItem") == item_wrapper && uvm_registry.wrapper_by_name("UvmParamItem#(WIDTH=8)") == param_item_wrapper && uvm_registry.wrapper_by_name("UvmFactoryItem") == factory_item_wrapper && uvm_registry.wrapper_by_name("UvmComponent") == component_wrapper && uvm_registry.snapshot(item_wrapper).kind == fsim::runtime::SystemVerilogUvmRegisteredKind::Object && uvm_registry.snapshot(component_wrapper).kind == fsim::runtime::SystemVerilogUvmRegisteredKind::Component && uvm_registry.snapshot(param_item_wrapper).parameterized);
        const auto registered_object = simulation.uvm_registry().create_object_by_type(item_wrapper,
            "registered_object");
        const auto registered_component = simulation.uvm_registry().create_component_by_name(
            "UvmComponent", "registered_child", api_component_top,
            api_component_root);
        const auto registered_param_object = simulation.uvm_registry().create_object_by_name(
            "UvmParamItem#(WIDTH=8)", "registered_param");
        auto& uvm_factory = simulation.uvm_factory();
        assert(uvm_factory.set_type_override_by_type(item_wrapper,
            param_item_wrapper));
        assert(uvm_factory.set_instance_override_by_type(
            item_wrapper, factory_item_wrapper, "factory_scope.factory_item"));
        const auto factory_debug = uvm_factory.debug_resolve_by_name(
            "UvmItem", "factory_scope.factory_item");
        const auto factory_object = uvm_factory.create_object_by_type(
            item_wrapper, "factory_scope", "factory_item");
        const auto factory_report = uvm_factory.report(true);
        auto& uvm_resources = simulation.uvm_resources();
        fsim::runtime::SystemVerilogUvmResourceDescriptor resource_descriptor;
        resource_descriptor.name = "timeout";
        resource_descriptor.scope_pattern = "api_top.*";
        resource_descriptor.type = {
            "uvm_resource#(logic[15:0])",
            fsim::runtime::SystemVerilogUvmResourceValueKind::Packed, 16
        };
        resource_descriptor.value = fsim::runtime::PackedLogic4::from_aval_bval(16, 0x1595, 0);
        const auto timeout_resource = uvm_resources.insert(std::move(resource_descriptor));
        std::vector<fsim::runtime::SystemVerilogUvmResourceCallbackEvent>
            shared_resource_callbacks;
        (void)uvm_resources.add_callback(
            timeout_resource, [&](const auto event, const auto& resource) {
                assert(resource.handle == timeout_resource);
                shared_resource_callbacks.push_back(event);
            });
        assert((
            uvm_resources.get_by_name("api_top.child", "timeout") == timeout_resource && uvm_resources.write(timeout_resource, fsim::runtime::PackedLogic4::from_aval_bval(16, 0x2020, 0), "api-first.api_top.child") && std::get<fsim::runtime::PackedLogic4>(uvm_resources.read(timeout_resource, "api-second.api_top.child")).low_word().aval == 0x2020 && uvm_resources.snapshot(timeout_resource).revision == 1 && uvm_resources.audit_records().size() == 2 && uvm_resources.audit_records()[0].accessor == "api-first.api_top.child" && uvm_resources.audit_records()[1].accessor == "api-second.api_top.child" && shared_resource_callbacks == std::vector { fsim::runtime::SystemVerilogUvmResourceCallbackEvent::PreWrite, fsim::runtime::SystemVerilogUvmResourceCallbackEvent::PostWrite, fsim::runtime::SystemVerilogUvmResourceCallbackEvent::PreRead, fsim::runtime::SystemVerilogUvmResourceCallbackEvent::PostRead }));
        auto& uvm_config_db = simulation.uvm_config_db();
        const fsim::runtime::SystemVerilogUvmConfigContext config_root { "", 0 };
        const fsim::runtime::SystemVerilogUvmConfigContext config_child { "api_top",
            1 };
        const auto config_type = fsim::runtime::SystemVerilogUvmResourceType {
            "uvm_config_db#(logic[15:0])",
            fsim::runtime::SystemVerilogUvmResourceValueKind::Packed, 16
        };
        (void)uvm_config_db.set(
            config_root, "api_top.*", "limit", config_type,
            fsim::runtime::PackedLogic4::from_aval_bval(16, 0x1595, 0),
            fsim::runtime::SystemVerilogUvmConfigPhase::Build);
        (void)uvm_config_db.set(
            config_child, "*", "limit", config_type,
            fsim::runtime::PackedLogic4::from_aval_bval(16, 0x1111, 0),
            fsim::runtime::SystemVerilogUvmConfigPhase::Build);
        assert(std::get<fsim::runtime::PackedLogic4>(
                   *uvm_config_db.get(config_child, "child", "limit",
                       config_type.identity))
                   .low_word()
                   .aval
            == 0x1595);
        std::vector<unsigned> config_wake_order;
        (void)uvm_config_db.wait_modified(
            config_child, "child", "limit",
            [&](const auto, const auto&) { config_wake_order.push_back(1); });
        (void)uvm_config_db.wait_modified(
            config_child, "child", "limit",
            [&](const auto, const auto&) { config_wake_order.push_back(2); });
        (void)uvm_config_db.set(
            config_child, "child", "limit", config_type,
            fsim::runtime::PackedLogic4::from_aval_bval(16, 0x2020, 0),
            fsim::runtime::SystemVerilogUvmConfigPhase::Runtime);
        assert(config_wake_order == std::vector<unsigned>({ 1, 2 }) && uvm_config_db.exists(config_child, "child", "limit", config_type.identity) && std::get<fsim::runtime::PackedLogic4>(*uvm_config_db.get(config_child, "child", "limit", config_type.identity)).low_word().aval == 0x2020);
        for (const auto accessor : { "api-first", "api-second" }) {
            assert(std::get<fsim::runtime::PackedLogic4>(
                       *uvm_config_db.get(config_child, "child", "limit",
                           config_type.identity, accessor))
                       .low_word()
                       .aval
                == 0x2020);
        }
        auto& uvm_command_line = simulation.uvm_command_line();
        const std::vector<std::string> uvm_plusargs {
            "+uvm_set_type_override=UvmFactoryItem,UvmParamItem#(WIDTH=8),1",
            "+uvm_set_config_string=api_top.*,command_mode,first",
            "+uvm_set_config_string=api_top.*,command_mode,second",
            "+UVM_VERBOSITY=UVM_HIGH",
            "+UVM_VERBOSITY=UVM_LOW",
            "+UVM_TIMEOUT=25,NO",
            "+UVM_RESOURCE_DB_TRACE",
            "+APP_PRIVATE=retained"
        };
        uvm_command_line.apply(uvm_plusargs);
        const auto& command_settings = uvm_command_line.settings();
        const auto command_config = uvm_config_db.get(config_root, "api_top.child", "command_mode",
            fsim::runtime::kSystemVerilogUvmStringConfigType);
        assert(uvm_factory.debug_resolve_by_name("UvmFactoryItem").resolved == param_item_wrapper && command_config && std::get<std::string>(*command_config) == "second" && command_settings.arguments == uvm_plusargs && command_settings.unknown_arguments == std::vector<std::string> { "+APP_PRIVATE=retained" } && command_settings.initial_verbosity == 300 && command_settings.initial_verbosity_argument_count == 2 && command_settings.timeout && command_settings.timeout->ticks == 25 && !command_settings.timeout->overridable && command_settings.resource_db_trace);
        assert(uvm_factory.debug_resolve_by_name("UvmItem", "api-first.api_top")
                    .resolved
                == param_item_wrapper
            && uvm_factory.debug_resolve_by_name("UvmItem", "api-second.api_top")
                    .resolved
                == param_item_wrapper);

        auto& uvm_reports = simulation.uvm_reports();
        uvm_reports.set_default_verbosity(*command_settings.initial_verbosity);
        std::vector<fsim::runtime::SystemVerilogUvmReportMessage> routed_reports;
        uvm_reports.set_route_hook(
            [&](const auto& message) { routed_reports.push_back(message); });
        fsim::runtime::SystemVerilogUvmReportRequest api_report;
        api_report.report_object = api_component_child;
        api_report.severity = fsim::runtime::SystemVerilogUvmReportSeverity::Info;
        api_report.id = "API_REPORT";
        api_report.message = "component ready";
        api_report.verbosity = 300;
        api_report.filename = "classes.sv";
        api_report.line = 159;
        api_report.context = "api-first";
        api_report.elements.add_string("engine", "classes");
        api_report.elements.add_object("item", uvm_child);
        assert(uvm_reports.report(api_report));
        auto filtered_report = api_report;
        filtered_report.verbosity = 301;
        assert(!uvm_reports.report(filtered_report));
        auto isolated_report = api_report;
        isolated_report.report_object = api_component_isolated;
        isolated_report.severity = fsim::runtime::SystemVerilogUvmReportSeverity::Warning;
        isolated_report.verbosity = 10'000;
        isolated_report.context = "api-second";
        assert(uvm_reports.report(isolated_report));
        auto prechecked_report = api_report;
        prechecked_report.verbosity = 10'000;
        prechecked_report.report_enabled_checked = true;
        prechecked_report.context = "prechecked";
        assert(uvm_reports.report(prechecked_report));
        const auto api_report_payload = uvm_reports.compose_payload(routed_reports.front());
        assert(routed_reports.size() == 3 && routed_reports[0].sequence == 1 && routed_reports[0].report_object == api_component_child && routed_reports[0].report_object_name == "api_top.api_child" && routed_reports[0].id == "API_REPORT" && routed_reports[0].filename == "classes.sv" && routed_reports[0].line == 159 && routed_reports[0].context == "api-first" && routed_reports[1].sequence == 2 && routed_reports[1].report_object == api_component_isolated && routed_reports[1].report_object_name == "api_top" && routed_reports[1].context == "api-second" && routed_reports[2].sequence == 3 && routed_reports[2].context == "prechecked" && api_report_payload.find("component ready\n +engine = \"classes\"") == 0 && api_report_payload.find("\n +item = @") != std::string::npos && api_report_payload.find("{api_child}") != std::string::npos && uvm_reports.default_verbosity() == 300 && uvm_reports.routed_count() == 3 && uvm_reports.filtered_count() == 1);
        uvm_reports.set_verbosity_hier(api_component_top, 150);
        uvm_reports.set_id_verbosity_hier(api_component_top, "APP_HANDLER", 140);
        uvm_reports.set_severity_id_verbosity_hier(
            api_component_top,
            fsim::runtime::SystemVerilogUvmReportSeverity::Warning, "APP_HANDLER",
            130);
        uvm_reports.set_severity_id_action_hier(
            api_component_top,
            fsim::runtime::SystemVerilogUvmReportSeverity::Warning, "APP_HANDLER",
            fsim::runtime::SystemVerilogUvmReportAction::Log);
        uvm_reports.set_severity_id_file_hier(
            api_component_top,
            fsim::runtime::SystemVerilogUvmReportSeverity::Warning, "APP_HANDLER",
            99);
        const auto child_report_policy = uvm_reports.policy(
            api_component_child,
            fsim::runtime::SystemVerilogUvmReportSeverity::Warning, "APP_HANDLER");
        const auto isolated_report_policy = uvm_reports.policy(
            api_component_isolated,
            fsim::runtime::SystemVerilogUvmReportSeverity::Warning, "APP_HANDLER");
        assert(child_report_policy.verbosity == 130 && child_report_policy.action == fsim::runtime::SystemVerilogUvmReportAction::Log && child_report_policy.file == 99 && isolated_report_policy.verbosity == 300 && isolated_report_policy.action == fsim::runtime::SystemVerilogUvmReportAction::Display && isolated_report_policy.file == 0 && uvm_reports.handler_count() == 3 && uvm_reports.setting_count() == 12);
        std::vector<std::string> report_display_records;
        std::vector<std::pair<std::uint64_t, std::string>> report_file_records;
        std::size_t report_record_count { };
        std::vector<fsim::runtime::SystemVerilogUvmReportExecution>
            report_control_events;
        uvm_reports.server().set_show_verbosity(true);
        uvm_reports.server().set_show_terminator(true);
        uvm_reports.server().set_display_sink([&](const auto record) {
            report_display_records.emplace_back(record);
        });
        uvm_reports.server().set_file_sink([&](const auto file, const auto record) {
            report_file_records.emplace_back(file, record);
        });
        uvm_reports.server().set_record_sink(
            [&](const auto&) { ++report_record_count; });
        uvm_reports.server().set_control_sink([&](const auto& execution) {
            report_control_events.push_back(execution);
        });
        assert(uvm_reports.server().set_max_quit_count(1, false));
        uvm_reports.set_severity_id_action(
            api_component_child,
            fsim::runtime::SystemVerilogUvmReportSeverity::Warning, "APP_SERVER",
            fsim::runtime::SystemVerilogUvmReportAction::Display | fsim::runtime::SystemVerilogUvmReportAction::Log | fsim::runtime::SystemVerilogUvmReportAction::Record | fsim::runtime::SystemVerilogUvmReportAction::Count);
        uvm_reports.set_severity_id_file(
            api_component_child,
            fsim::runtime::SystemVerilogUvmReportSeverity::Warning, "APP_SERVER",
            3);
        fsim::runtime::SystemVerilogUvmReportRequest server_report;
        server_report.report_object = api_component_child;
        server_report.severity = fsim::runtime::SystemVerilogUvmReportSeverity::Warning;
        server_report.id = "APP_SERVER";
        server_report.message = "server action";
        server_report.verbosity = 0;
        server_report.filename = "classes.sv";
        server_report.line = 615;
        server_report.timestamp = 17;
        server_report.context = "api-server";
        assert(uvm_reports.report(server_report));
        const auto expected_server_record = std::string { "UVM_WARNING(UVM_NONE) classes.sv(615) @ 17: "
                                                          "api_top.api_child@@api-server [APP_SERVER] server action "
                                                          "-UVM_WARNING\n" };
        assert((routed_reports.size() == 4 && routed_reports.back().sequence == 4 && fsim::runtime::has_action(routed_reports.back().action, fsim::runtime::SystemVerilogUvmReportAction::Exit) && report_display_records == std::vector<std::string> { expected_server_record } && report_file_records == std::vector<std::pair<std::uint64_t, std::string>> { { 2, expected_server_record } } && report_record_count == 1 && report_control_events.size() == 1 && report_control_events.front().exit_requested && uvm_reports.server().quit_count() == 1 && uvm_reports.server().severity_count(fsim::runtime::SystemVerilogUvmReportSeverity::Info) == 2 && uvm_reports.server().severity_count(fsim::runtime::SystemVerilogUvmReportSeverity::Warning) == 2 && uvm_reports.server().id_count("APP_SERVER") == 1));

        std::vector<std::string> application_catcher_order;
        const auto application_global_catcher = uvm_reports.add_catcher(
            0, "application-global",
            [&](auto& context) {
                application_catcher_order.push_back("global:" + context.message().id);
                context.set_severity(
                    fsim::runtime::SystemVerilogUvmReportSeverity::Info);
                context.set_message("modified by application catcher");
                context.elements().add_string("engine", "catcher");
                return fsim::runtime::SystemVerilogUvmReportCatcherResult::Throw;
            },
            fsim::runtime::SystemVerilogUvmReportCatcherOrdering::Prepend);
        const auto application_instance_catcher = uvm_reports.add_catcher(
            api_component_child, "application-instance", [&](auto& context) {
                application_catcher_order.push_back("instance:" + context.message().id);
                assert(context.message().severity == fsim::runtime::SystemVerilogUvmReportSeverity::Info && context.message().message == "modified by application catcher" && context.message().elements.size() == 1);
                return fsim::runtime::SystemVerilogUvmReportCatcherResult::Caught;
            });
        const auto application_isolated_catcher = uvm_reports.add_catcher(
            api_component_isolated, "application-isolated", [&](auto&) {
                application_catcher_order.push_back("isolated");
                return fsim::runtime::SystemVerilogUvmReportCatcherResult::Throw;
            });
        (void)application_global_catcher;
        (void)application_instance_catcher;
        (void)application_isolated_catcher;
        auto caught_application_report = server_report;
        caught_application_report.id = "APP_CATCH";
        caught_application_report.message = "catch me";
        assert((!uvm_reports.report(caught_application_report) && application_catcher_order == std::vector<std::string> { "global:APP_CATCH", "instance:APP_CATCH" } && routed_reports.size() == 4 && uvm_reports.routed_count() == 4 && uvm_reports.catcher_count() == 3 && uvm_reports.catcher_invocations() == 2 && uvm_reports.caught_count(fsim::runtime::SystemVerilogUvmReportSeverity::Warning) == 1 && uvm_reports.demoted_count(fsim::runtime::SystemVerilogUvmReportSeverity::Warning) == 1 && uvm_reports.server().id_count("APP_CATCH") == 0));

        if (engine == fsim::app::SimulationEngine::interpreter) {
            const auto make_fresh_context = [&]() {
                fsim::diagnostic::Engine peer_diagnostics;
                auto peer_project = fsim::app::build_project(config, peer_diagnostics);
                assert(peer_project && !peer_diagnostics.has_error());
                return fsim::app::Simulation(std::move(*peer_project),
                    config.run.max_deltas,
                    fsim::app::SimulationEngine::interpreter);
            };
            const auto assert_fresh_context = [&](const auto& context) {
                assert(context.uvm_registry().size() == 4 && context.uvm_factory().type_overrides().empty() && context.uvm_factory().instance_overrides().empty() && context.uvm_resources().size() == 0 && context.uvm_config_db().entries().empty() && context.uvm_config_db().waiter_count() == 0 && context.uvm_command_line().settings().arguments.empty() && context.uvm_reports().default_verbosity() == 200 && context.uvm_reports().routed_count() == 0 && context.uvm_reports().filtered_count() == 0 && context.uvm_reports().handler_count() == 0 && context.uvm_reports().setting_count() == 0 && context.uvm_reports().catcher_count() == 0 && context.uvm_reports().catcher_invocations() == 0 && context.uvm_reports().caught_count(fsim::runtime::SystemVerilogUvmReportSeverity::Warning) == 0 && context.uvm_reports().server().severity_count(fsim::runtime::SystemVerilogUvmReportSeverity::Info) == 0 && context.uvm_components().roots().empty() && context.uvm_sequences().sequencers().empty() && context.uvm_sequences().sequences().empty() && context.uvm_sequences().items().empty() && context.uvm_sequences().roles().empty() && context.uvm_tlm1().endpoints().empty() && context.uvm_tlm2().sockets().empty() && context.uvm_phases().standard_schedule() && context.uvm_phases().domains().size() == 2 && context.uvm_phases().snapshot(context.uvm_phases().standard_schedule()->common_domain).roots.empty() && context.uvm_phases().snapshot(context.uvm_phases().standard_schedule()->runtime_domain).roots.empty());
            };
            std::size_t peer_callback_count { };
            {
                auto peer = make_fresh_context();
                assert_fresh_context(peer);
                const auto peer_root = peer.create_uvm_root("api-first");
                const auto peer_top = peer.allocate_uvm_component(
                    test_uvm_component_specialization, "api_top", 0, peer_root,
                    uvm_component_identity);
                const auto peer_sequence_object = peer.allocate_uvm_object(
                    uvm_item_specialization, "peer_sequence", uvm_object_identity);
                const auto peer_sequencer = peer.uvm_sequences().register_sequencer(
                    { peer_top, test_uvm_component_specialization,
                        api_sequence_profile });
                fsim::runtime::SystemVerilogUvmSequenceDescriptor
                    peer_sequence_descriptor;
                peer_sequence_descriptor.object = peer_sequence_object;
                peer_sequence_descriptor.name = "peer_sequence";
                peer_sequence_descriptor.nominal_type = uvm_item_specialization;
                peer_sequence_descriptor.profile = api_sequence_profile;
                peer_sequence_descriptor.sequencer = peer_sequencer;
                const auto peer_sequence = peer.uvm_sequences().register_sequence(
                    std::move(peer_sequence_descriptor));
                const auto peer_schedule = *peer.uvm_phases().standard_schedule();
                const auto peer_phase_domain = peer_schedule.common_domain;
                const auto peer_build_phase = peer_schedule.phase(
                    fsim::runtime::SystemVerilogUvmPhaseKind::Build);
                std::size_t peer_report_count { };
                peer.uvm_reports().set_default_verbosity(100);
                peer.uvm_reports().set_route_hook(
                    [&](const auto&) { ++peer_report_count; });
                fsim::runtime::SystemVerilogUvmReportRequest peer_report;
                peer_report.report_object = peer_top;
                peer_report.id = "PEER";
                peer_report.message = "peer report";
                peer_report.verbosity = 101;
                assert(!peer.uvm_reports().report(peer_report));
                peer_report.severity = fsim::runtime::SystemVerilogUvmReportSeverity::Warning;
                assert(peer.uvm_reports().report(peer_report));
                assert(peer.uvm_components().lookup_root(peer_root, "api_top") == peer_top && peer.uvm_phases().contains(peer_phase_domain) && peer.uvm_phases().contains(peer_build_phase) && !uvm_phases.contains(peer_phase_domain) && !peer.uvm_phases().contains(api_phase_domain) && peer.uvm_sequences().contains(peer_sequence) && !api_sequences.contains(peer_sequence) && !peer.uvm_sequences().contains(api_sequence) && simulation.uvm_components().lookup_root(api_component_root, "api_top") == api_component_top && peer_report_count == 1 && peer.uvm_reports().default_verbosity() == 100 && peer.uvm_reports().routed_count() == 1 && peer.uvm_reports().filtered_count() == 1 && uvm_reports.default_verbosity() == 300 && uvm_reports.routed_count() == 4 && uvm_reports.filtered_count() == 1);

                const auto peer_item = peer.uvm_registry().wrapper_by_name("UvmItem");
                const auto peer_factory_item = peer.uvm_registry().wrapper_by_name("UvmFactoryItem");
                assert(peer.uvm_factory().set_type_override_by_type(peer_item,
                    peer_factory_item));
                assert(peer.uvm_factory().debug_resolve_by_name("UvmItem").resolved == peer_factory_item && simulation.uvm_factory().debug_resolve_by_name("UvmItem").resolved == param_item_wrapper);

                fsim::runtime::SystemVerilogUvmResourceDescriptor peer_resource;
                peer_resource.name = "timeout";
                peer_resource.scope_pattern = "api_top.*";
                peer_resource.type = config_type;
                peer_resource.value = fsim::runtime::PackedLogic4::from_aval_bval(16, 0xbeef, 0);
                const auto peer_timeout = peer.uvm_resources().insert(std::move(peer_resource));
                (void)peer.uvm_resources().add_callback(
                    peer_timeout,
                    [&](const auto, const auto&) { ++peer_callback_count; });
                assert(peer.uvm_resources().write(
                    peer_timeout,
                    fsim::runtime::PackedLogic4::from_aval_bval(16, 0xcafe, 0),
                    "peer.api_top.child"));
                (void)peer.uvm_config_db().set(
                    config_child, "child", "limit", config_type,
                    fsim::runtime::PackedLogic4::from_aval_bval(16, 0xbeef, 0),
                    fsim::runtime::SystemVerilogUvmConfigPhase::Runtime);
                const std::vector<std::string> peer_plusargs {
                    "+uvm_set_config_string=api_top.*,command_mode,peer",
                    "+UVM_TIMEOUT=7,YES"
                };
                peer.uvm_command_line().apply(peer_plusargs);
                assert(
                    peer_callback_count == 2 && shared_resource_callbacks.size() == 4 && std::get<fsim::runtime::PackedLogic4>(peer.uvm_resources().read(peer_timeout, "peer.api_top.child")).low_word().aval == 0xcafe && std::get<fsim::runtime::PackedLogic4>(*peer.uvm_config_db().get(config_child, "child", "limit", config_type.identity)).low_word().aval == 0xbeef && std::get<fsim::runtime::PackedLogic4>(*uvm_config_db.get(config_child, "child", "limit", config_type.identity)).low_word().aval == 0x2020 && peer.uvm_command_line().settings().timeout->ticks == 7 && command_settings.timeout->ticks == 25);
                (void)uvm_resources.read(timeout_resource,
                    "api-first.api_top.restart-check");
                assert(shared_resource_callbacks.size() == 6 && peer_callback_count == 4);
            }
            {
                auto restarted = make_fresh_context();
                assert_fresh_context(restarted);
                const auto restarted_root = restarted.create_uvm_root("api-first");
                assert(restarted_root == 1 && restarted.uvm_factory().type_overrides().empty() && restarted.uvm_resources().size() == 0 && restarted.uvm_config_db().entries().empty() && restarted.uvm_command_line().settings().arguments.empty() && restarted.uvm_reports().default_verbosity() == 200 && restarted.uvm_reports().routed_count() == 0);
            }
            assert(uvm_factory.debug_resolve_by_name("UvmItem").resolved == param_item_wrapper && std::get<fsim::runtime::PackedLogic4>(*uvm_config_db.get(config_child, "child", "limit", config_type.identity)).low_word().aval == 0x2020 && command_settings.arguments == uvm_plusargs && uvm_reports.routed_count() == 4 && uvm_reports.filtered_count() == 1);
        }
        assert(
            simulation.uvm_objects().name(registered_object) == "registered_object" && simulation.uvm_objects().type_name(registered_object) == "UvmItem" && simulation.uvm_objects().name(registered_param_object) == "registered_param" && factory_debug.resolved == factory_item_wrapper && factory_debug.steps.size() == 1 && factory_debug.steps.front().kind == fsim::runtime::SystemVerilogUvmOverrideKind::Instance && simulation.class_heap().object(factory_object).specialization_identity == uvm_factory_item_specialization && simulation.uvm_objects().name(factory_object) == "factory_item" && factory_report.find("UvmItem @ factory_scope.factory_item -> UvmFactoryItem uses=1") != std::string::npos && simulation.uvm_components().full_name(registered_component) == "api_top.registered_child");
        simulation.deposit_class_property(
            uvm_root, uvm_item_identity + "::value",
            fsim::runtime::PackedLogic4::from_aval_bval(16, 0x1595, 0));
        simulation.deposit_class_property(
            uvm_child, uvm_item_identity + "::value",
            fsim::runtime::PackedLogic4::from_aval_bval(16, 0x2020, 0));
        simulation.class_heap()
            .property(uvm_root, uvm_item_identity + "::child")
            .handle = uvm_child;
        simulation.class_heap()
            .property(uvm_child, uvm_item_identity + "::child")
            .handle = uvm_root;
        const auto uvm_clone = simulation.uvm_objects().clone(uvm_root);
        assert(simulation.uvm_objects().name(uvm_clone) == "api_root" && simulation.uvm_objects().type_name(uvm_clone) == "UvmItem" && simulation.uvm_objects().compare(uvm_root, uvm_clone));
        const auto uvm_print = simulation.uvm_objects().print(uvm_clone);
        const auto uvm_record = simulation.uvm_objects().record(uvm_clone);
        assert(std::ranges::any_of(uvm_print, [](const auto& entry) {
            return entry.kind == fsim::runtime::SystemVerilogUvmObjectEntryKind::Cycle && entry.value == "api_root";
        }));
        assert(!uvm_record.empty());
        std::size_t source_uvm_prints { };
        std::size_t source_uvm_records { };
        simulation.uvm_objects().set_print_hook([&](const auto entries) {
            assert(!entries.empty());
            ++source_uvm_prints;
        });
        simulation.uvm_objects().set_record_hook([&](const auto entries) {
            assert(!entries.empty());
            ++source_uvm_records;
        });
        assert(simulation.class_heap()
                   .random_state(handle, derived_identity + "::value")
                   .kind
            == fsim::runtime::SystemVerilogClassRandomKind::Rand);
        const auto& generated_random = simulation.class_heap().random_state(
            handle, derived_identity + "::generated_value");
        assert(generated_random.kind == fsim::runtime::SystemVerilogClassRandomKind::Randc);
        assert(generated_random.width == 4);
        assert(generated_random.nominal_type == "logic");
        simulation.deposit_class_property(
            handle, base_identity + "::value",
            fsim::runtime::PackedLogic4::from_aval_bval(8, 0, 0));
        simulation.deposit_class_property(
            handle, derived_identity + "::value",
            fsim::runtime::PackedLogic4::from_aval_bval(8, 0, 0));
        auto& shared = simulation.class_static_store()
                           .property(base_identity, "shared")
                           .packed;
        assert(shared.low_word().aval == 2);
        shared = fsim::runtime::PackedLogic4::from_aval_bval(64, 5, 0);

        fsim::runtime::SystemVerilogClassMethodDescriptor base_method;
        base_method.canonical_identity = base_method_identity;
        base_method.owner_type = base_identity;
        base_method.arguments = {
            fsim::runtime::SystemVerilogClassArgumentMode::Input
        };
        base_method.virtual_slot = *virtual_slot;
        base_method.entry = [](auto& frame) {
            const auto amount = frame.argument(0).packed.low_word().aval;
            auto& value = frame.property("value").packed;
            value = fsim::runtime::PackedLogic4::from_aval_bval(
                8, value.low_word().aval + amount, 0);
            return fsim::runtime::SystemVerilogClassMethodStatus::Completed;
        };
        simulation.class_methods().register_method(std::move(base_method));
        fsim::runtime::SystemVerilogClassMethodDescriptor derived_override;
        derived_override.canonical_identity = derived_method_identity;
        derived_override.owner_type = derived_identity;
        derived_override.arguments = {
            fsim::runtime::SystemVerilogClassArgumentMode::Input
        };
        derived_override.virtual_slot = *virtual_slot;
        derived_override.entry = [](auto& frame) {
            const auto amount = frame.argument(0).packed.low_word().aval;
            auto& value = frame.property("value").packed;
            value = fsim::runtime::PackedLogic4::from_aval_bval(
                8, value.low_word().aval + amount + 1, 0);
            return fsim::runtime::SystemVerilogClassMethodStatus::Completed;
        };
        simulation.class_methods().register_method(std::move(derived_override));
        const auto suspended_method_identity = base_identity + "::debug_suspend";
        fsim::runtime::SystemVerilogClassMethodDescriptor suspended_method;
        suspended_method.canonical_identity = suspended_method_identity;
        suspended_method.owner_type = base_identity;
        suspended_method.arguments = {
            fsim::runtime::SystemVerilogClassArgumentMode::Input
        };
        suspended_method.automatic_value_count = 1;
        suspended_method.is_task = true;
        suspended_method.entry = [](auto& frame) {
            if (frame.continuation_point() == 0) {
                frame.local(0).packed = fsim::runtime::PackedLogic4::from_aval_bval(8, 42, 0);
                frame.suspend_at(7);
                return fsim::runtime::SystemVerilogClassMethodStatus::Suspended;
            }
            return fsim::runtime::SystemVerilogClassMethodStatus::Completed;
        };
        simulation.class_methods().register_method(std::move(suspended_method));

        std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>>
            property_changes;
        std::vector<std::tuple<fsim::runtime::SystemVerilogClassHandle, std::string,
            std::string>>
            all_property_changes;
        std::vector<std::tuple<fsim::runtime::SystemVerilogClassHandle,
            std::uint64_t, std::uint64_t, std::uint64_t>>
            randomization_callback_states;
        simulation.set_class_property_change_hook(
            [&](const auto changed_handle, const std::string_view property,
                const auto& value, const auto time, const auto delta) {
                all_property_changes.emplace_back(
                    changed_handle, std::string { property }, value.to_msb_string());
                if (property.ends_with("::generated_value")) {
                    const auto& random = simulation.class_heap().random_state(changed_handle, property);
                    randomization_callback_states.emplace_back(
                        changed_handle, random.revision, random.randc_cycle,
                        random.randc_used_values.size());
                }
                if (changed_handle != handle)
                    return;
                if (property.ends_with("::wide_value"))
                    return;
                assert(property.ends_with("::value"));
                property_changes.emplace_back(time, delta, value.to_msb_string());
            });
        std::vector<std::tuple<std::string, std::string, std::string>>
            static_changes;
        simulation.set_class_static_property_change_hook(
            [&](const std::string_view specialization,
                const std::string_view property, const auto& value, const auto,
                const auto) {
                static_changes.emplace_back(specialization, property,
                    value.to_msb_string());
            });
        auto wide_service_value = fsim::runtime::PackedLogic4 { 137, fsim::runtime::Logic4::zero };
        wide_service_value.set(136, fsim::runtime::Logic4::one);
        wide_service_value.set(91, fsim::runtime::Logic4::one);
        wide_service_value.set(5, fsim::runtime::Logic4::one);
        simulation.deposit_class_property(handle, base_identity + "::wide_value",
            wide_service_value);
        std::size_t safe_points { };
        std::size_t safe_point_live_objects { };
        const auto safe_point = simulation.add_safe_point_hook([&](const auto&, const auto) {
            ++safe_points;
            safe_point_live_objects = std::max(
                safe_point_live_objects, simulation.class_heap().live_objects());
        });
        std::vector<fsim::runtime::SystemVerilogClassMethodValue> actuals(1);
        actuals.front().packed = fsim::runtime::PackedLogic4::from_aval_bval(32, 3, 0);
        bool completion_called = false;
        std::vector<fsim::runtime::SystemVerilogClassMethodValue> base_actuals(1);
        base_actuals.front().packed = fsim::runtime::PackedLogic4::from_aval_bval(32, 2, 0);
        simulation.schedule_class_method(0, 6, base_method_identity, handle,
            std::move(base_actuals));
        simulation.schedule_class_method(
            1, 7, { }, handle, std::move(actuals), virtual_slot,
            [&](const auto& result, const auto& retained_actuals) {
                assert(result.status == fsim::runtime::SystemVerilogClassMethodStatus::Completed);
                assert(retained_actuals.size() == 1);
                completion_called = true;
            });
        if (engine == fsim::app::SimulationEngine::debug) {
            std::ifstream source_input(class_source);
            std::uint32_t source_object_line { };
            for (std::string line; std::getline(source_input, line);) {
                ++source_object_line;
                if (line.find("source_object = new(3, WIDE_SEED);") != std::string::npos) {
                    break;
                }
            }
            assert(source_object_line != 0 && source_input.good());
            std::ostringstream early_debug_output;
            std::ostringstream early_debug_error;
            {
                fsim::app::DebuggerControl early_debugger(
                    simulation, early_debug_output, early_debug_error);
                early_debugger.execute({ "break", "source",
                    class_source.filename().string() + ":" + std::to_string(source_object_line) });
                early_debugger.execute({ "continue" });
                early_debugger.execute({ "show", "class_top.source_object" });
            }
            assert(early_debug_error.str().empty());
            assert(early_debug_output.str().find("source_object = ") != std::string::npos);
            assert(early_debug_output.str().find(" declared " + derived_identity) != std::string::npos);
            simulation.clear_stop();
        }
        const auto result = simulation.run();
        assert(source_uvm_prints == 1 && source_uvm_records == 1);
        const auto source_uvm_ready = simulation.find_signal("class_top.source_uvm_constructed");
        const auto source_uvm_object = simulation.find_signal("class_top.source_uvm_object");
        const auto source_uvm_clone = simulation.find_signal("class_top.source_uvm_clone");
        const auto source_uvm_compared = simulation.find_signal("class_top.source_uvm_clone_compared");
        const auto source_uvm_copy = simulation.find_signal("class_top.source_uvm_copy");
        const auto source_uvm_copy_compared = simulation.find_signal("class_top.source_uvm_copy_compared");
        const auto source_uvm_top = simulation.find_signal("class_top.source_uvm_top");
        const auto source_uvm_child = simulation.find_signal("class_top.source_uvm_child");
        const auto source_uvm_parent_ok = simulation.find_signal("class_top.source_uvm_parent_ok");
        const auto source_uvm_child_count = simulation.find_signal("class_top.source_uvm_child_count");
        const auto source_uvm_component_clone_null = simulation.find_signal("class_top.source_uvm_component_clone_null");
        const auto source_uvm_item_type = simulation.find_signal("class_top.source_uvm_item_type");
        const auto source_uvm_component_type = simulation.find_signal("class_top.source_uvm_component_type");
        const auto source_uvm_item_wrapper_equal = simulation.find_signal("class_top.source_uvm_item_wrapper_equal");
        const auto source_uvm_component_wrapper_equal = simulation.find_signal("class_top.source_uvm_component_wrapper_equal");
        const auto source_uvm_wrapper_kinds_distinct = simulation.find_signal("class_top.source_uvm_wrapper_kinds_distinct");
        const auto source_uvm_param_type = simulation.find_signal("class_top.source_uvm_param_type");
        const auto source_uvm_param_wrapper_equal = simulation.find_signal("class_top.source_uvm_param_wrapper_equal");
        const auto source_uvm_factory_selected_override = simulation.find_signal(
            "class_top.source_uvm_factory_selected_override");
        const auto source_uvm_factory_created_override = simulation.find_signal("class_top.source_uvm_factory_created_override");
        assert(source_uvm_ready && source_uvm_object && source_uvm_clone && source_uvm_compared && source_uvm_copy && source_uvm_copy_compared && source_uvm_top && source_uvm_child && source_uvm_parent_ok && source_uvm_child_count && source_uvm_component_clone_null && source_uvm_item_type && source_uvm_component_type && source_uvm_item_wrapper_equal && source_uvm_component_wrapper_equal && source_uvm_wrapper_kinds_distinct && source_uvm_param_type && source_uvm_param_wrapper_equal && source_uvm_factory_selected_override && source_uvm_factory_created_override);
        const auto source_uvm_handle = simulation.read_signal(*source_uvm_object).low_word().aval;
        assert(simulation.read_signal(*source_uvm_ready).low_word().aval == 1);
        assert(simulation.read_signal(*source_uvm_compared).low_word().aval == 1);
        assert(simulation.read_signal(*source_uvm_copy_compared).low_word().aval == 1);
        const auto source_uvm_top_handle = simulation.read_signal(*source_uvm_top).low_word().aval;
        const auto source_uvm_child_handle = simulation.read_signal(*source_uvm_child).low_word().aval;
        assert(
            simulation.read_signal(*source_uvm_parent_ok).low_word().aval == 1 && simulation.read_signal(*source_uvm_child_count).low_word().aval == 1 && simulation.read_signal(*source_uvm_component_clone_null).low_word().aval == 1 && simulation.read_signal(*source_uvm_item_wrapper_equal).low_word().aval == 1 && simulation.read_signal(*source_uvm_component_wrapper_equal).low_word().aval == 1 && simulation.read_signal(*source_uvm_wrapper_kinds_distinct).low_word().aval == 1 && simulation.read_signal(*source_uvm_param_wrapper_equal).low_word().aval == 1 && simulation.read_signal(*source_uvm_factory_selected_override).low_word().aval == 1 && simulation.read_signal(*source_uvm_factory_created_override).low_word().aval == 1 && simulation.read_signal(*source_uvm_item_type).low_word().aval == item_wrapper && simulation.read_signal(*source_uvm_component_type).low_word().aval == component_wrapper && simulation.read_signal(*source_uvm_param_type).low_word().aval == param_item_wrapper && simulation.uvm_components().full_name(source_uvm_top_handle) == "source_top" && simulation.uvm_components().full_name(source_uvm_child_handle) == "source_top.source_child" && simulation.uvm_components().lookup(source_uvm_top_handle, "source_child") == source_uvm_child_handle);
        assert(simulation.uvm_objects().type_name(source_uvm_handle) == "UvmItem");
        assert(simulation.uvm_objects().name(source_uvm_handle).empty());
        assert(simulation
                   .read_class_property(source_uvm_handle,
                       uvm_item_identity + "::value")
                   .packed.low_word()
                   .aval
            == 0x1595);
        const auto source_uvm_clone_handle = simulation.read_signal(*source_uvm_clone).low_word().aval;
        assert(source_uvm_clone_handle != source_uvm_handle && simulation.uvm_objects().type_name(source_uvm_clone_handle) == "UvmItem" && simulation.read_class_property(source_uvm_clone_handle, uvm_item_identity + "::value").packed.low_word().aval == 0x1595);
        const auto source_uvm_copy_handle = simulation.read_signal(*source_uvm_copy).low_word().aval;
        assert(source_uvm_copy_handle != source_uvm_handle && source_uvm_copy_handle != source_uvm_clone_handle && simulation.read_class_property(source_uvm_copy_handle, uvm_item_identity + "::value").packed.low_word().aval == 0x1595);
        simulation.remove_safe_point_hook(safe_point);
        assert(result.time == 9);
        assert(safe_points != 0 && safe_point_live_objects >= 4);
        const auto source_object = simulation.find_signal("class_top.source_object");
        assert(source_object);
        const auto source_handle = simulation.read_signal(*source_object).low_word().aval;
        assert(source_handle != 0 && source_handle != handle);
        assert(simulation.class_heap().object(source_handle).dynamic_type == derived_identity);
        const auto& source_object_state = simulation.class_heap().object(source_handle);
        assert(source_object_state.random_root_identity == "class_top");
        auto source_random_stream = simulation.class_heap().random_stream(
            source_handle, derived_identity + "::randomize@source");
        const auto source_random_sample = source_random_stream.next_u64();
        assert(
            simulation.read_class_property(source_handle, base_identity + "::value")
                .packed.low_word()
                .aval
            == 5);
        assert(simulation
                   .read_class_property(source_handle, derived_identity + "::value")
                   .packed.low_word()
                   .aval
            == 18);
        auto wide_seed = fsim::runtime::PackedLogic4 { 137, fsim::runtime::Logic4::zero };
        wide_seed.set(136, fsim::runtime::Logic4::one);
        wide_seed.set(73, fsim::runtime::Logic4::one);
        wide_seed.set(3, fsim::runtime::Logic4::one);
        auto wide_amount = fsim::runtime::PackedLogic4 { 137, fsim::runtime::Logic4::zero };
        wide_amount.set(128, fsim::runtime::Logic4::one);
        wide_amount.set(7, fsim::runtime::Logic4::one);
        wide_amount.set(0, fsim::runtime::Logic4::one);
        assert(
            simulation
                .read_class_property(source_handle, base_identity + "::wide_value")
                .packed
            == wide_seed);
        const auto assert_wide_signal =
            [&](const std::string_view path,
                const fsim::runtime::PackedLogic4& expected) {
                const auto signal = simulation.find_signal(path);
                assert(signal);
                assert(simulation.read_signal(*signal) == expected);
            };
        for (const auto path :
            { "class_top.source_wide_prior", "class_top.source_wide_alias",
                "class_top.source_wide_result", "class_top.source_wide_recursive",
                "class_top.source_module_wide_prior",
                "class_top.source_module_wide_alias",
                "class_top.source_module_wide_result" }) {
            assert_wide_signal(path, wide_seed);
        }
        for (const auto path : { "class_top.source_wide_accumulator",
                 "class_top.source_wide_task_observed",
                 "class_top.source_wide_task_accumulator",
                 "class_top.source_module_wide_accumulator",
                 "class_top.source_module_wide_task_observed",
                 "class_top.source_module_wide_task_accumulator" }) {
            assert_wide_signal(path, wide_amount);
        }
        for (const auto path : { "class_top.source_wide_static_first",
                 "class_top.source_wide_static_second",
                 "class_top.source_module_wide_static_first",
                 "class_top.source_module_wide_static_second" }) {
            assert_wide_signal(path, wide_seed);
        }
        assert(std::ranges::any_of(all_property_changes, [&](const auto& change) {
            return std::get<0>(change) == source_handle && std::get<1>(change) == derived_identity + "::value" && std::get<2>(change) == "00010010";
        }));
        assert(std::ranges::any_of(static_changes, [&](const auto& change) {
            return std::get<0>(change) == base->specialization_identity && std::get<1>(change) == "shared" && std::get<2>(change) == fsim::runtime::PackedLogic4::from_aval_bval(64, 10, 0).to_msb_string();
        }));
        for (const auto& [path, expected] :
            std::vector<std::pair<std::string, std::uint64_t>> {
                { "class_top.source_prior", 3 },
                { "class_top.source_accumulator", 6 },
                { "class_top.source_alias", 6 },
                { "class_top.source_result", 12 },
                { "class_top.source_recursive", 12 },
                { "class_top.source_static_first", 1 },
                { "class_top.source_static_second", 2 },
                { "class_top.source_base_result", 5 },
                { "class_top.source_task_observed", 16 },
                { "class_top.source_task_accumulator", 17 },
                { "class_top.task_event_observed", 1 },
                { "class_top.source_virtual_result", 18 },
                { "class_top.source_static_result", 8 },
                { "class_top.source_static_task_observed", 10 },
                { "class_top.source_static_property", 10 },
                { "class_top.source_handle_alias", 1 },
                { "class_top.source_handle_property_alias", 1 },
                { "class_top.source_task_handle_alias", 1 },
                { "class_top.source_static_handle_alias", 1 },
                { "class_top.source_fixed_handle_alias", 1 },
                { "class_top.source_dynamic_handle_alias", 1 },
                { "class_top.source_queued_handle_alias", 1 },
                { "class_top.source_queue_pop_alias", 1 },
                { "class_top.source_queue_size", 1 },
                { "class_top.source_associative_handle_alias", 1 },
                { "class_top.source_cast_alias", 1 },
                { "class_top.source_failed_cast_preserved", 1 },
                { "class_top.source_function_handle_alias", 1 },
                { "class_top.source_module_task_handle_alias", 1 },
                { "class_top.source_final_seen", 1 },
                { "class_top.source_randomize_result", 1 },
                { "class_top.source_randomize_pre", 30 },
                { "class_top.source_randomize_post", 30 },
                { "class_top.source_rand_mode_initial", 1 },
                { "class_top.source_rand_mode_disabled", 0 },
                { "class_top.source_rand_mode_enabled", 1 },
                { "class_top.source_constraint_mode_initial", 1 },
                { "class_top.source_constraint_mode_disabled", 0 },
                { "class_top.source_constraint_mode_enabled", 1 },
                { "class_top.source_impossible_enabled_result", 1 },
                { "class_top.source_impossible_result", 0 },
                { "class_top.source_impossible_pre", 20 },
                { "class_top.source_impossible_post", 10 },
                { "class_top.source_broken_pre_result", 0 },
                { "class_top.source_broken_pre_count", 0 },
                { "class_top.source_broken_post_result", 0 },
                { "class_top.source_broken_post_pre", 0 },
                { "class_top.source_broken_post_count", 0 },
                { "class_top.source_selected_randomize_result", 1 },
                { "class_top.source_selected_randomize_value", 77 },
                { "class_top.source_property", 18 } }) {
            const auto signal = simulation.find_signal(path);
            assert(signal);
            assert(simulation.read_signal(*signal).low_word().aval == expected);
        }
        const auto selected_randomized = simulation.find_signal("class_top.source_selected_randomize_generated");
        const auto fully_randomized = simulation.find_signal("class_top.source_randomize_generated");
        const auto third_randomized = simulation.find_signal("class_top.source_randc_third");
        assert(selected_randomized && fully_randomized && third_randomized);
        assert(simulation.read_signal(*selected_randomized).low_word().aval >= 1 && simulation.read_signal(*selected_randomized).low_word().aval <= 3);
        assert(simulation.read_signal(*fully_randomized).low_word().aval >= 1 && simulation.read_signal(*fully_randomized).low_word().aval <= 3);
        const std::set<std::uint64_t> randc_cycle {
            simulation.read_signal(*selected_randomized).low_word().aval,
            simulation.read_signal(*fully_randomized).low_word().aval,
            simulation.read_signal(*third_randomized).low_word().aval
        };
        assert((randc_cycle == std::set<std::uint64_t> { 1, 2, 3 }));
        const auto randomized_object_signal = simulation.find_signal("class_top.randomized_object");
        assert(randomized_object_signal);
        const auto randomized_handle = simulation.read_signal(*randomized_object_signal).low_word().aval;
        assert(simulation.class_heap()
                   .random_state(randomized_handle, derived_identity + "::value")
                   .revision
            == 0);
        assert(simulation.class_heap()
                   .random_state(randomized_handle,
                       derived_identity + "::generated_value")
                   .revision
            == 3);
        std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>>
            randomized_callback_states;
        for (const auto& [changed_handle, revision, cycle, used] :
            randomization_callback_states) {
            if (changed_handle == randomized_handle) {
                randomized_callback_states.emplace_back(revision, cycle, used);
            }
        }
        assert(
            (randomized_callback_states == std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> { { 1, 0, 1 }, { 2, 0, 2 }, { 3, 0, 3 } }));
        const auto randomization_trace = simulation.class_randomization_trace_states();
        const auto generated_trace = std::ranges::find_if(randomization_trace, [&](const auto& state) {
            return state.object == randomized_handle && state.kind == fsim::app::ClassRandomizationTraceKind::property && state.path.ends_with("::generated_value.$random-state");
        });
        assert(generated_trace != randomization_trace.end() && generated_trace->enabled && generated_trace->revision == 3 && generated_trace->stream_seed != 0 && generated_trace->domain_signature != 0 && generated_trace->cycle == 0 && generated_trace->used_values == 3);
        assert(std::ranges::any_of(randomization_trace, [&](const auto& state) {
            return state.object == randomized_handle && state.kind == fsim::app::ClassRandomizationTraceKind::constraint && state.enabled && state.path.ends_with("::generated_small.$constraint-mode");
        }));
        if (engine == fsim::app::SimulationEngine::debug) {
            std::ostringstream random_debug_output;
            std::ostringstream random_debug_error;
            fsim::app::DebuggerControl random_debugger(
                simulation, random_debug_output, random_debug_error);
            random_debugger.execute({ "class", std::to_string(randomized_handle) });
            assert(random_debug_error.str().empty());
            assert(random_debug_output.str().find("randc enabled 1 revision 3") != std::string::npos && random_debug_output.str().find("cycle 0 used 3") != std::string::npos && random_debug_output.str().find("constraint " + derived_identity + "::generated_small enabled 1") != std::string::npos);
        }
        for (const auto name :
            { "class_top.broken_pre_object", "class_top.broken_post_object" }) {
            const auto failed_object = simulation.find_signal(name);
            assert(failed_object);
            const auto failed_handle = simulation.read_signal(*failed_object).low_word().aval;
            assert(simulation.class_heap()
                       .random_state(failed_handle, derived_identity + "::value")
                       .revision
                == 0);
            assert(simulation.class_heap()
                       .random_state(failed_handle,
                           derived_identity + "::generated_value")
                       .revision
                == 0);
        }
        assert(completion_called);
        assert(simulation.read_class_property(handle, "value")
                   .packed.low_word()
                   .aval
            == 4);
        assert(simulation.read_class_property(handle, base_identity + "::value")
                   .packed.low_word()
                   .aval
            == 2);
        const auto source_other = simulation.find_signal("class_top.source_other");
        assert(source_other);
        assert(
            simulation.read_class_property(source_handle, base_identity + "::peer")
                .handle
            == simulation.read_signal(*source_other).low_word().aval);
        assert(simulation.class_static_store()
                   .property(derived_identity, "shared")
                   .packed.low_word()
                   .aval
            == 10);
        const std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>>
            expected_changes { { 0, 0, "00000010" }, { 1, 0, "00000100" } };
        assert(property_changes == expected_changes);
        const auto trace_values = simulation.class_packed_trace_values();
        assert(std::ranges::any_of(trace_values, [&](const auto& trace) {
            return trace.object == source_handle && trace.path.ends_with(derived_identity + "::value") && trace.value.to_msb_string() == "00010010";
        }));
        assert(std::ranges::any_of(trace_values, [&](const auto& trace) {
            return trace.object == handle && trace.path.ends_with(base_identity + "::wide_value") && trace.value == wide_service_value;
        }));
        assert(std::ranges::any_of(trace_values, [&](const auto& trace) {
            return !trace.object && trace.path.ends_with(base->specialization_identity + ".shared") && trace.value.low_word().aval == 10;
        }));
        std::ostringstream class_vcd_output;
        fsim::runtime::VcdWriter class_vcd { class_vcd_output, "1ns", 64 };
        std::vector<fsim::runtime::VcdSignal> class_vcd_signals;
        for (const auto& trace : trace_values) {
            class_vcd_signals.push_back(
                class_vcd.declare_signal(trace.path, trace.value.width()));
        }
        class_vcd.begin(simulation.now());
        for (std::size_t index = 0; index < trace_values.size(); ++index) {
            class_vcd.change(class_vcd_signals[index], trace_values[index].value);
        }
        class_vcd.flush();
        assert(class_vcd_output.str().find("00010010") != std::string::npos);
        assert(class_vcd_output.str().find(wide_service_value.to_msb_string()) != std::string::npos);

        std::vector<fsim::runtime::SystemVerilogClassMethodValue> suspended_actuals(
            1);
        suspended_actuals.front().packed = fsim::runtime::PackedLogic4::from_aval_bval(8, 1, 0);
        const auto suspended = simulation.class_methods().invoke(
            suspended_method_identity, handle, suspended_actuals);
        assert(suspended.status == fsim::runtime::SystemVerilogClassMethodStatus::Suspended);
        std::ostringstream debug_output;
        std::ostringstream debug_error;
        fsim::app::DebuggerControl debugger(simulation, debug_output, debug_error);
        debugger.execute({ "classes" });
        debugger.execute({ "class", std::to_string(handle), "value" });
        debugger.execute({ "class", "statics" });
        debugger.execute(
            { "class", "static", base->specialization_identity, "shared" });
        debugger.execute({ "class", "frames" });
        debugger.execute({ "uvm", "summary" });
        debugger.execute({ "uvm", "tlm2" });
        debugger.execute({ "uvm", "callbacks" });
        debugger.execute({ "uvm", "sequences" });
        debugger.execute({ "uvm", "transactions" });
        debugger.execute({ "uvm", "registers" });
        debugger.execute({ "break", "phase", "final" });
        debugger.execute({ "break", "uvm", "*" });
        debugger.execute({ "breakpoints" });
        assert(debug_error.str().empty());
        assert(debug_output.str().find(derived_identity) != std::string::npos);
        assert(debug_output.str().find("value = 00000100") != std::string::npos);
        assert(debug_output.str().find("shared = ") != std::string::npos);
        assert(debug_output.str().find(suspended_method_identity) != std::string::npos);
        assert(debug_output.str().find("point 7") != std::string::npos);
        assert(debug_output.str().find("local[0] = 00101010") != std::string::npos);
        assert(debug_output.str().find("uvm time ") != std::string::npos);
        assert(debug_output.str().find("api-first:api_top.initiator_socket") != std::string::npos);
        assert(debug_output.str().find("api_type_callback") != std::string::npos && debug_output.str().find("sequence api_") != std::string::npos);
        assert(debug_output.str().find("transaction trace ") != std::string::npos && debug_output.str().find("register block ") != std::string::npos);
        assert(debug_output.str().find("phase common.final") != std::string::npos && debug_output.str().find("uvm *") != std::string::npos);
        const auto resumed = simulation.class_methods().resume(
            suspended.continuation, suspended_actuals);
        assert(resumed.status == fsim::runtime::SystemVerilogClassMethodStatus::Completed);
        simulation.remove_uvm_activity_hook(api_uvm_activity_token);
        assert(foreign_host.remove_callback(foreign_host.context,
                   foreign_activity_token)
                == FSIM_UVM_FOREIGN_OK
            && !foreign_activity.kinds.empty() && foreign_host.release(foreign_host.context, &foreign_snapshot) == FSIM_UVM_FOREIGN_OK && foreign_host.release(foreign_host.context, &foreign_snapshot) == FSIM_UVM_FOREIGN_STALE_HANDLE && simulation.uvm_foreign().diagnostic_code() == "FSIM-UVM-FOREIGN-001");
        engine_snapshots.emplace_back(
            result.time,
            simulation.read_class_property(source_handle, base_identity + "::value")
                .packed.low_word()
                .aval,
            simulation
                .read_class_property(source_handle, derived_identity + "::value")
                .packed.low_word()
                .aval,
            simulation.class_static_store()
                .property(base_identity, "shared")
                .packed.low_word()
                .aval,
            source_object_state.random_root_seed,
            source_object_state.random_object_seed, source_random_sample,
            simulation.class_heap().live_objects(),
            api_virtual_probe.trace_signature() ^ trace_values.size());
        std::cerr << "application classes: engine " << static_cast<unsigned>(engine)
                  << " complete\n";
    }
    assert(engine_snapshots.size() == 3);
    assert(std::ranges::all_of(engine_snapshots | std::views::drop(1),
        [&](const auto& snapshot) {
            return snapshot == engine_snapshots.front();
        }));
    assert(portable_uvm_snapshots.size() == 3 && std::ranges::all_of(portable_uvm_snapshots | std::views::drop(1), [&](const auto& snapshot) {
        return snapshot == portable_uvm_snapshots.front();
    }));

    auto cache_config = config;
    std::cerr << "application classes: native cache\n";
    cache_config.project.name = "class-native-cache";
    cache_config.project.top = "sv:work.class_cache_top";
    cache_config.build.cache_path = directory / "class-native-cache";
    const auto cached_run = [&]() {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(cache_config, diagnostics);
        assert(project && !diagnostics.has_error());
        fsim::app::Simulation simulation(std::move(*project),
            cache_config.run.max_deltas,
            fsim::app::SimulationEngine::compiled);
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::stopped);
        return simulation.native_cache_statistics();
    };
    const auto cold_cache = cached_run();
#if defined(FSIM_HAS_LLVM)
    assert(cold_cache.misses != 0 && cold_cache.stores != 0);
#else
    assert(cold_cache.hits == 0 && cold_cache.misses == 0);
    assert(cold_cache.stores == 0);
#endif
    const auto warm_cache = cached_run();
#if defined(FSIM_HAS_LLVM)
    assert(warm_cache.hits != 0 && warm_cache.misses == 0);
#else
    assert(warm_cache.hits == 0 && warm_cache.misses == 0);
    assert(warm_cache.stores == 0);
#endif
    const auto original_source = [&] {
        std::ifstream input(class_source, std::ios::binary);
        assert(input);
        return std::string { std::istreambuf_iterator<char> { input },
            std::istreambuf_iterator<char> { } };
    }();
    auto edited_source = original_source;
    const auto edited_delay = edited_source.find("#1 $finish; // class-cache-delay");
    assert(edited_delay != std::string::npos);
    edited_source.replace(edited_delay, 32, "#2 $finish; // class-cache-delay");
    {
        std::ofstream edited_output(class_source,
            std::ios::binary | std::ios::trunc);
        edited_output << edited_source;
    }
    const auto edited_cache = cached_run();
#if defined(FSIM_HAS_LLVM)
    assert(edited_cache.misses != 0 && edited_cache.stores != 0);
#else
    assert(edited_cache.hits == 0 && edited_cache.misses == 0);
    assert(edited_cache.stores == 0);
#endif
    {
        std::ofstream restored_output(class_source,
            std::ios::binary | std::ios::trunc);
        restored_output << original_source;
    }

    const auto object = directory / "class-object.fsimobj";
    std::cerr << "application classes: artifacts\n";
    const auto library = directory / "class-library.fsimlib";
    const auto design = directory / "class-design.fsimdesign";
    fsim::diagnostic::Engine artifact_diagnostics;
    assert(fsim::app::compile_artifact(config, object, artifact_diagnostics));
    assert(
        fsim::app::export_library(config, "work", library, artifact_diagnostics));
    const std::vector<std::filesystem::path> objects { object };
    auto artifact_config = config;
    artifact_config.source_sets.clear();
    artifact_config.build.cache_path = directory / "class-artifact-cache";
    const auto artifact_elaborated = fsim::app::elaborate_artifact(
        artifact_config, objects, design, artifact_diagnostics);
    if (!artifact_elaborated) {
        for (const auto& diagnostic : artifact_diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(artifact_elaborated);
    assert(!artifact_diagnostics.has_error());

    const auto hidden_source = class_source.string() + ".hidden";
    const auto hidden_object = object.string() + ".hidden";
    std::filesystem::rename(class_source, hidden_source);
    std::filesystem::rename(object, hidden_object);
    const auto relocated_root = directory / "relocated-classes";
    std::filesystem::create_directories(relocated_root);
    const auto relocated_design = relocated_root / design.filename();
    const auto relocated_library = relocated_root / library.filename();
    const auto relocate_directory = [](const auto& origin, const auto& target) {
        std::filesystem::create_directories(target);
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(origin)) {
            const auto destination = target / entry.path().lexically_relative(origin);
            if (entry.is_directory()) {
                std::filesystem::create_directories(destination);
            } else if (entry.is_regular_file()) {
                std::filesystem::copy_file(entry.path(), destination);
            }
        }
    };
    relocate_directory(design, relocated_design);
    relocate_directory(library, relocated_library);
    fsim::diagnostic::Engine standalone_diagnostics;
    auto standalone = fsim::app::load_design_artifact(relocated_design, standalone_diagnostics);
    assert(standalone);
    assert(standalone->systemverilog_class_specializations.size() == 15);
    assert(std::ranges::any_of(standalone->systemverilog_class_specializations,
        [](const auto& specialization) {
            return std::ranges::any_of(
                specialization.properties,
                [](const auto& property) {
                    return property.name == "wide_value" && property.bit_width == 137 && property.type.width() == 137;
                });
        }));
    assert(standalone->systemverilog_hir.classes().size() == 15);
    assert(std::ranges::any_of(
        standalone->systemverilog_hir.classes(), [](const auto& declaration) {
            return declaration.canonical_identity.ends_with("::AppDerived") && !declaration.constraints.empty() && !declaration.composed_constraints.empty();
        }));
    assert(std::ranges::any_of(
        standalone->design.processes(), [](const auto& process) {
            return std::ranges::any_of(process.operations, [](const auto& op) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::ClassAllocate>(op);
            });
        }));
    assert(std::ranges::any_of(
        standalone->design.processes(), [](const auto& process) {
            const auto has_class_call = std::ranges::any_of(process.operations, [](const auto& op) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::ClassMethodCall>(op);
            });
            const auto has_continuation = std::ranges::any_of(process.operations, [](const auto& op) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::Call>(op);
            });
            return has_class_call && has_continuation;
        }));
    fsim::app::Simulation standalone_simulation(
        std::move(*standalone), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter);
    const auto standalone_derived = std::ranges::find_if(
        standalone_simulation.class_specializations(),
        [](const auto& specialization) {
            return specialization.declaration_identity.ends_with("::AppDerived");
        });
    assert(standalone_derived != standalone_simulation.class_specializations().end());
    const auto standalone_transfer = std::ranges::find(standalone_derived->methods, std::string { "transfer" },
        &fsim::frontend::SystemVerilogClassMethodProfile::name);
    assert(standalone_transfer != standalone_derived->methods.end() && !standalone_transfer->statements.empty() && !standalone_transfer->statements.front().span.source_name.empty());
    const auto standalone_handle = standalone_simulation.allocate_class(
        standalone_derived->specialization_identity);
    assert(standalone_simulation.read_class_property(standalone_handle, "value")
               .packed.to_msb_string()
        == "XXXXXXXX");
    const auto standalone_base = std::ranges::find_if(
        standalone_simulation.class_specializations(),
        [](const auto& specialization) {
            return specialization.declaration_identity.ends_with("::AppBase");
        });
    assert(standalone_base != standalone_simulation.class_specializations().end());
    assert(standalone_simulation.class_static_store()
               .property(standalone_base->declaration_identity, "shared")
               .packed.low_word()
               .aval
        == 2);
    const auto standalone_result = standalone_simulation.run();
    assert(standalone_result.status == fsim::runtime::RunStatus::stopped && standalone_result.time == 9);
    const auto standalone_property = standalone_simulation.find_signal("class_top.source_property");
    assert(
        standalone_property && standalone_simulation.read_signal(*standalone_property).low_word().aval == 18);
    assert(standalone_simulation.class_static_store()
               .property(standalone_base->declaration_identity, "shared")
               .packed.low_word()
               .aval
        == 7);

    auto mapped_config = config;
    std::cerr << "application classes: relocated library\n";
    mapped_config.project.name = "mapped-class-simulation";
    mapped_config.source_sets.clear();
    mapped_config.library_mappings = { { "work", relocated_library } };
    mapped_config.elaboration.search_libraries = { "work" };
    mapped_config.build.cache_path = directory / "mapped-class-cache";
    fsim::diagnostic::Engine mapped_diagnostics;
    auto mapped = fsim::app::build_project(mapped_config, mapped_diagnostics);
    assert(mapped);
    assert(mapped->systemverilog_class_specializations.size() == 15);
    assert(std::ranges::any_of(mapped->systemverilog_class_specializations,
        [](const auto& specialization) {
            return std::ranges::any_of(
                specialization.properties,
                [](const auto& property) {
                    return property.name == "wide_value" && property.bit_width == 137 && property.type.width() == 137;
                });
        }));
    fsim::app::Simulation mapped_simulation(
        std::move(*mapped), mapped_config.run.max_deltas,
        fsim::app::SimulationEngine::compiled);
    const auto mapped_result = mapped_simulation.run();
    assert(mapped_result.status == fsim::runtime::RunStatus::stopped && mapped_result.time == 9);
    const auto mapped_property = mapped_simulation.find_signal("class_top.source_property");
    assert(mapped_property && mapped_simulation.read_signal(*mapped_property).low_word().aval == 18);

    auto multiple = config;
    std::cerr << "application classes: multiple roots\n";
    multiple.project.name = "multi-root-class-simulation";
    multiple.project.top.clear();
    multiple.project.tops = { { "sv:work.class_root_a", "left" },
        { "sv:work.class_root_b", "right" } };
    multiple.build.cache_path = directory / "multi-root-class-cache";
    multiple.source_sets.front().files = { hidden_source };
    for (const auto engine : { fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::compiled,
             fsim::app::SimulationEngine::debug }) {
        fsim::diagnostic::Engine multiple_diagnostics;
        auto multiple_project = fsim::app::build_project(multiple, multiple_diagnostics);
        if (!multiple_project) {
            for (const auto& diagnostic : multiple_diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
        assert(multiple_project);
        assert((multiple_project->design.roots() == std::vector<std::string> { "left", "right" }));
        fsim::app::Simulation multiple_simulation(std::move(*multiple_project),
            multiple.run.max_deltas, engine);
        const auto multiple_result = multiple_simulation.run();
        assert(multiple_result.status == fsim::runtime::RunStatus::stopped);
        assert(multiple_result.time == 2);
        for (const auto path : { "left.ready", "right.ready" }) {
            const auto signal = multiple_simulation.find_signal(path);
            assert(signal);
            assert(multiple_simulation.read_signal(*signal).low_word().aval == 1);
        }
        assert(multiple_simulation.class_heap().live_objects() == 2);
        const auto multi_base = std::ranges::find_if(
            multiple_simulation.class_specializations(),
            [](const auto& specialization) {
                return specialization.declaration_identity.ends_with("::AppBase");
            });
        assert(multi_base != multiple_simulation.class_specializations().end());
        assert(multiple_simulation.class_static_store()
                   .property(multi_base->declaration_identity, "shared")
                   .packed.low_word()
                   .aval
            == 13);
    }
}

} // namespace fsim::test
