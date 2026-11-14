// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "fsim/runtime/simir_interpreter.hpp"
#include "fsim/runtime/vpi_control.hpp"

#include "../elaboration/hierarchy_sv_constant_evaluator.hpp"

#include "fsim/runtime/class_randomize_graph.hpp"
#include "fsim/semantic/systemverilog_structure_image.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "application_class_execution.hpp"
#include "application_internal.hpp"
#include "application_uvm_registry.hpp"
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/frontend/design_core.hpp"
#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/class_methods.hpp"
#include "fsim/runtime/class_randomize.hpp"
#include "fsim/runtime/constraint_solver.hpp"
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/scheduler_events.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_container_value.hpp"
#include "fsim/runtime/simir_ids.hpp"
#include "fsim/runtime/sva_storage.hpp"
#include "fsim/runtime/uvm_command_line.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_registry.hpp"
#include "fsim/runtime/vpi_abi.h"
#include "fsim/runtime/vpi_callback.hpp"
#include "fsim/runtime/vpi_object.hpp"
#include "fsim/runtime/vpi_system.hpp"
#include "fsim/runtime/vpi_types.hpp"
#include "fsim/runtime/vpi_value.hpp"
#include "fsim/semantic/design_ir.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_class_specialization.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <ranges>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <ios>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::app {

namespace {

// The value of UVM_IMPLEMENTATION, the last literal of the library's
// uvm_port_type_e (IEEE 1800.2-2020 F.2.1.4), found by the type's recorded
// role; empty when the design does not declare it or a literal of the type
// has an explicit value.
[[nodiscard]] std::optional<std::uint64_t> uvm_port_implementation_value(
    const semantic::sv::Hir& hir)
{
    for (const auto& unit : hir.units()) {
        if (!support::uvm_library(unit.uvm_role))
            continue;
        for (const auto id : unit.declarations) {
            const auto* declaration = hir.find_declaration(id);
            if (declaration == nullptr
                || declaration->uvm_role != support::UvmRole::port_type_enum
                || !declaration->declared_type)
                continue;
            const auto* type = hir.find_type(*declaration->declared_type);
            if (type == nullptr || type->enumeration_literals.empty()
                || std::ranges::any_of(type->enumeration_literals,
                    [](const auto& literal) { return literal.value.has_value(); }))
                return std::nullopt;
            return type->enumeration_literals.size() - 1U;
        }
    }
    return std::nullopt;
}

} // namespace

[[nodiscard]] std::filesystem::path Simulation::Impl::coverage_database_file(
    const std::string_view filename) const
{
    std::filesystem::path relative;
    try {
        relative = support::path_from_utf8(filename).lexically_normal();
    } catch (const std::exception&) {
        throw std::runtime_error {
            "coverage database filename is not a valid UTF-8 path"
        };
    }
    if (relative.empty() || relative.is_absolute()
        || relative.has_root_name()
        || std::ranges::any_of(relative, [](const auto& component) {
               return component == "..";
           })) {
        throw std::runtime_error {
            "coverage database filename must remain beneath the project file root"
        };
    }
    return (built.file_root / relative).lexically_normal();
}

void Simulation::Impl::load_coverage_database(const std::filesystem::path& path)
{
    std::error_code error;
    // A database not yet written holds no cumulative coverage to merge
    // (IEEE 1800-2017 20.14.4).
    if (!support::native_fs::exists(path, error) && !error) {
        return;
    }
    error.clear();
    const auto bytes = support::native_fs::file_size(path, error);
    if (error) {
        throw std::runtime_error {
            "cannot inspect coverage database: " + error.message()
        };
    }
    if (bytes > runtime::simir::maximum_memory_file_bytes) {
        throw std::runtime_error {
            "coverage database exceeds the governed file-size budget"
        };
    }
    auto input = support::native_fs::open_ifstream(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error { "cannot open coverage database" };
    }
    std::string contents(
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { });
    if (!input.good() && !input.eof()) {
        throw std::runtime_error { "cannot read coverage database" };
    }
    diagnostic::Engine diagnostics;
    auto loaded = deserialize_systemverilog_coverage_state(
        contents, support::path_to_utf8(path), built.systemverilog_hir,
        built.semantics, diagnostics);
    if (!loaded) {
        const auto& entries = diagnostics.diagnostics();
        throw std::runtime_error {
            entries.empty()
                ? "coverage database is malformed"
                : entries.front().message
        };
    }
    std::string merge_error;
    if (!application_detail::merge_systemverilog_coverage_state(
            built.systemverilog_coverage, *loaded,
            built.systemverilog_hir, built.semantics, merge_error)) {
        throw std::runtime_error { std::move(merge_error) };
    }
}

void Simulation::Impl::save_coverage_database()
{
    if (!coverage_database_path) {
        return;
    }
    diagnostic::Engine diagnostics;
    auto contents = serialize_systemverilog_coverage_state(
        built.systemverilog_coverage, built.systemverilog_hir,
        built.semantics, diagnostics);
    if (!contents) {
        const auto& entries = diagnostics.diagnostics();
        throw std::runtime_error {
            entries.empty()
                ? "cannot serialize coverage database"
                : entries.front().message
        };
    }
    std::error_code error;
    support::native_fs::create_directories(
        coverage_database_path->parent_path(), error);
    if (error) {
        throw std::runtime_error {
            "cannot create coverage database directory: "
            + error.message()
        };
    }
    auto temporary = *coverage_database_path;
    temporary += ".fsim-tmp";
    {
        auto output = support::native_fs::open_ofstream(
            temporary, std::ios::binary | std::ios::trunc);
        output.write(contents->data(),
            static_cast<std::streamsize>(contents->size()));
        if (!output) {
            support::native_fs::remove(temporary, error);
            throw std::runtime_error {
                "cannot write coverage database"
            };
        }
    }
    auto backup = *coverage_database_path;
    backup += ".fsim-old";
    support::native_fs::remove(backup, error);
    if (error) {
        support::native_fs::remove(temporary, error);
        throw std::runtime_error {
            "cannot prepare coverage database replacement: "
            + error.message()
        };
    }
    const auto had_existing = support::native_fs::exists(
        *coverage_database_path, error);
    if (error) {
        support::native_fs::remove(temporary, error);
        throw std::runtime_error {
            "cannot inspect coverage database destination: "
            + error.message()
        };
    }
    if (had_existing) {
        support::native_fs::rename(
            *coverage_database_path, backup, error);
        if (error) {
            support::native_fs::remove(temporary, error);
            throw std::runtime_error {
                "cannot preserve the previous coverage database: "
                + error.message()
            };
        }
    }
    support::native_fs::rename(
        temporary, *coverage_database_path, error);
    if (error) {
        const auto publish_error = error.message();
        if (had_existing) {
            std::error_code restore_error;
            support::native_fs::rename(
                backup, *coverage_database_path, restore_error);
        }
        support::native_fs::remove(temporary, error);
        throw std::runtime_error {
            "cannot publish coverage database: " + publish_error
        };
    }
    if (had_existing) {
        support::native_fs::remove(backup, error);
    }
}

void Simulation::Impl::control_coverage_database(
    const runtime::simir::CoverageDatabaseControlEvent& event)
{
    const auto path = coverage_database_file(event.filename);
    if (event.kind
        == runtime::simir::CoverageDatabaseControlKind::set_name) {
        coverage_database_path = path;
        return;
    }
    if (event.kind
        == runtime::simir::CoverageDatabaseControlKind::load) {
        load_coverage_database(path);
        return;
    }
    throw std::runtime_error {
        "unknown coverage database control kind"
    };
}

std::vector<runtime::SystemVerilogUvmCommandReportApplication>
Simulation::Impl::apply_uvm_report_settings(
    const std::string_view phase,
    const SimulationTick time)
{
    return uvm_command_line.apply_report_settings(
        uvm_components, uvm_reports, phase, time);
}

void Simulation::Impl::UvmReportSettingEvent::dispatch(
    runtime::Scheduler&, const Payload& scheduled)
{
    static_cast<void>(
        scheduled.owner->apply_uvm_report_settings("time", scheduled.offset));
}

void Simulation::Impl::register_event_kinds()
{
    static const bool registered = [] {
        static_cast<void>(runtime::register_scheduler_event_kind<
            UvmReportSettingEvent::Payload, &UvmReportSettingEvent::dispatch,
            UvmReportSettingEvent::Codec>("sim.uvm_report_setting",
            runtime::SchedulerEventClass::data));
        return true;
    }();
    static_cast<void>(registered);
}

bool Simulation::Impl::bind_restored_event_handle(const std::string_view kind,
    const std::uint64_t key, const runtime::ScheduledTaskHandle& handle)
{
    static_cast<void>(key);
    if (kind == "sim.uvm_report_setting") {
        uvm_report_setting_tasks.push_back(handle);
        return true;
    }
    return false;
}

void Simulation::Impl::schedule_uvm_report_settings()
{
    register_event_kinds();
    auto& scheduler = interpreter->scheduler();
    for (const auto& handle : uvm_report_setting_tasks) {
        (void)scheduler.cancel(handle);
    }
    uvm_report_setting_tasks.clear();
    const auto now = scheduler.now();
    (void)apply_uvm_report_settings("time", now);
    std::set<SimulationTick> offsets;
    for (const auto& setting : uvm_command_line.settings().verbosity_settings) {
        if (setting.phase == "time" && setting.time_offset
            && *setting.time_offset > now) {
            offsets.insert(*setting.time_offset);
        }
    }
    runtime::StableOrder order { };
    for (const auto offset : offsets) {
        uvm_report_setting_tasks.push_back(
            scheduler.schedule_after_cancelable(
                offset - now, runtime::SchedulerPhase::active, order++,
                runtime::Scheduler::Task { runtime::detail::
                        make_scheduler_task_descriptor<
                            UvmReportSettingEvent::Payload,
                            &UvmReportSettingEvent::dispatch>(
                            UvmReportSettingEvent::Payload { this, offset }) }));
    }
}

void Simulation::Impl::validate_external_value(
    const SignalId signal,
    const PackedLogic4& value,
    const std::string_view operation) const
{
    const auto& info = built.design.signals().at(signal);
    if (info.width != value.width()) {
        throw std::invalid_argument(
            std::string { operation } + " width does not match signal '"
            + info.name + "'");
    }
    if (info.source_domain != frontend::ValueDomain::Bit2
        && info.source_domain != frontend::ValueDomain::Boolean) {
        return;
    }
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        const auto state = value.get(bit);
        if (state != runtime::Logic4::zero
            && state != runtime::Logic4::one) {
            throw std::invalid_argument(
                std::string { operation }
                + " would place an X/Z value into two-state signal '"
                + info.name + "'");
        }
    }
}

const semantic::sv::ClassSpecialization*
Simulation::Impl::generated_class_specialization(
    const std::string_view identity) const
{
    const auto find = [&]() -> const semantic::sv::ClassSpecialization* {
        const auto found = std::ranges::find(
            generated_class_specializations, identity,
            &semantic::sv::ClassSpecialization::specialization_identity);
        return found == generated_class_specializations.end() ? nullptr
                                                              : &*found;
    };
    if (const auto* existing = find()) {
        return existing;
    }
    if (identity.find("#gen{") == std::string_view::npos) {
        return nullptr;
    }
    if (!runtime_class_index().specialize_generated_identity(identity)) {
        return nullptr;
    }
    for (const auto& specialization :
        generated_class_index->result().specializations) {
        if (!specialization.generate_environment.empty()
            && std::ranges::find(generated_class_specializations,
                   specialization.specialization_identity,
                   &semantic::sv::ClassSpecialization::specialization_identity)
                == generated_class_specializations.end()) {
            generated_class_specializations.push_back(specialization);
        }
    }
    return find();
}

semantic::sv::ClassSpecializationIndex&
Simulation::Impl::runtime_class_index() const
{
    if (!generated_class_index) {
        generated_class_index
            = std::make_unique<semantic::sv::ClassSpecializationIndex>(
                built.semantics, built.systemverilog_hir);
        // The index starts from the specializations the design was
        // elaborated with; only those it makes later are new.
        generated_class_index_published = generated_class_index->size();
    }
    return *generated_class_index;
}

std::optional<std::string> Simulation::Impl::class_type_specialization(
    const semantic::sv::TypeReference& type,
    const std::string_view context) const
{
    if (!type.class_specialization.empty()) {
        return type.class_specialization;
    }
    if (type.class_identity.empty()) {
        return std::nullopt;
    }
    auto key = std::pair { &type, std::string { context } };
    if (const auto cached = class_type_specializations.find(key);
        cached != class_type_specializations.end()) {
        return cached->second;
    }
    auto answer = make_class_type_specialization(type, context);
    class_type_specializations.emplace(std::move(key), answer);
    return answer;
}

std::optional<std::string> Simulation::Impl::make_class_type_specialization(
    const semantic::sv::TypeReference& type,
    const std::string_view context) const
{
    // A class name without actuals denotes the default specialization
    // (IEEE 1800-2023 8.25), which the design always has.
    if (type.interface_parameter_actuals.empty()) {
        const auto& compiled
            = built.compiled_systemverilog_class_specializations;
        const auto found = std::ranges::find_if(compiled,
            [&](const semantic::sv::ClassSpecialization& candidate) {
                return candidate.declaration_identity == type.class_identity
                    && candidate.default_specialization;
            });
        if (found != compiled.end()) {
            return found->specialization_identity;
        }
    }
    // Actuals that read the running class's parameters (`C#(T)` in a
    // method of `D#(T)`) take that specialization's values.
    auto& index = runtime_class_index();
    std::optional<semantic::sv::ClassSpecializationId> running;
    if (!context.empty()) {
        running = index.find(context);
        if (!running && context.find("#gen{") != std::string_view::npos) {
            running = index.specialize_generated_identity(context);
        }
        if (!running) {
            return std::nullopt;
        }
    }
    const auto made = index.specialize(type, running, type.target.source);
    if (!made) {
        return std::nullopt;
    }
    // A specialization the elaborated design does not have joins the
    // classes made at run time, with static storage of its own.
    const auto count = index.size();
    for (auto position = generated_class_index_published; position < count;
         ++position) {
        const auto& specialization = index.result().specializations[position];
        if (std::ranges::find(generated_class_specializations,
                specialization.specialization_identity,
                &semantic::sv::ClassSpecialization::specialization_identity)
            == generated_class_specializations.end()) {
            generated_class_specializations.push_back(specialization);
        }
    }
    if (count > generated_class_index_published) {
        class_specialization_recipes.emplace_back(type, std::string { context });
    }
    generated_class_index_published = count;
    return index.at(*made).specialization_identity;
}

[[nodiscard]] const semantic::sv::ClassSpecialization&
Simulation::Impl::class_specialization(const std::string_view identity) const
{
    auto found = std::ranges::find(
        built.compiled_systemverilog_class_specializations, identity,
        &semantic::sv::ClassSpecialization::specialization_identity);
    if (found == built.compiled_systemverilog_class_specializations.end()) {
        if (const auto* generated = generated_class_specialization(identity)) {
            return *generated;
        }
        const auto matches = std::ranges::count(
            built.compiled_systemverilog_class_specializations, identity,
            &semantic::sv::ClassSpecialization::declaration_identity);
        if (matches == 1) {
            found = std::ranges::find(
                built.compiled_systemverilog_class_specializations, identity,
                &semantic::sv::ClassSpecialization::declaration_identity);
        } else if (matches > 1) {
            // A bare class name denotes its default specialization.
            found = std::ranges::find_if(
                built.compiled_systemverilog_class_specializations,
                [&](const semantic::sv::ClassSpecialization& candidate) {
                    return candidate.declaration_identity == identity
                        && candidate.default_specialization;
                });
        }
    }
    if (found == built.compiled_systemverilog_class_specializations.end()) {
        throw std::out_of_range {
            "SystemVerilog class specialization '" + std::string { identity }
            + "' is not available in this simulation"
        };
    }
    return *found;
}

[[nodiscard]] const std::map<std::string, std::string, std::less<>>&
Simulation::Impl::class_instance_parameters(
    const semantic::sv::ClassSpecialization& specialization) const
{
    const auto cached = class_instance_parameter_cache.find(
        specialization.specialization_identity);
    if (cached != class_instance_parameter_cache.end()) {
        return cached->second;
    }
    std::map<std::string, std::string, std::less<>> values;
    const auto instance = std::ranges::find(
        specialization.generate_environment,
        semantic::sv::class_instance_environment,
        &std::pair<std::string, std::string>::first);
    if (instance != specialization.generate_environment.end()) {
        const semantic::design::InstanceOccurrence* occurrence = nullptr;
        for (const auto& candidate : built.design_ir.instances()) {
            if (candidate.path.valid()
                && built.design_ir.path(candidate.path) == instance->second) {
                occurrence = &candidate;
                break;
            }
        }
        const auto* owner = occurrence == nullptr
            ? nullptr
            : exact_specialization(
                built.design_ir, occurrence->specialization);
        const semantic::sv::Unit* unit = nullptr;
        if (owner != nullptr) {
            for (const auto& candidate : built.systemverilog_hir.units()) {
                if (candidate.id == owner->unit) {
                    unit = &candidate;
                    break;
                }
            }
        }
        if (unit == nullptr) {
            throw std::logic_error {
                "SystemVerilog class '" + specialization.specialization_identity
                + "' names no design element instance"
            };
        }
        const auto parameter_form = [](const semantic::sv::DeclarationForm form) {
            return form == semantic::sv::DeclarationForm::parameter
                || form == semantic::sv::DeclarationForm::local_parameter;
        };
        for (const auto& parameter : owner->parameters) {
            // Specializations also record imported package values; only the
            // unit's own value parameters bind its classes' constraints.
            const auto own = std::ranges::any_of(unit->declarations,
                [&](const semantic::DeclarationId id) {
                    const auto* declaration
                        = built.systemverilog_hir.find_declaration(id);
                    return declaration != nullptr
                        && declaration->name == parameter.name
                        && parameter_form(declaration->form)
                        && (!parameter.declaration
                            || *parameter.declaration == id);
                });
            if (!own) {
                continue;
            }
            // A value that is not a known integer of at most 64 bits leaves
            // the binding without a constant, so lowering reports it.
            std::string value;
            if (const auto decoded
                = elaboration::decode_hir_systemverilog_constant(
                    parameter.identity);
                decoded) {
                const auto integer = decoded->known() && !decoded->scalar
                        && decoded->width <= 64U
                    ? decoded->integer_value()
                    : std::nullopt;
                if (integer) {
                    value = std::to_string(static_cast<std::uint64_t>(*integer));
                }
            } else {
                std::int64_t integer { };
                const std::string_view text { parameter.value };
                const auto parsed = std::from_chars(
                    text.data(), text.data() + text.size(), integer);
                if (parsed.ec == std::errc { }
                    && parsed.ptr == text.data() + text.size()) {
                    value = std::to_string(static_cast<std::uint64_t>(integer));
                }
            }
            values.emplace(unit->name + "::" + parameter.name, std::move(value));
        }
    }
    return class_instance_parameter_cache
        .emplace(specialization.specialization_identity, std::move(values))
        .first->second;
}

[[nodiscard]] runtime::SystemVerilogUvmRootHandle Simulation::Impl::component_root(
    const std::string_view allocation_scope)
{
    return class_execution.component_root(allocation_scope);
}

[[nodiscard]] const std::vector<Simulation::Impl::ConstraintStateVariable>&
Simulation::Impl::constraint_state_variables(
    const semantic::sv::ClassSpecialization& specialization)
{
    const auto [cached, inserted] = class_constraint_state_variables.try_emplace(
        specialization.declaration_identity);
    if (!inserted) {
        return cached->second;
    }
    auto& states = cached->second;
    const auto& classes = built.systemverilog_hir.classes();
    const auto declaration = std::ranges::find(
        classes, specialization.declaration_identity,
        &semantic::sv::ClassDeclaration::canonical_identity);
    if (declaration == classes.end()) {
        return states;
    }
    const auto collect = [&](auto&& self,
                             const semantic::sv::ConstraintExpression&
                                 expression) -> void {
        for (const auto& binding : expression.bindings) {
            if (binding.kind != semantic::sv::ConstraintReferenceKind::state
                || !binding.declaration
                || std::ranges::find(
                       states, binding.canonical_identity,
                       &ConstraintStateVariable::identity)
                    != states.end()) {
                continue;
            }
            states.push_back({ binding.canonical_identity,
                *binding.declaration, binding });
        }
        for (const auto& operand : expression.operands) {
            self(self, operand);
        }
    };
    for (const auto& composed : declaration->composed_constraints) {
        for (const auto& owner : classes) {
            const auto constraint = std::ranges::find(
                owner.constraints, composed.selected_identity,
                &semantic::sv::ClassConstraint::canonical_identity);
            if (constraint == owner.constraints.end()) {
                continue;
            }
            for (const auto& expression : constraint->expressions) {
                collect(collect, expression);
            }
            break;
        }
    }
    return states;
}

void Simulation::Impl::add_constraint_state_variables(
    runtime::SystemVerilogConstraintSolver& solver,
    runtime::SystemVerilogClassRandomizeVariables& variables,
    const semantic::sv::ClassSpecialization& specialization)
{
    for (const auto& state : constraint_state_variables(specialization)) {
        if (variables.contains(state.identity)) {
            continue;
        }
        const auto* declaration
            = built.systemverilog_hir.find_declaration(state.declaration);
        if (declaration == nullptr || !declaration->declared_value) {
            continue;
        }
        // The declaration's runtime storage; a declaration with several
        // instances has no single current value.
        const auto storage = [&](const semantic::design::ObjectKind kind)
            -> std::optional<std::uint64_t> {
            const auto unique = [&](const auto& matches)
                -> std::optional<std::optional<std::uint64_t>> {
                std::optional<std::uint64_t> index;
                for (const auto& object : built.design_ir.objects()) {
                    if (object.kind != kind || !matches(object)) {
                        continue;
                    }
                    if (index && *index != object.runtime_index) {
                        return std::optional<std::uint64_t> { };
                    }
                    index = object.runtime_index;
                }
                if (!index) {
                    return std::nullopt;
                }
                return index;
            };
            if (const auto by_value = unique([&](const auto& object) {
                    return object.declaration_value
                        == declaration->declared_value;
                })) {
                return *by_value;
            }
            // Storage materialized under its hierarchical name only (an
            // unpacked or queue variable) is the one object of that name in
            // the declaring unit (`$state::unit::name`).
            constexpr std::string_view state_prefix { "$state::" };
            std::string_view unit { state.identity };
            unit.remove_prefix(std::min(unit.size(), state_prefix.size()));
            unit = unit.substr(0U, unit.find("::"));
            const auto& specializations = built.design_ir.specializations();
            return unique([&](const auto& object) {
                return object.name == declaration->name
                    && object.specialization.valid()
                    && object.specialization.value() < specializations.size()
                    && specializations[object.specialization.value()].name
                        == unit;
            }).value_or(std::nullopt);
        };
        if (const auto signal = storage(semantic::design::ObjectKind::signal);
            signal
            && *signal <= std::numeric_limits<runtime::simir::SignalId>::max()) {
            auto value = interpreter->signal_value_snapshot(
                static_cast<runtime::simir::SignalId>(*signal));
            if (value.is_logic9()) {
                continue;
            }
            auto profile
                = application_detail::systemverilog_constraint_profile(
                    state.binding);
            profile.width = value.width();
            runtime::SystemVerilogConstraintVariable variable;
            variable.canonical_identity = state.identity;
            variable.profile = std::move(profile);
            variable.domain.push_back(std::move(value));
            variables.emplace(
                state.identity, solver.add_variable(std::move(variable)));
            continue;
        }
        // A container is visible to a constraint through its size (IEEE
        // 1800-2017 7.12.3, 18.5.8).
        if (const auto container
            = storage(semantic::design::ObjectKind::container);
            container
            && *container
                <= std::numeric_limits<runtime::simir::ContainerObjectId>::max()) {
            const auto size = runtime::simir::container_value_size(
                interpreter->container_object_value_snapshot(
                    static_cast<runtime::simir::ContainerObjectId>(
                        *container)));
            runtime::SystemVerilogConstraintVariable variable;
            variable.canonical_identity = state.identity + ".size()";
            variable.profile = { runtime::SystemVerilogConstraintDomainKind::Integer,
                32, true, "int", false };
            variable.domain.push_back(runtime::PackedLogic4::from_aval_bval(
                32, static_cast<std::uint64_t>(size), 0U));
            auto identity = variable.canonical_identity;
            variables.emplace(
                std::move(identity), solver.add_variable(std::move(variable)));
        }
    }
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_randomize(
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const std::string> names,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const runtime::SystemVerilogConstraintTemplate>
        source_constraints,
    const std::optional<runtime::simir::ProcessId> calling_process,
    const bool callbacks_lowered)
{
    // The generated code ran pre_randomize and runs post_randomize; the
    // objects that join the solve are the ones its call collected.
    const LoweredRandomizeCall* const lowered = callbacks_lowered
        && !lowered_randomize_calls.empty()
        && lowered_randomize_calls.back().handle == handle
        ? &lowered_randomize_calls.back()
        : nullptr;
    if (callbacks_lowered && lowered == nullptr) {
        throw std::logic_error {
            "randomize() solve outside its lowered call"
        };
    }
    constexpr std::string_view inline_value_prefix = "@inline-value:";
    std::vector<std::string> selected_names;
    std::vector<runtime::PackedLogic4> inline_values;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (!names[index].starts_with(inline_value_prefix)) {
            selected_names.push_back(names[index]);
            continue;
        }
        if (index >= actuals.size()) {
            throw std::invalid_argument {
                "randomize inline value has no actual"
            };
        }
        inline_values.push_back(actuals[index]);
    }
    // Fill each calling-scope value placeholder (IEEE 1800-2017 18.7).
    std::vector<runtime::SystemVerilogConstraintTemplate> inline_constraints {
        source_constraints.begin(), source_constraints.end()
    };
    const auto fill = [&](auto&& self,
                          runtime::SystemVerilogConstraintTemplate& node)
        -> void {
        if (node.kind == runtime::SystemVerilogConstraintTemplateKind::Constant
            && node.text.starts_with(inline_value_prefix)) {
            const auto slot = std::stoull(
                node.text.substr(inline_value_prefix.size()));
            if (slot >= inline_values.size()
                || inline_values[slot].width() != node.constant.width()) {
                throw std::invalid_argument {
                    "randomize inline value does not match its constraint"
                };
            }
            node.constant = inline_values[slot];
            return;
        }
        for (auto& operand : node.operands) {
            self(self, operand);
        }
    };
    for (auto& constraint : inline_constraints) {
        fill(fill, constraint);
    }

    // A static random variable is randomized with the object, shared by
    // every instance (IEEE 1800-2017 18.4): its stored value and random
    // state join the object for the solve and return to the store after.
    struct BorrowedStatics {
        Simulation::Impl& impl;
        runtime::SystemVerilogClassHandle handle;
        std::size_t own_count;
        std::vector<std::pair<std::string, std::string>> sources;
        ~BorrowedStatics()
        {
            if (sources.empty()) {
                return;
            }
            auto& current = impl.class_heap.object(handle);
            for (std::size_t index { }; index < sources.size(); ++index) {
                const auto slot = own_count + index;
                if (slot < current.properties.size()) {
                    impl.class_static_store.property(
                        sources[index].first, sources[index].second)
                        = std::move(current.properties[slot]);
                }
            }
            current.properties.resize(own_count);
            current.property_names.resize(own_count);
        }
    } borrowed_statics { *this, handle,
        class_heap.object(handle).properties.size(), { } };
    {
        auto& owner_object = class_heap.object(handle);
        // Inherited static variables are the base classes' (8.13).
        std::vector<const semantic::sv::SpecializedClassProperty*> statics;
        for (std::string current { owner_object.specialization_identity };
             !current.empty();) {
            const auto& specialization = class_specialization(current);
            for (const auto& property : specialization.properties) {
                statics.push_back(&property);
            }
            current = specialization.base
                ? specialization.base->specialization_identity
                : std::string { };
        }
        for (const auto* const property_pointer : statics) {
            const auto& property = *property_pointer;
            // A static randc cycle would need the heap's storage
            // accounting; only rand variables are borrowed.
            if (!property.static_storage
                || property.random_kind != semantic::sv::ClassRandomKind::rand) {
                continue;
            }
            auto qualified = property.owner_identity + "::" + property.name;
            if (std::ranges::find(owner_object.property_names, qualified)
                != owner_object.property_names.end()) {
                continue;
            }
            auto specialization = static_property_specialization(
                property.owner_identity, handle);
            const auto& stored = class_static_store.property(
                specialization, property.name);
            if (!stored.random_state) {
                continue;
            }
            owner_object.property_names.push_back(std::move(qualified));
            owner_object.properties.push_back(stored);
            borrowed_statics.sources.emplace_back(
                std::move(specialization), property.name);
        }
    }
    auto& object = class_heap.object(handle);
    const auto prior_properties = object.properties;
    const auto callback_on = [&](const runtime::SystemVerilogClassHandle target,
                                 const std::string_view name) {
        run_randomize_callback(target, name, calling_process);
    };
    const auto callback = [&](const std::string_view name) {
        if (lowered == nullptr) {
            callback_on(handle, name);
        }
    };
    try {
        callback("pre_randomize");
    } catch (...) {
        object.properties = prior_properties;
        throw;
    }
    // Objects reached through class-handle properties join the solve; rand
    // handles extend randomization to theirs, whose pre_randomize runs after
    // the parent's (IEEE 1800-2017 18.5.9, 18.6.1).
    auto members = lowered != nullptr
        ? lowered->members
        : runtime::systemverilog_class_randomize_members(class_heap, handle);
    std::vector<std::pair<runtime::SystemVerilogClassHandle,
        std::vector<runtime::SystemVerilogClassPropertyValue>>>
        prior_members;
    try {
        for (const auto& member : members) {
            prior_members.emplace_back(member.handle,
                class_heap.object(member.handle).properties);
            if (member.randomized && lowered == nullptr) {
                callback_on(member.handle, "pre_randomize");
            }
        }
    } catch (...) {
        object.properties = prior_properties;
        for (auto& [member_handle, properties] : prior_members) {
            class_heap.object(member_handle).properties = std::move(properties);
        }
        throw;
    }
    runtime::SystemVerilogClassRandomizeRequest request;
    // Bound the enumerated domains, as scope randomize does, so a wide
    // random property fails cleanly instead of exhausting memory.
    request.limits.maximum_domain_values = std::size_t { 1U } << 22U;
    application_detail::configure_systemverilog_randomize_selection(
        request, selected_names);
    request.call_identity = object.specialization_identity
        + "::randomize@source";
    // A random enumerated property takes only its type's values (IEEE
    // 1800-2017 18.4).
    const auto enumeration_domains = [&](const std::string& specialization) {
        std::map<std::string, std::vector<runtime::PackedLogic4>, std::less<>>
            domains;
        for (const auto& property :
            class_specialization(specialization).properties) {
            if (property.enumeration_domain.empty() || property.static_storage
                || property.bit_width == 0U || property.bit_width > 64U) {
                continue;
            }
            auto& domain = domains[property.canonical_identity];
            domain.clear();
            for (const auto value : property.enumeration_domain) {
                const auto masked = property.bit_width == 64U
                    ? value
                    : value
                        & ((std::uint64_t { 1 } << property.bit_width) - 1U);
                const auto encoded = runtime::PackedLogic4::from_aval_bval(
                    property.bit_width, masked, 0U);
                if (std::ranges::find(domain, encoded) == domain.end()) {
                    domain.push_back(encoded);
                }
            }
        }
        return domains;
    };
    // The string properties of an object are state its constraints read,
    // as the keys of a string-indexed array (IEEE 1800-2017 7.8, 18.5).
    const auto string_state = [this](
        const runtime::SystemVerilogClassHandle target,
        const std::string& specialization) {
        std::map<std::string, std::string, std::less<>> values;
        for (const auto& property :
            class_specialization(specialization).properties) {
            if (property.static_storage
                || property.type.value_form != semantic::sv::TypeForm::string
                || property.type.container_form) {
                continue;
            }
            try {
                values.emplace(property.canonical_identity,
                    class_heap.property(target, property.canonical_identity)
                        .string);
            } catch (const std::out_of_range&) {
            }
        }
        return values;
    };
    const auto class_constraints_of = [this, string_state](
        const runtime::SystemVerilogClassHandle target,
        std::string specialization, std::string path) {
        return runtime::SystemVerilogClassConstraintConfigurator {
            [this, target, string_state,
                specialization = std::move(specialization),
                path = std::move(path)](
                auto& solver, const auto& variables) {
                // Unit-scope variables the constraints read join the solve
                // as fixed variables holding their current values (IEEE
                // 1800-2017 18.5).
                auto solved_variables = variables;
                add_constraint_state_variables(
                    solver, solved_variables,
                    class_specialization(specialization));
                application_detail::configure_systemverilog_class_constraints(
                    solver, solved_variables, built.systemverilog_hir,
                    class_specialization(specialization),
                    [this, target](const auto identity) {
                        if (const auto shared
                            = static_constraint_modes.find(identity);
                            shared != static_constraint_modes.end()) {
                            return shared->second;
                        }
                        return class_heap.constraint_mode(target, identity);
                    },
                    path, string_state(target, specialization),
                    class_instance_parameters(
                        class_specialization(specialization)));
            }
        };
    };
    request.property_domains = enumeration_domains(
        object.specialization_identity);
    request.class_constraints = class_constraints_of(
        handle, object.specialization_identity, std::string { });
    for (auto& member : members) {
        const auto& specialization
            = class_heap.object(member.handle).specialization_identity;
        member.property_domains = enumeration_domains(specialization);
        // Only an object reached through rand handles adds its constraints;
        // others are state the solve may read (IEEE 1800-2017 18.5.9).
        if (member.randomized) {
            member.class_constraints = class_constraints_of(
                member.handle, specialization, member.path);
        }
    }
    if (!inline_constraints.empty()) {
        request.inline_constraints
            = [inline_constraints,
                  identity = object.specialization_identity](
                  auto& solver, const auto& variables) {
                  runtime::configure_systemverilog_inline_constraints(
                      solver, variables, inline_constraints,
                      identity + "::randomize@inline");
              };
    }
    // Unpacked arrays, dynamic arrays and queues of packed values join the
    // solve element by element (IEEE 1800-2017 18.4).
    std::vector<runtime::SystemVerilogClassRandomizeContainer> containers;
    const auto node_handle = [&](const std::size_t node) {
        return node == 0U ? handle : members[node - 1U].handle;
    };
    if (!class_constraint_property_types) {
        auto& types = class_constraint_property_types.emplace();
        const auto collect = [&](auto&& self,
                                 const semantic::sv::ConstraintExpression&
                                     expression) -> void {
            for (const auto& binding : expression.bindings) {
                if (binding.kind
                        == semantic::sv::ConstraintReferenceKind::property
                    && binding.canonical_identity.find('.')
                        == std::string::npos) {
                    types.try_emplace(binding.canonical_identity,
                        binding.type);
                }
            }
            for (const auto& operand : expression.operands) {
                self(self, operand);
            }
        };
        for (const auto& declaration : built.systemverilog_hir.classes()) {
            for (const auto& constraint : declaration.constraints) {
                for (const auto& expression : constraint.expressions) {
                    collect(collect, expression);
                }
            }
        }
    }
    using runtime::simir::ContainerElementKind;
    // The containers of a property and its packed elements, in pre-order,
    // each with its path from the property: a nested container by
    // element, a multi-dimensional fixed-size array by dimension (IEEE
    // 1800-2017 7.4.2, 7.4.5). False when an element is neither.
    const auto extent = [](const std::int64_t left, const std::int64_t right) {
        return static_cast<std::size_t>(
                   left >= right ? left - right : right - left)
            + 1U;
    };
    const auto fixed_shape = [](const std::int64_t left,
                                 const std::int64_t right) {
        runtime::SystemVerilogConstraintContainerShape shape;
        shape.fixed = true;
        shape.left = left;
        shape.descending = left > right;
        return shape;
    };
    const auto visit_container = [&](auto&& self,
                                     runtime::simir::ContainerValue& value,
                                     const std::string& path,
                                     const auto& leaf,
                                     const auto& node,
                                     const auto& aggregate) -> bool {
        const auto& type = value.type;
        if (type.fixed && type.dimensions.size() > 1U) {
            // Its elements are packed values or unpacked structures.
            const bool structures
                = type.element_kind == ContainerElementKind::Aggregate
                && !type.aggregate_value;
            if (type.element_kind != ContainerElementKind::Packed
                && !structures) {
                return false;
            }
            const auto rank = type.dimensions.size();
            std::vector<std::size_t> strides(rank, 1U);
            for (std::size_t k = rank - 1U; k-- > 0U;) {
                strides[k] = strides[k + 1U]
                    * extent(type.dimensions[k + 1U].first,
                        type.dimensions[k + 1U].second);
            }
            if (strides.front()
                    * extent(type.dimensions.front().first,
                        type.dimensions.front().second)
                != (structures ? value.nested_elements.size()
                               : value.elements.size())) {
                return false;
            }
            // Elements are stored with the rightmost dimension varying
            // fastest.
            bool listed = true;
            const auto dimension = [&](auto&& next, const std::size_t k,
                                       const std::size_t offset,
                                       const std::string& prefix) -> void {
                const auto [left, right] = type.dimensions[k];
                const auto count = extent(left, right);
                node(prefix, count, fixed_shape(left, right));
                for (std::size_t position = 0; position < count && listed;
                     ++position) {
                    const auto child
                        = prefix + "[" + std::to_string(position) + "]";
                    const auto at = offset + position * strides[k];
                    if (k + 1U != rank) {
                        next(next, k + 1U, at, child);
                    } else if (structures) {
                        listed = aggregate(child, value.nested_elements[at]);
                    } else {
                        leaf(child, value.elements[at]);
                    }
                }
            };
            dimension(dimension, 0U, 0U, path);
            return listed;
        }
        runtime::SystemVerilogConstraintContainerShape shape;
        if (type.fixed) shape = fixed_shape(type.index_left, type.index_right);
        if (type.associative) {
            shape.associative = true;
            shape.string_keys = type.string_indices;
            shape.keys = value.keys;
            shape.string_key_values = value.string_keys;
        }
        if (type.element_kind == ContainerElementKind::Packed) {
            node(path, value.elements.size(), shape);
            for (std::size_t index = 0; index < value.elements.size();
                 ++index) {
                leaf(path + "[" + std::to_string(index) + "]",
                    value.elements[index]);
            }
            return true;
        }
        // An array of unpacked structures: each element's members.
        if (type.element_kind == ContainerElementKind::Aggregate
            && !type.aggregate_value) {
            node(path, value.nested_elements.size(), shape);
            for (std::size_t index = 0; index < value.nested_elements.size();
                 ++index) {
                if (!aggregate(path + "[" + std::to_string(index) + "]",
                        value.nested_elements[index])) {
                    return false;
                }
            }
            return true;
        }
        if (type.element_kind != ContainerElementKind::Container) {
            return false;
        }
        node(path, value.nested_elements.size(), shape);
        for (std::size_t index = 0; index < value.nested_elements.size();
             ++index) {
            if (!self(self, value.nested_elements[index],
                    path + "[" + std::to_string(index) + "]", leaf, node,
                    aggregate)) {
                return false;
            }
        }
        return true;
    };
    const auto no_aggregate = [](const std::string&,
                                 runtime::simir::ContainerValue&) {
        return false;
    };
    // A nested, multi-dimensional or associative property is listed with
    // its whole shape and written back from this value.
    std::vector<std::optional<runtime::simir::ContainerValue>> shaped_values;
    // A structure property is listed member by member; each member is
    // written back to these bits (offset, width) of the property.
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>>
        structure_bits;
    const auto structure_of = [&](const semantic::sv::TypeReference& type) {
        return semantic::sv::systemverilog_structure_definition(
            built.systemverilog_hir, type);
    };
    // The bits a value of `type` takes in a structure's image.
    const auto image_width = [&](auto&&,
                                 const semantic::sv::TypeReference& type) {
        return semantic::sv::systemverilog_structure_image_width(
            built.systemverilog_hir, type);
    };
    struct StructureLeaf {
        std::string path;
        std::size_t offset { };
        std::size_t width { };
        const semantic::sv::TypeReference* type { };
        bool random { };
    };
    // The members of a structure, first member most significant; an
    // unpacked structure's member is random when declared rand, a packed
    // structure's members with the structure (IEEE 1800-2017 7.2, 18.4).
    const auto structure_leaves = [&](auto&& self,
                                      const semantic::sv::TypeDefinition&
                                          definition,
                                      const std::string& path,
                                      std::size_t top, const bool random,
                                      std::vector<StructureLeaf>& leaves)
        -> bool {
        const bool packed
            = definition.form == semantic::sv::TypeForm::packed_structure;
        for (const auto& member : definition.members) {
            const auto width = image_width(image_width, member.type);
            if (!width || *width == 0U || *width > top) return false;
            top -= *width;
            const bool member_random = random && (packed || member.random);
            if (const auto* nested = structure_of(member.type)) {
                if (!self(self, *nested, path + "." + member.name,
                        top + *width, member_random, leaves)) {
                    return false;
                }
                continue;
            }
            if (*width > 4096U) return false;
            leaves.push_back({ path + "." + member.name, top, *width,
                &member.type, member_random });
        }
        return true;
    };
    // The members of an unpacked structure held as a value (an element of
    // an array of structures), in declaration order: `visit` receives each
    // packed member's path, value, type and whether it is random.
    // An unpacked-array member is listed element by element, its shape
    // through `node` (7.2, 7.4).
    const auto box_leaves = [&](auto&& self,
                                runtime::simir::ContainerValue& box,
                                const semantic::sv::TypeDefinition& definition,
                                const std::string& path, const bool random,
                                const auto& visit, const auto& node) -> bool {
        if (box.nested_elements.size() != definition.members.size()) {
            return false;
        }
        for (std::size_t index = 0; index < definition.members.size();
             ++index) {
            const auto& member = definition.members[index];
            auto& held = box.nested_elements[index];
            const bool member_random = random && member.random;
            if (const auto* nested = structure_of(member.type)) {
                if (!self(self, held, *nested, path + "." + member.name,
                        member_random, visit, node)) {
                    return false;
                }
                continue;
            }
            // A member of a typedef'd array type has that array's shape.
            const auto& member_type = semantic::sv::systemverilog_array_alias_type(
                built.systemverilog_hir, member.type);
            const bool member_array = (member_type.container_form
                    && member_type.container_element_types.size() == 1U)
                || !member_type.unpacked_dimensions.empty();
            if (member_array && !held.type.aggregate_value) {
                // A fixed-size array member's elements have its type
                // without the unpacked dimensions.
                auto fixed_element = member_type;
                fixed_element.unpacked_dimensions.clear();
                const auto* element_type = member_type.container_form
                    ? &member_type.container_element_types.front()
                    : &fixed_element;
                while (element_type->container_form
                    && element_type->container_element_types.size() == 1U) {
                    element_type
                        = &element_type->container_element_types.front();
                }
                const auto* element_structure = structure_of(*element_type);
                const auto leaf = [&](const std::string& element_path,
                                      runtime::PackedLogic4& value) {
                    visit(element_path, value, *element_type, member_random);
                };
                const auto aggregate
                    = [&](const std::string& element_path,
                          runtime::simir::ContainerValue& element) {
                          return element_structure != nullptr
                              && self(self, element, *element_structure,
                                  element_path, member_random, visit, node);
                      };
                if (!visit_container(visit_container, held,
                        path + "." + member.name, leaf, node, aggregate)) {
                    return false;
                }
                continue;
            }
            if (held.elements.size() != 1U
                || (held.type.element_kind != ContainerElementKind::Packed
                    && held.type.element_kind
                        != ContainerElementKind::Scalar)) {
                return false;
            }
            // A real member is not a random variable (IEEE 1800-2017 18.4).
            visit(path + "." + member.name, held.elements.front(), member.type,
                member_random
                    && held.type.element_kind == ContainerElementKind::Packed);
        }
        return true;
    };
    // The default value of a fixed-size array of unpacked structures, in
    // the layout the elaborated design gives it: aggregate boxes of
    // one-element packed members (IEEE 1800-2017 7.2, 7.4.1).
    const auto box_type = [&](auto&& self,
                              const semantic::sv::TypeDefinition& definition)
        -> std::optional<runtime::simir::ContainerType> {
        runtime::simir::ContainerType box;
        box.element_kind = ContainerElementKind::Aggregate;
        box.element_width = 0U;
        box.aggregate_value = true;
        for (const auto& member : definition.members) {
            const auto& member_type = semantic::sv::systemverilog_array_alias_type(
                built.systemverilog_hir, member.type);
            if (member_type.container_form
                || !member_type.unpacked_dimensions.empty()) {
                return std::nullopt;
            }
            const auto* nested = structure_of(member.type);
            if (nested != nullptr
                && nested->form == semantic::sv::TypeForm::unpacked_structure) {
                auto inner = self(self, *nested);
                if (!inner) return std::nullopt;
                box.member_names.push_back(member.name);
                box.element_types.push_back(std::move(*inner));
                continue;
            }
            const auto width = image_width(image_width, member.type);
            if (!width || *width == 0U || *width > 4096U
                || (nested != nullptr
                    && nested->form
                        != semantic::sv::TypeForm::packed_structure)) {
                return std::nullopt;
            }
            runtime::simir::ContainerType leaf;
            leaf.element_kind = ContainerElementKind::Packed;
            leaf.element_width = static_cast<std::uint32_t>(*width);
            leaf.two_state = !member.type.four_state;
            leaf.signed_elements = member.type.signed_value;
            leaf.fixed = true;
            leaf.dimensions.emplace_back(0, 0);
            box.member_names.push_back(member.name);
            box.element_types.push_back(std::move(leaf));
        }
        return box;
    };
    const auto default_structure_array
        = [&](const semantic::sv::TypeDefinition& definition,
              const semantic::sv::TypeReference& type)
        -> std::optional<runtime::simir::ContainerValue> {
        if (type.unpacked_dimensions.size() != 1U) return std::nullopt;
        const auto& range = type.unpacked_dimensions.front();
        if (!range.left || !range.right
            || extent(*range.left, *range.right) > (1U << 20)) {
            return std::nullopt;
        }
        auto element = box_type(box_type, definition);
        if (!element) return std::nullopt;
        auto array = std::move(*element);
        array.aggregate_value = false;
        array.fixed = true;
        array.index_left = static_cast<std::int32_t>(*range.left);
        array.index_right = static_cast<std::int32_t>(*range.right);
        array.dimensions.emplace_back(array.index_left, array.index_right);
        return runtime::simir::default_container_value(array);
    };
    const auto leaf_profile = [](const semantic::sv::TypeReference& type,
                                 const runtime::PackedLogic4& value,
                                 const bool random) {
        runtime::SystemVerilogClassPropertyValue element;
        element.kind = type.four_state
            ? runtime::SystemVerilogClassPropertyKind::Logic4
            : runtime::SystemVerilogClassPropertyKind::Bit2;
        element.packed = value;
        element.random_state = runtime::SystemVerilogClassRandomState {
            random ? runtime::SystemVerilogClassRandomKind::Rand
                   : runtime::SystemVerilogClassRandomKind::None,
            value.width(),
            type.signed_value,
            type.target.spelling.empty()
                ? std::string { "$constraint-value" }
                : type.target.spelling,
            true, 0, 0, 0, 0, { } };
        return element;
    };
    // The structure of each array of structures' elements, for write-back.
    std::vector<const semantic::sv::TypeDefinition*> element_structures;
    for (std::size_t node = 0; node <= members.size(); ++node) {
        const auto target = node_handle(node);
        const auto& target_object = class_heap.object(target);
        for (const auto& property :
            class_specialization(target_object.specialization_identity)
                .properties) {
            if (!property.static_storage && !property.type.container_form) {
                const auto* structure = structure_of(property.type);
                if (structure == nullptr
                    || std::ranges::find(target_object.property_names,
                           property.canonical_identity)
                        == target_object.property_names.end()) {
                    continue;
                }
                const auto& held = class_heap.property(
                    target, property.canonical_identity);
                // A structure held as a value (one with an unpacked-array
                // member) joins the solve member by member and element by
                // element (IEEE 1800-2017 7.2, 18.4).
                if (held.container && held.container->type.aggregate_value
                    && structure->form
                        == semantic::sv::TypeForm::unpacked_structure) {
                    runtime::SystemVerilogClassRandomizeContainer container;
                    container.node = node;
                    container.name = property.canonical_identity;
                    auto shaped = *held.container;
                    const auto shape_node
                        = [&](const std::string& path, const std::size_t size,
                              runtime::SystemVerilogConstraintContainerShape
                                  shape) {
                              container.shapes.push_back(
                                  { path, size, std::move(shape) });
                          };
                    if (!box_leaves(box_leaves, shaped, *structure,
                            std::string { }, true,
                            [&](const std::string& member_path,
                                const runtime::PackedLogic4& value,
                                const semantic::sv::TypeReference& type,
                                const bool random) {
                                container.element_paths.push_back(member_path);
                                container.values.push_back(value);
                                container.element_profiles.push_back(
                                    leaf_profile(type, value, random));
                            },
                            shape_node)
                        || container.element_profiles.empty()) {
                        continue;
                    }
                    container.element = container.element_profiles.front();
                    shaped_values.resize(containers.size());
                    shaped_values.push_back(std::move(shaped));
                    element_structures.resize(containers.size());
                    element_structures.push_back(structure);
                    containers.push_back(std::move(container));
                    continue;
                }
                const auto width = structure == nullptr ? std::nullopt
                    : image_width(image_width, property.type);
                std::vector<StructureLeaf> leaves;
                if (structure == nullptr || !width
                    || held.packed.width() != *width
                    || !structure_leaves(structure_leaves, *structure,
                        std::string { }, *width, true, leaves)) {
                    continue;
                }
                runtime::SystemVerilogClassRandomizeContainer container;
                container.node = node;
                container.name = property.canonical_identity;
                std::vector<std::pair<std::size_t, std::size_t>> bits;
                for (const auto& leaf : leaves) {
                    runtime::PackedLogic4 value(
                        leaf.width, runtime::Logic4::zero);
                    for (std::size_t bit = 0; bit < leaf.width; ++bit) {
                        value.set(bit, held.packed.get(leaf.offset + bit));
                    }
                    auto element = leaf_profile(*leaf.type, value, leaf.random);
                    // An enumerated member takes only its type's values
                    // (IEEE 1800-2017 18.4).
                    const auto* enumeration
                        = semantic::sv::systemverilog_type_definition(
                            built.systemverilog_hir, *leaf.type);
                    if (enumeration != nullptr && leaf.width <= 64U
                        && enumeration->form
                            == semantic::sv::TypeForm::enumeration) {
                        auto& domains = node == 0U
                            ? request.property_domains
                            : members[node - 1U].property_domains;
                        auto& domain = domains[property.canonical_identity
                            + leaf.path];
                        domain.clear();
                        for (const auto literal :
                            semantic::sv::systemverilog_enumeration_values(
                                built.systemverilog_hir, *enumeration)) {
                            const auto encoded
                                = runtime::PackedLogic4::from_aval_bval(
                                    leaf.width,
                                    leaf.width == 64U ? literal
                                        : literal
                                            & ((std::uint64_t { 1 }
                                                   << leaf.width)
                                                - 1U),
                                    0U);
                            if (std::ranges::find(domain, encoded)
                                == domain.end()) {
                                domain.push_back(encoded);
                            }
                        }
                        if (domain.empty()) {
                            domains.erase(
                                property.canonical_identity + leaf.path);
                        }
                    }
                    container.element_paths.push_back(leaf.path);
                    container.values.push_back(element.packed);
                    container.element_profiles.push_back(std::move(element));
                    bits.emplace_back(leaf.offset, leaf.width);
                }
                container.element = container.element_profiles.front();
                shaped_values.resize(containers.size());
                shaped_values.emplace_back();
                structure_bits.resize(containers.size());
                structure_bits.push_back(std::move(bits));
                containers.push_back(std::move(container));
                continue;
            }
            if (property.static_storage || !property.type.container_form
                || property.type.container_element_types.size() != 1U
                || semantic::sv::nested_class_handle_container(
                    property.type)) {
                continue;
            }
            const auto* leaf_type = &property.type;
            while (leaf_type->container_form
                && leaf_type->container_element_types.size() == 1U) {
                leaf_type = &leaf_type->container_element_types.front();
            }
            if (leaf_type->container_form) continue;
            const bool nested = property.type.container_element_types.front()
                                    .container_form.has_value();
            const auto form = *property.type.container_form;
            const bool resizable
                = form == semantic::sv::TypeForm::dynamic_array
                || form == semantic::sv::TypeForm::queue;
            if (!resizable && form != semantic::sv::TypeForm::static_array
                && form != semantic::sv::TypeForm::associative_array) {
                continue;
            }
            // An array of unpacked structures joins the solve member by
            // member of each element (`a[i].m`, IEEE 1800-2017 7.2, 18.4);
            // it keeps its size. A fixed-size one never written holds its
            // default elements.
            const auto* element_structure = structure_of(*leaf_type);
            const bool written = std::ranges::find(
                                     target_object.property_names,
                                     property.canonical_identity)
                != target_object.property_names.end();
            auto structure_array = element_structure != nullptr && written
                    && element_structure->form
                        == semantic::sv::TypeForm::unpacked_structure
                ? class_heap.property(target, property.canonical_identity)
                      .container
                : std::nullopt;
            if (!structure_array && element_structure != nullptr && written
                && element_structure->form
                    == semantic::sv::TypeForm::unpacked_structure
                && form == semantic::sv::TypeForm::static_array && !nested) {
                structure_array = default_structure_array(
                    *element_structure, property.type);
            }
            if (structure_array) {
                runtime::SystemVerilogClassRandomizeContainer container;
                container.node = node;
                container.name = property.canonical_identity;
                auto shaped = *structure_array;
                bool valid = true;
                const auto leaf = [&](const std::string&,
                                      const runtime::PackedLogic4&) {
                    valid = false;
                };
                const auto shape_node = [&](const std::string& path,
                                            const std::size_t size,
                                            runtime::SystemVerilogConstraintContainerShape
                                                shape) {
                    container.shapes.push_back(
                        { path, size, std::move(shape) });
                };
                const auto aggregate = [&](const std::string& path,
                                           runtime::simir::ContainerValue& box) {
                    return box_leaves(box_leaves, box, *element_structure, path,
                        true,
                        [&](const std::string& member_path,
                            const runtime::PackedLogic4& value,
                            const semantic::sv::TypeReference& type,
                            const bool random) {
                            container.element_paths.push_back(member_path);
                            container.values.push_back(value);
                            container.element_profiles.push_back(
                                leaf_profile(type, value, random));
                        },
                        shape_node);
                };
                if (!visit_container(visit_container, shaped, std::string { },
                        leaf, shape_node, aggregate)
                    || !valid) {
                    continue;
                }
                // A dynamic array or queue of structures without nested
                // containers can be resized: its members' paths and
                // initial values for a new element (IEEE 1800-2017 7.5.1).
                const auto element_box = resizable && !nested
                        && container.shapes.size() == 1U
                    ? runtime::simir::nested_element_container_type(
                          shaped.type)
                    : std::nullopt;
                if (element_box) {
                    auto box = runtime::simir::default_container_value(
                        *element_box);
                    bool flat = true;
                    if (box_leaves(box_leaves, box, *element_structure,
                            std::string { }, true,
                            [&](const std::string& member_path,
                                const runtime::PackedLogic4& value,
                                const semantic::sv::TypeReference& type,
                                const bool random) {
                                container.member_paths.push_back(member_path);
                                container.member_profiles.push_back(
                                    leaf_profile(type, value, random));
                            },
                            [&](const std::string&, std::size_t,
                                const runtime::SystemVerilogConstraintContainerShape&) {
                                flat = false;
                            })
                        && flat && !container.member_paths.empty()) {
                        container.resizable = true;
                    } else {
                        container.member_paths.clear();
                        container.member_profiles.clear();
                    }
                }
                container.element = container.element_profiles.empty()
                    ? leaf_profile(*leaf_type,
                          runtime::PackedLogic4(1U, runtime::Logic4::zero),
                          false)
                    : container.element_profiles.front();
                shaped_values.resize(containers.size());
                shaped_values.push_back(std::move(shaped));
                element_structures.resize(containers.size());
                element_structures.push_back(element_structure);
                containers.push_back(std::move(container));
                continue;
            }
            // The constraints' profile of the elements, where they name
            // the array, matches the variables their clauses read.
            const auto named = class_constraint_property_types->find(
                property.canonical_identity);
            // Otherwise the element type, through its typedefs.
            auto declared = nested ? *leaf_type
                : named != class_constraint_property_types->end()
                ? named->second : property.type;
            std::vector<semantic::TypeId> typedef_path;
            while (!declared.executable_width && !declared.packed_range) {
                const auto& element
                    = declared.container_element_types.empty()
                    ? declared : declared.container_element_types.front();
                const auto* definition = element.target.target.valid()
                    ? built.systemverilog_hir.find_type(element.target.target)
                    : nullptr;
                // A typedef from another scope keeps only its spelling; it
                // resolves when the spelling names one typedef.
                if (definition == nullptr && !element.target.spelling.empty()) {
                    for (const auto& candidate :
                        built.systemverilog_hir.types()) {
                        if (candidate.name != element.target.spelling) {
                            continue;
                        }
                        if (definition != nullptr) {
                            definition = nullptr;
                            break;
                        }
                        definition = &candidate;
                    }
                }
                if (definition == nullptr || !definition->members.empty()) {
                    break;
                }
                // A typedef chain that returns to a type already followed
                // ends the walk.
                if (std::ranges::find(typedef_path, definition->id)
                    != typedef_path.end()) {
                    break;
                }
                typedef_path.push_back(definition->id);
                const auto spelling = declared.target.spelling;
                declared = definition->base;
                declared.target.spelling = spelling;
            }
            if (!declared.executable_width && declared.packed_range
                && declared.packed_range->left
                && declared.packed_range->right) {
                declared.executable_width = static_cast<std::uint64_t>(
                    support::index_distance(*declared.packed_range->left,
                        *declared.packed_range->right) + 1U);
            }
            const auto width = declared.executable_width.value_or(0U);
            if (width == 0U || width > 4096U
                || leaf_type->value_form == semantic::sv::TypeForm::string
                || leaf_type->value_form
                    == semantic::sv::TypeForm::class_handle
                || std::ranges::find(target_object.property_names,
                       property.canonical_identity)
                    == target_object.property_names.end()) {
                continue;
            }
            runtime::SystemVerilogClassRandomizeContainer container;
            container.node = node;
            container.name = property.canonical_identity;
            container.resizable = resizable;
            container.element.kind = declared.four_state
                ? runtime::SystemVerilogClassPropertyKind::Logic4
                : runtime::SystemVerilogClassPropertyKind::Bit2;
            const auto initial = declared.four_state
                ? runtime::Logic4::x : runtime::Logic4::zero;
            container.element.packed = runtime::PackedLogic4(width, initial);
            container.element.random_state
                = runtime::SystemVerilogClassRandomState {
                      runtime::SystemVerilogClassRandomKind::Rand,
                      static_cast<std::size_t>(width),
                      declared.signed_value,
                      declared.target.spelling.empty()
                          ? std::string { "$constraint-value" }
                          : declared.target.spelling,
                      true, 0, 0, 0, 0, { } };
            // An array of an enumeration takes only its values.
            if (!property.enumeration_domain.empty() && width <= 64U) {
                auto& domains = node == 0U
                    ? request.property_domains
                    : members[node - 1U].property_domains;
                auto& domain = domains[property.canonical_identity];
                domain.clear();
                for (const auto value : property.enumeration_domain) {
                    const auto masked = width == 64U ? value
                        : value & ((std::uint64_t { 1 } << width) - 1U);
                    const auto encoded = runtime::PackedLogic4::from_aval_bval(
                        width, masked, 0U);
                    if (std::ranges::find(domain, encoded) == domain.end()) {
                        domain.push_back(encoded);
                    }
                }
            }
            std::optional<runtime::simir::ContainerValue> shaped;
            if (const auto& stored = class_heap.property(
                    target, property.canonical_identity).container) {
                if (nested || stored->type.associative
                    || stored->type.dimensions.size() > 1U) {
                    shaped = *stored;
                } else if (stored->type.element_kind
                        != ContainerElementKind::Packed
                    || std::ranges::any_of(stored->elements,
                        [&](const auto& element) {
                            return element.width() != width;
                        })) {
                    continue;
                } else {
                    container.values = stored->elements;
                    if (stored->type.fixed) {
                        container.shapes.push_back({ std::string { },
                            stored->elements.size(),
                            fixed_shape(stored->type.index_left,
                                stored->type.index_right) });
                    }
                }
            } else if (!resizable && !nested
                && form == semantic::sv::TypeForm::static_array) {
                // An array never written holds its default elements.
                std::vector<runtime::simir::ContainerDimension> dimensions;
                for (const auto& range : property.type.unpacked_dimensions) {
                    if (!range.left || !range.right
                        || extent(*range.left, *range.right) > (1U << 20)) {
                        dimensions.clear();
                        break;
                    }
                    dimensions.emplace_back(
                        static_cast<std::int32_t>(*range.left),
                        static_cast<std::int32_t>(*range.right));
                }
                if (dimensions.empty()) continue;
                runtime::simir::ContainerType type;
                type.element_width = static_cast<std::uint32_t>(width);
                type.two_state = !declared.four_state;
                type.signed_elements = declared.signed_value;
                type.fixed = true;
                type.index_left = dimensions.front().first;
                type.index_right = dimensions.front().second;
                type.dimensions = dimensions;
                if (dimensions.size() > 1U) {
                    shaped = runtime::simir::default_container_value(type);
                } else {
                    container.values.assign(
                        extent(type.index_left, type.index_right),
                        container.element.packed);
                    container.shapes.push_back({ std::string { },
                        container.values.size(),
                        fixed_shape(type.index_left, type.index_right) });
                }
            }
            if (shaped) {
                // A nested or associative property keeps its sizes.
                container.resizable = false;
                container.shapes.clear();
                bool packed = true;
                const auto leaf = [&](const std::string& path,
                                      const runtime::PackedLogic4& value) {
                    packed = packed && value.width() == width;
                    container.element_paths.push_back(path);
                    container.values.push_back(value);
                };
                const auto shape_node = [&](const std::string& path,
                                            const std::size_t size,
                                            runtime::SystemVerilogConstraintContainerShape
                                                shape) {
                    container.shapes.push_back(
                        { path, size, std::move(shape) });
                };
                if (!visit_container(visit_container, *shaped, std::string { },
                        leaf, shape_node, no_aggregate)
                    || !packed) {
                    continue;
                }
            }
            shaped_values.resize(containers.size());
            shaped_values.push_back(std::move(shaped));
            containers.push_back(std::move(container));
        }
    }
    shaped_values.resize(containers.size());
    structure_bits.resize(containers.size());
    element_structures.resize(containers.size());
    const auto result = runtime::randomize_systemverilog_class_graph(
        class_heap, handle, request, members, containers);
    for (std::size_t c = 0; c < containers.size(); ++c) {
        const auto& container = containers[c];
        if (!container.committed) continue;
        if (!structure_bits[c].empty()) {
            auto& packed = class_heap.property(
                node_handle(container.node), container.name).packed;
            for (std::size_t leaf = 0; leaf < structure_bits[c].size();
                 ++leaf) {
                const auto [offset, width] = structure_bits[c][leaf];
                for (std::size_t bit = 0; bit < width; ++bit) {
                    packed.set(offset + bit, container.values[leaf].get(bit));
                }
            }
            continue;
        }
        auto& stored = class_heap.property(
            node_handle(container.node), container.name).container;
        if (auto& shaped = shaped_values[c]) {
            std::size_t next { };
            const auto leaf = [&](const std::string&,
                                  runtime::PackedLogic4& value) {
                value = container.values.at(next++);
            };
            const auto shape_node = [](const std::string&, std::size_t,
                                       const runtime::SystemVerilogConstraintContainerShape&) {
            };
            const auto aggregate = [&](const std::string& path,
                                       runtime::simir::ContainerValue& box) {
                return element_structures[c] != nullptr
                    && box_leaves(box_leaves, box, *element_structures[c], path,
                        true,
                        [&](const std::string&, runtime::PackedLogic4& value,
                            const semantic::sv::TypeReference&, bool) {
                            value = container.values.at(next++);
                        },
                        shape_node);
            };
            // A resized array of structures keeps its leading elements;
            // a new element starts from its default value.
            if (!container.member_paths.empty()) {
                const auto count = container.values.size()
                    / container.member_paths.size();
                const auto element_box
                    = runtime::simir::nested_element_container_type(
                        shaped->type);
                if (element_box) {
                    shaped->nested_elements.resize(count,
                        runtime::simir::default_container_value(*element_box));
                }
            }
            if (shaped->type.aggregate_value) {
                (void)aggregate(std::string { }, *shaped);
            } else {
                (void)visit_container(visit_container, *shaped,
                    std::string { }, leaf, shape_node, aggregate);
            }
            stored = std::move(*shaped);
            continue;
        }
        if (!stored) {
            runtime::simir::ContainerValue created;
            created.type.element_width = static_cast<std::uint32_t>(
                container.element.packed.width());
            created.type.two_state = container.element.kind
                == runtime::SystemVerilogClassPropertyKind::Bit2;
            created.type.signed_elements
                = container.element.random_state->signed_value;
            created.type.queue = false;
            created.type.fixed = !container.resizable;
            if (!container.resizable) {
                const auto last = static_cast<std::int32_t>(
                    container.values.size()) - 1;
                const auto left = container.shapes.empty()
                    ? 0 : static_cast<std::int32_t>(
                          container.shapes.front().shape.left);
                const auto right = container.shapes.empty()
                    || !container.shapes.front().shape.descending
                    ? left + last : left - last;
                created.type.index_left = left;
                created.type.index_right = right;
                created.type.dimensions.emplace_back(left, right);
            }
            stored = std::move(created);
        }
        stored->elements = container.values;
    }
    if (result.language_result() != 0) {
        try {
            callback("post_randomize");
            for (const auto& member : members) {
                if (member.randomized && lowered == nullptr) {
                    callback_on(member.handle, "post_randomize");
                }
            }
        } catch (...) {
            object.properties = prior_properties;
            for (auto& [member_handle, properties] : prior_members) {
                class_heap.object(member_handle).properties
                    = std::move(properties);
            }
            throw;
        }
    }
    return runtime::PackedLogic4::from_aval_bval(
        32, result.language_result(), 0);
}

void Simulation::Impl::run_randomize_callback(
    const runtime::SystemVerilogClassHandle target,
    const std::string_view name,
    const std::optional<runtime::simir::ProcessId> calling_process)
{
    if (target == 0U) {
        return;
    }
    const auto* method = class_hir_execution.method_named(target, name);
    if (method == nullptr)
        return;
    const auto owner = randomize_callback_owner(*method);
    const auto returns_void = !method->return_type
        || method->return_type->target.spelling == "void";
    if (method->kind != semantic::sv::ClassMethodKind::function
        || method->static_method || !method->formals.empty()
        || !returns_void) {
        throw std::invalid_argument { std::string { name }
            + " of class '" + owner
            + "' must be a nonstatic zero-argument void function" };
    }
    std::vector<runtime::PackedLogic4> actuals;
    std::vector<std::string> string_actuals;
    // A callback that cannot complete is an error of the run, not a
    // randomization failure, which only an unsatisfiable solve is
    // (IEEE 1800-2023 18.6.1, 18.6.3).
    try {
        // The callbacks run in the process that called randomize()
        // (18.6.1), whose random number stream they draw from (18.14).
        (void)class_hir_execution.invoke_function(*method, target,
            actuals, string_actuals, { }, { }, { }, calling_process);
    } catch (const std::exception& error) {
        throw std::runtime_error { std::string { name } + " of class '"
            + owner + "' failed in randomize(): " + error.what() };
    }
}

std::string Simulation::Impl::randomize_callback_owner(
    const semantic::sv::SpecializedClassMethod& method)
{
    auto owner = method.owner_identity;
    if (const auto unit = owner.find("$unit@");
        unit != std::string::npos) {
        const auto end = owner.find("::", unit);
        owner.erase(unit + 5U,
            end == std::string::npos ? end : end - unit - 5U);
    }
    return owner;
}

std::string Simulation::Impl::randomize_callback_error(
    std::string message) const
{
    // A callback that cannot complete is an error of the run, not a
    // randomization failure, which only an unsatisfiable solve is
    // (IEEE 1800-2023 18.6.1, 18.6.3).
    for (const auto& context :
        std::ranges::reverse_view { randomize_callback_contexts }) {
        const auto* method
            = class_hir_execution.method_named(context.target, context.name);
        if (method == nullptr) {
            continue;
        }
        message = context.name + " of class '"
            + randomize_callback_owner(*method)
            + "' failed in randomize(): " + message;
    }
    return message;
}

runtime::PackedLogic4 Simulation::Impl::invoke_lowered_randomize_step(
    const runtime::simir::ProcessId process,
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view step,
    const std::span<const runtime::PackedLogic4> actuals)
{
    const auto done = runtime::PackedLogic4::from_aval_bval(1U, 0U, 0U);
    if (step.starts_with("@randomize-callback-enter:")) {
        randomize_callback_contexts.push_back({ process, handle,
            std::string { step.substr(step.find(':') + 1U) } });
        return done;
    }
    if (step == "@randomize-callback-leave") {
        if (randomize_callback_contexts.empty()
            || randomize_callback_contexts.back().process != process) {
            throw std::logic_error {
                "randomize() callback left outside its lowered call" };
        }
        randomize_callback_contexts.pop_back();
        return done;
    }
    // A kill (IEEE 1800-2023 9.7) that ends the killing process ends the
    // randomize() calls and callbacks it is in: they are set aside before
    // the kill and come back only when the kill returns.
    if (step == "@process-kill-begin") {
        killing_randomize_state = KilledRandomizeState {
            process,
            std::exchange(lowered_randomize_calls, { }),
            std::exchange(randomize_callback_contexts, { }),
        };
        return done;
    }
    if (step == "@process-kill-end") {
        if (!killing_randomize_state
            || killing_randomize_state->process != process) {
            throw std::logic_error { "process kill returned unmatched" };
        }
        lowered_randomize_calls = std::move(killing_randomize_state->calls);
        randomize_callback_contexts
            = std::move(killing_randomize_state->contexts);
        killing_randomize_state.reset();
        return done;
    }
    if (step == "@randomize-begin") {
        // The object itself must exist (8.4); the objects reached through
        // its handles are taken after its pre_randomize ran (18.6.1).
        (void)class_heap.object(handle);
        auto members
            = runtime::systemverilog_class_randomize_members(class_heap, handle);
        const auto count = members.size();
        lowered_randomize_calls.push_back({ handle, std::move(members) });
        return runtime::PackedLogic4::from_aval_bval(32U, count, 0U);
    }
    if (lowered_randomize_calls.empty()
        || lowered_randomize_calls.back().handle != handle) {
        throw std::logic_error { "randomize() step outside its lowered call" };
    }
    if (step == "@randomize-end") {
        lowered_randomize_calls.pop_back();
        return runtime::PackedLogic4::from_aval_bval(1U, 0U, 0U);
    }
    const auto& members = lowered_randomize_calls.back().members;
    const auto index = actuals.empty() ? members.size()
                                       : actuals.front().low_word().aval;
    // A member reached through a handle that is not rand joins the solve
    // without its callbacks (18.5.9).
    const auto member = index < members.size() && members[index].randomized
        ? members[index].handle
        : runtime::SystemVerilogClassHandle { };
    return runtime::PackedLogic4::from_aval_bval(64U, member, 0U);
}

[[nodiscard]] bool Simulation::Impl::static_constraint(
    const std::string_view identity)
{
    if (!static_constraint_identities) {
        auto& identities = static_constraint_identities.emplace();
        for (const auto& declaration : built.systemverilog_hir.classes()) {
            for (const auto& constraint : declaration.constraints) {
                if (constraint.static_constraint) {
                    identities.insert(constraint.canonical_identity);
                }
            }
        }
    }
    return static_constraint_identities->contains(identity);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_source_randomization_mode(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view method,
    const std::span<const runtime::PackedLogic4> actuals)
{
    // A static constraint's mode belongs to its class (IEEE 1800-2017
    // 18.9): every object reads and updates the same mode.
    constexpr std::string_view constraint_prefix { "@builtin-constraint-mode:" };
    if (method.starts_with(constraint_prefix)
        && static_constraint(method.substr(constraint_prefix.size()))) {
        const auto identity = method.substr(constraint_prefix.size());
        if (actuals.size() > 1U) {
            throw std::invalid_argument {
                "randomization mode method accepts zero or one argument"
            };
        }
        if (!actuals.empty()) {
            const auto value = actuals.front().low_word();
            if (value.bval != 0 || (value.aval != 0 && value.aval != 1)) {
                throw std::invalid_argument {
                    "randomization mode update requires a known zero or one"
                };
            }
            static_constraint_modes.insert_or_assign(
                std::string { identity }, value.aval != 0);
        }
        const auto mode = static_constraint_modes.find(identity);
        const bool enabled = mode == static_constraint_modes.end()
            || mode->second;
        return runtime::PackedLogic4::from_aval_bval(
            32, enabled ? 1U : 0U, 0);
    }
    // A static random variable's mode is shared by every instance (IEEE
    // 1800-2017 18.8): it lives with the variable in the static store.
    constexpr std::string_view random_prefix { "@builtin-rand-mode:" };
    std::vector<runtime::SystemVerilogClassRandomState*> static_states;
    std::optional<runtime::SystemVerilogClassRandomState*> named_static;
    if (method.starts_with(random_prefix) && handle != 0U) {
        const auto identity = method.substr(random_prefix.size());
        for (std::string current {
                 class_heap.object(handle).specialization_identity };
             !current.empty();) {
            const auto& specialization = class_specialization(current);
            for (const auto& property : specialization.properties) {
                if (!property.static_storage
                    || property.random_kind
                        == semantic::sv::ClassRandomKind::none) {
                    continue;
                }
                auto& stored = class_static_store.property(
                    static_property_specialization(
                        property.owner_identity, handle),
                    property.name);
                if (!stored.random_state) {
                    continue;
                }
                static_states.push_back(&*stored.random_state);
                if (identity
                    == property.owner_identity + "::" + property.name) {
                    named_static = &*stored.random_state;
                }
            }
            current = specialization.base
                ? specialization.base->specialization_identity
                : std::string { };
        }
    }
    if (named_static) {
        if (actuals.size() > 1U) {
            throw std::invalid_argument {
                "randomization mode method accepts zero or one argument"
            };
        }
        if (!actuals.empty()) {
            const auto value = actuals.front().low_word();
            if (value.bval != 0 || (value.aval != 0 && value.aval != 1)) {
                throw std::invalid_argument {
                    "randomization mode update requires a known zero or one"
                };
            }
            (*named_static)->enabled = value.aval != 0;
        }
        return runtime::PackedLogic4::from_aval_bval(
            32, (*named_static)->enabled ? 1U : 0U, 0);
    }
    auto result = class_execution.invoke_source_randomization_mode(
        handle, method, actuals);
    if (method == std::string { random_prefix } + "@object"
        && !actuals.empty()) {
        for (auto* const state : static_states) {
            state->enabled = actuals.front().low_word().aval != 0;
        }
    }
    return result;
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::resize_packed(
    const runtime::PackedLogic4& value,
    const std::size_t width)
{
    return application_detail::SystemVerilogClassExecution::resize_packed(
        value, width);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::packed_property_value(
    const runtime::SystemVerilogClassPropertyValue& value)
{
    return application_detail::SystemVerilogClassExecution::packed_property_value(
        value);
}

void Simulation::Impl::assign_property_value(
    runtime::SystemVerilogClassPropertyValue& destination,
    const std::string_view canonical_identity,
    const runtime::PackedLogic4& value,
    const std::string_view object_specialization)
{
    if (destination.kind
        == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
        const auto word = value.low_word();
        if (word.bval != 0) {
            throw std::invalid_argument {
                "class-handle property assignment contains X or Z"
            };
        }
        const auto [named_owner, name] = static_property_parts(
            canonical_identity);
        // A static property of a specialization is named through the
        // specialization's identity.
        const semantic::sv::ClassSpecialization* specialization = nullptr;
        std::string_view owner = named_owner;
        if (const auto exact = std::ranges::find(
                built.compiled_systemverilog_class_specializations,
                named_owner,
                &semantic::sv::ClassSpecialization::specialization_identity);
            exact != built.compiled_systemverilog_class_specializations.end()
            && exact->declaration_identity != named_owner) {
            owner = exact->declaration_identity;
            specialization = &*exact;
        } else if (const auto* generated
                   = generated_class_specialization(named_owner)) {
            // The class of a generated scope or design element instance.
            owner = generated->declaration_identity;
            specialization = generated;
        }
        // The property's type comes from the specialization of the object
        // that holds it, or of the base it inherits the property from: a
        // type parameter's actual can be a class (IEEE 1800-2017 8.25).
        for (auto current = specialization == nullptr
                 ? object_specialization
                 : std::string_view { };
             !current.empty();) {
            const auto& candidate = class_specialization(current);
            if (std::ranges::any_of(candidate.properties,
                    [&](const auto& property) {
                        return property.owner_identity == owner
                            && property.name == name;
                    })) {
                specialization = &candidate;
                break;
            }
            if (!candidate.base) {
                break;
            }
            current = candidate.base->specialization_identity;
        }
        if (specialization == nullptr) {
            specialization = &class_specialization(owner);
        }
        const auto property = std::ranges::find_if(
            specialization->properties, [&](const auto& candidate) {
                return candidate.owner_identity == owner
                    && candidate.name == name;
            });
        if (property == specialization->properties.end()) {
            throw std::out_of_range {
                "SystemVerilog class property profile '"
                + std::string { canonical_identity } + "' is not available"
            };
        }
        if (std::getenv("FSIM_SV_CLASS_SPEC_TRACE") != nullptr) {
            std::cerr << "[fsim-sv-class-spec] assign " << canonical_identity
                      << " object=" << object_specialization
                      << " selected=" << specialization->specialization_identity
                      << " type=" << property->type.class_identity << '\n';
        }
        if (word.aval != 0) {
            (void)class_heap.checked_cast(
                word.aval, property->type.class_identity);
        }
        destination.handle = word.aval;
        return;
    }
    destination.packed = resize_packed(value, destination.packed.width());
    // A 2-state property stores 0 for each X or Z bit written to it (IEEE
    // 1800-2023 6.3.2.1, 10.7).
    auto& stored = destination.packed;
    if (destination.kind != runtime::SystemVerilogClassPropertyKind::Bit2
        || stored.is_logic9() || stored.width() == 0U) {
        return;
    }
    if (stored.width() <= 64U) {
        const auto word = stored.unchecked_low_word();
        if (word.bval != 0U) {
            stored.assign_word({ word.width, word.aval & ~word.bval, 0U });
        }
        return;
    }
    for (std::size_t bit = 0U; bit < stored.width(); ++bit) {
        const auto state = stored.get(bit);
        if (state == runtime::Logic4::x || state == runtime::Logic4::z) {
            stored.set(bit, runtime::Logic4::zero);
        }
    }
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_class_container(
    const runtime::SystemVerilogClassHandle receiver,
    const std::string_view operation,
    const std::span<const runtime::PackedLogic4> actuals)
{
    return class_execution.invoke_class_container(
        receiver, operation, actuals);
}

[[nodiscard]] runtime::PackedLogic4 Simulation::Impl::invoke_checked_class_cast(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view operation) const
{
    return class_execution.invoke_checked_class_cast(handle, operation);
}

[[nodiscard]] std::pair<std::string_view, std::string_view>
Simulation::Impl::static_property_parts(const std::string_view identity)
{
    return application_detail::SystemVerilogClassExecution::static_property_parts(
        identity);
}

std::string Simulation::Impl::static_property_specialization(
    const std::string_view owner,
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view specialization) const
{
    // The class executor selected the specialization: the one a static
    // method runs in, or the one a selection names (IEEE 1800-2023 8.25.1).
    if (!specialization.empty()) {
        return std::string { specialization };
    }
    const auto registered = [&] {
        try {
            static_cast<void>(class_static_store.initialized(owner));
            return true;
        } catch (const std::out_of_range&) {
            return false;
        }
    }();
    if (handle == 0U || registered) {
        return std::string { owner };
    }
    try {
        for (std::string current {
                 class_heap.object(handle).specialization_identity };
             !current.empty();) {
            const auto& candidate = class_specialization(current);
            if (candidate.declaration_identity == owner) {
                return current;
            }
            if (!candidate.base) {
                break;
            }
            current = candidate.base->specialization_identity;
        }
    } catch (const std::exception&) {
    }
    return std::string { owner };
}

[[nodiscard]] runtime::SystemVerilogClassHandle Simulation::Impl::allocate_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type,
    const std::string_view allocation_scope)
{
    const auto& specialization = class_specialization(
        specialization_identity);
    runtime::SystemVerilogClassDescriptor descriptor;
    descriptor.dynamic_type = specialization.declaration_identity;
    descriptor.declared_type = declared_type.empty()
        ? descriptor.dynamic_type
        : std::string { declared_type };
    descriptor.specialization_identity = specialization.specialization_identity;
    const auto separator = allocation_scope.find('.');
    descriptor.random_root_identity = std::string {
        allocation_scope.substr(0, separator)
    };
    const auto* current = &specialization;
    std::vector<const semantic::sv::SpecializedClassRelation*> interfaces;
    while (current != nullptr) {
        descriptor.assignable_declared_types.push_back(
            current->declaration_identity);
        // The class of one generated scope is a type of its own, which a
        // lowered override dispatch tests the receiver against (IEEE
        // 1800-2017 27.4, 8.20), and so is each specialization of a
        // parameterized class, which a lowered `$cast` tests (IEEE
        // 1800-2023 8.16, 8.25).
        if (semantic::sv::has_own_static_storage(*current)) {
            descriptor.assignable_declared_types.push_back(
                current->specialization_identity);
        }
        for (const auto& implemented : current->interfaces) {
            interfaces.push_back(&implemented);
        }
        if (!current->base)
            break;
        current = &class_specialization(
            current->base->specialization_identity);
    }
    // A handle of a class converts to each interface class it implements,
    // and to the interface classes those extend (IEEE 1800-2017 8.26).
    for (std::size_t index { }; index < interfaces.size(); ++index) {
        const auto& implemented = *interfaces[index];
        if (std::ranges::find(descriptor.assignable_declared_types,
                implemented.declaration_identity)
            != descriptor.assignable_declared_types.end()) {
            continue;
        }
        descriptor.assignable_declared_types.push_back(
            implemented.declaration_identity);
        const auto interface_class = std::ranges::find(
            built.compiled_systemverilog_class_specializations,
            implemented.specialization_identity,
            &semantic::sv::ClassSpecialization::specialization_identity);
        if (interface_class
            == built.compiled_systemverilog_class_specializations.end()) {
            continue;
        }
        if (interface_class->base) {
            interfaces.push_back(&*interface_class->base);
        }
        for (const auto& extended : interface_class->interfaces) {
            interfaces.push_back(&extended);
        }
    }
    for (const auto& property : specialization.properties) {
        if (!property.static_storage) {
            descriptor.properties.push_back(
                class_property_descriptor(property, built.systemverilog_hir,
                    true, &built.design.class_container_types()));
        }
    }
    const auto declaration = std::ranges::find(
        built.systemverilog_hir.classes(),
        specialization.declaration_identity,
        &semantic::sv::ClassDeclaration::canonical_identity);
    if (declaration != built.systemverilog_hir.classes().end()) {
        for (const auto& constraint : declaration->composed_constraints) {
            if (constraint.override_legal) {
                descriptor.constraint_modes.emplace_back(
                    constraint.selected_identity,
                    constraint.mode_enabled);
            }
        }
    } else {
        for (const auto& constraint : specialization.constraints) {
            descriptor.constraint_modes.emplace_back(
                constraint.selected_identity, constraint.mode_enabled);
        }
    }
    return class_heap.allocate(descriptor);
}

std::string Simulation::Impl::service_construct_identity(
    const std::string_view specialization)
{
    const auto& selected = class_specialization(specialization);
    if (support::uvm_library(selected.uvm_role)
        || !is_systemverilog_uvm_type(
            built.compiled_systemverilog_class_specializations, selected,
            support::UvmRole::object)) {
        return std::string { specialization };
    }
    return "@service-construct:" + std::string { specialization };
}

[[nodiscard]] runtime::SystemVerilogClassHandle Simulation::Impl::construct_class(
    const std::string_view specialization_identity,
    const std::string_view declared_type,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals,
    const std::span<const std::string> actual_names,
    const std::string_view allocation_scope,
    const runtime::SystemVerilogUvmRootHandle requested_root,
    const std::optional<runtime::simir::ProcessId> owner_design_process,
    const runtime::SystemVerilogClassHandle owner_context_handle,
    const std::optional<runtime::simir::ProcessId> report_process)
{
    // `@no-constructor:` allocates the object for a constructor the
    // elaborated process runs itself.
    constexpr std::string_view lowered_constructor { "@no-constructor:" };
    // `@api-construct:` allocates a UVM object for the API: its constructor
    // chain runs as generated code in the dispatch process.
    constexpr std::string_view api_constructor { "@api-construct:" };
    // `@service-construct:` is the same for a host service a design process
    // can call (clone, the factory): inside the running design the calling
    // process runs the chain, so the object waits for it.
    constexpr std::string_view service_constructor { "@service-construct:" };
    const bool service_chain
        = specialization_identity.starts_with(service_constructor);
    bool api_chain = service_chain
        || specialization_identity.starts_with(api_constructor);
    bool run_constructor
        = !api_chain && !specialization_identity.starts_with(lowered_constructor);
    const auto identity = api_chain
        ? specialization_identity.substr(service_chain
              ? service_constructor.size() : api_constructor.size())
        : run_constructor
        ? specialization_identity
        : specialization_identity.substr(lowered_constructor.size());
    const auto& specialization = class_specialization(identity);
    // A construction the host makes outside the running design (the API,
    // the factory's and clone services called from the host) runs the
    // object's constructor chain as generated code in the dispatch process.
    // One made inside a running process cannot enter the scheduler again.
    if (run_constructor && interpreter && !interpreter->scheduler().running()
        && !support::uvm_library(specialization.uvm_role)
        && is_systemverilog_uvm_type(
            built.compiled_systemverilog_class_specializations,
            specialization, support::UvmRole::object)) {
        api_chain = true;
        run_constructor = false;
    }
    ClassOwnerParameterValues owner_parameters;
    std::optional<runtime::simir::ProcessId> owner_process_context;
    bool owner_context_available { };
    if (owner_context_handle != 0U) {
        const auto inherited = class_owner_parameter_values.find(
            owner_context_handle);
        if (inherited != class_owner_parameter_values.end()) {
            owner_parameters = inherited->second.parameters;
            owner_process_context = inherited->second.owner_design_process;
            owner_context_available = true;
        }
    }
    if (owner_design_process) {
        owner_process_context = owner_design_process;
        const semantic::design::ProcessOccurrence* owner_occurrence = nullptr;
        for (const auto& process : built.design_ir.processes()) {
            if (process.runtime_index != *owner_design_process) {
                continue;
            }
            if (owner_occurrence != nullptr) {
                throw std::logic_error {
                    "class allocation owner process is ambiguous"
                };
            }
            owner_occurrence = &process;
        }
        if (owner_occurrence == nullptr) {
            throw std::logic_error {
                "class allocation owner process has no design occurrence"
            };
        }
        const auto* exact = exact_specialization(
            built.design_ir, owner_occurrence->specialization);
        if (exact == nullptr) {
            throw std::logic_error {
                "class allocation owner has no exact specialization"
            };
        }
        const auto& owner_specialization = *exact;
        if (owner_specialization.language
                != semantic::Language::system_verilog
            || !owner_specialization.unit.valid()
            || !owner_specialization.scope.valid()) {
            throw std::logic_error {
                "class allocation owner specialization is not a resolved "
                "SystemVerilog unit"
            };
        }
        const semantic::sv::Unit* owner_unit = nullptr;
        for (const auto& unit : built.systemverilog_hir.units()) {
            if (unit.id != owner_specialization.unit) {
                continue;
            }
            if (owner_unit != nullptr) {
                throw std::logic_error {
                    "class allocation owner has ambiguous HIR unit identity"
                };
            }
            owner_unit = &unit;
        }
        if (owner_unit == nullptr
            || owner_unit->scope != owner_specialization.scope) {
            throw std::logic_error {
                "class allocation owner has no exact HIR unit scope"
            };
        }
        const auto owner_constant_declaration = [&](
            const semantic::design::ParameterValue& parameter)
            -> const semantic::sv::Declaration* {
            const auto is_owner_declaration = [&](
                const semantic::DeclarationId declaration_id) {
                return std::ranges::find(
                           owner_unit->declarations, declaration_id)
                    != owner_unit->declarations.end();
            };
            if (parameter.declaration) {
                if (!is_owner_declaration(*parameter.declaration)) {
                    return nullptr;
                }
                const auto* declaration
                    = built.systemverilog_hir.find_declaration(
                        *parameter.declaration);
                if (declaration == nullptr
                    || declaration->name != parameter.name) {
                    throw std::logic_error {
                        "class allocation owner parameter identity is stale"
                    };
                }
                if (declaration->form
                        != semantic::sv::DeclarationForm::parameter
                    && declaration->form
                        != semantic::sv::DeclarationForm::local_parameter
                    && declaration->form
                        != semantic::sv::DeclarationForm::type_parameter) {
                    return nullptr;
                }
                return &*declaration;
            }
            const semantic::sv::Declaration* selected = nullptr;
            for (const auto declaration_id : owner_unit->declarations) {
                const auto* declaration
                    = built.systemverilog_hir.find_declaration(declaration_id);
                if (declaration == nullptr
                    || declaration->name != parameter.name
                    || (declaration->form
                            != semantic::sv::DeclarationForm::parameter
                        && declaration->form
                            != semantic::sv::DeclarationForm::local_parameter
                        && declaration->form
                            != semantic::sv::DeclarationForm::type_parameter)) {
                    continue;
                }
                if (selected != nullptr) {
                    throw std::logic_error {
                        "class allocation owner parameter name is ambiguous"
                    };
                }
                selected = &*declaration;
            }
            return selected;
        };
        const auto packed_parameter_value = [&](
            const semantic::design::ParameterValue& parameter,
            const semantic::sv::Declaration& declaration)
            -> std::optional<runtime::PackedLogic4> {
            if (const auto decoded
                = elaboration::decode_hir_systemverilog_constant(
                    parameter.identity);
                decoded && decoded->packed.width() > 0U
                && decoded->packed.width() <= 64U) {
                return decoded->packed;
            }
            if (!declaration.type
                || !declaration.type->executable_width
                || *declaration.type->executable_width == 0U
                || *declaration.type->executable_width > 64U
                || declaration.type->container_form
                || !declaration.type->value_form
                || (*declaration.type->value_form
                        != semantic::sv::TypeForm::packed_integral
                    && *declaration.type->value_form
                        != semantic::sv::TypeForm::enumeration)) {
                return std::nullopt;
            }
            std::int64_t value { };
            const auto text = std::string_view { parameter.value };
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc { }
                || parsed.ptr != text.data() + text.size()) {
                return std::nullopt;
            }
            return runtime::PackedLogic4::from_aval_bval(
                static_cast<std::size_t>(
                    *declaration.type->executable_width),
                static_cast<std::uint64_t>(value), 0U);
        };
        owner_parameters.clear();
        for (const auto& parameter : owner_specialization.parameters) {
            const auto* declaration = owner_constant_declaration(parameter);
            if (declaration == nullptr) {
                // Specialization records also retain imported package values
                // and other identity metadata. They are not bindings in the
                // exact owning module's lexical scope.
                continue;
            }
            const bool module_parameter = declaration->form
                == semantic::sv::DeclarationForm::parameter;
            const auto value = packed_parameter_value(parameter, *declaration);
            if (!module_parameter && !value) {
                // A localparam can be evaluated from its retained HIR
                // initializer using this owner's exact parameter bindings.
                // Leave it absent here so that path is available.
                continue;
            }
            const bool inserted = owner_parameters.emplace(
                declaration->id, value).second;
            if (!inserted) {
                throw std::logic_error {
                    "class allocation owner has duplicate parameter "
                    "declaration identities"
                };
            }
        }
        owner_context_available = true;
    }
    const auto handle = allocate_class(
        specialization.specialization_identity,
        declared_type,
        allocation_scope);
    try {
        if (owner_context_available) {
            const auto [owner, inserted] = class_owner_parameter_values.emplace(
                handle, ClassOwnerParameterContext { std::move(owner_parameters), owner_process_context });
            (void)owner;
            if (!inserted) {
                throw std::logic_error {
                    "class allocation reused a live owner context handle"
                };
            }
        }
    } catch (...) {
        (void)class_heap.release(handle);
        throw;
    }
    // A lowered constructor chain runs in the allocating process: its
    // completion point announces the object (cbCreateObj, IEEE 1800-2023
    // 38.36.1), so the object waits here for it.
    if (foreign_probes && class_created_hook && !run_constructor
        && !api_chain) {
        constructed_classes.push_back(handle);
    }
    const auto before = packed_class_snapshot();
    // The library's root is a uvm_component, but it is the hierarchy's root,
    // not a top component of it (IEEE 1800.2-2020 13.2): the native
    // hierarchy answers its methods and never lists it among the children.
    const auto component_specialization = is_systemverilog_uvm_type(
            built.compiled_systemverilog_class_specializations,
            specialization,
            support::UvmRole::component)
        && !is_systemverilog_uvm_type(
            built.compiled_systemverilog_class_specializations,
            specialization,
            support::UvmRole::root);
    UvmBaseConstruction uvm_base;
    uvm_base.component = component_specialization;
    auto& object_initialized = uvm_base.object_initialized;
    auto& automatic_root_created = uvm_base.automatic_root_created;
    auto& automatic_root = uvm_base.automatic_root;
    auto& automatic_root_identity = uvm_base.automatic_root_identity;
    // A lowered constructor chain of a class deriving from the library makes
    // the UVM base state when it reaches the library's constructor
    // (`@uvm-construct-base`), from the actuals of `super.new` (IEEE
    // 1800.2-2020 5.3.2, 13.1.2). The simulation allocates the library's own
    // objects without their constructors and makes their state here.
    const bool deferred_uvm_base = (!run_constructor || api_chain)
        && !support::uvm_library(specialization.uvm_role)
        && is_systemverilog_uvm_type(
            built.compiled_systemverilog_class_specializations,
            specialization, support::UvmRole::object);
    try {
        if (deferred_uvm_base) {
            uvm_pending_constructions.insert_or_assign(handle,
                UvmPendingConstruction {
                    std::string { allocation_scope }, requested_root, std::string { },
                    runtime::SystemVerilogClassHandle { }, false });
        } else {
            construct_uvm_base(specialization, handle, actuals,
                string_actuals, actual_names, allocation_scope,
                requested_root, uvm_base);
        }
        // The UVM base state exists once `super.new` has run, so a
        // user-derived constructor may already report through it; the
        // simulation's UVM services stand in for the library constructors.
        class_hir_execution.set_census_scope(std::string { allocation_scope }
            + (interpreter && interpreter->scheduler().running()
                    ? " in-run" : " idle"));
        if (run_constructor) {
            invoke_source_constructor(
                specialization, handle, actuals, string_actuals, actual_names,
                report_process);
        } else if (api_chain) {
            if (deferred_uvm_base) {
                auto& pending = uvm_pending_constructions.at(handle);
                pending.name = string_actuals.empty()
                    ? std::string { } : std::string { string_actuals[0] };
                pending.parent = actuals.size() > 1U
                    ? actuals[1].low_word().aval
                    : runtime::SystemVerilogClassHandle { };
                if (service_chain && interpreter->scheduler().running()) {
                    // The calling design process runs the generated chain
                    // right after the service returns.
                    if (foreign_probes && class_created_hook) {
                        constructed_classes.push_back(handle);
                    }
                    // The service returns the object to its caller, which
                    // needs its UVM state (registered, named) before the
                    // chain runs; the chain then completes the construction.
                    construct_uvm_base(specialization, handle, actuals,
                        string_actuals, actual_names, allocation_scope,
                        requested_root, uvm_base);
                    pending.base_made = true;
                } else if (!dispatch_uvm_api_construct(handle)) {
                    throw std::runtime_error {
                        "the constructor chain of class '"
                        + std::string { identity }
                        + "' does not lower to generated code"
                    };
                }
            } else if (!uvm_base.object_initialized) {
                // A library class has no generated constructor: the
                // simulation makes its base state.
                construct_uvm_base(specialization, handle, actuals,
                    string_actuals, actual_names, allocation_scope,
                    requested_root, uvm_base);
            }
        }
    } catch (...) {
        class_owner_parameter_values.erase(handle);
        uvm_pending_constructions.erase(handle);
        if (component_specialization) {
            if (uvm_components.contains(handle)) {
                try {
                    uvm_components.release(handle);
                } catch (...) {
                }
            } else {
                if (object_initialized)
                    uvm_objects.erase(handle);
                (void)class_heap.release(handle);
            }
            if (automatic_root_created
                && uvm_components.contains_root(automatic_root)) {
                try {
                    uvm_phases.unparticipate_standard_root(automatic_root);
                } catch (...) {
                }
                try {
                    uvm_components.destroy_root(automatic_root);
                } catch (...) {
                }
                uvm_roots_by_scope.erase(automatic_root_identity);
            }
        }
        if (component_specialization && class_heap.contains(handle)) {
            (void)class_heap.release(handle);
        }
        // HIR writes are not rolled back on this path. At the outermost
        // construction boundary, publish the current live-state diff; an
        // enclosing source call owns the snapshot when construction was nested.
        if (source_function_dispatch_depth == 0U) {
            try {
                notify_class_changes(before);
            } catch (...) {
            }
        }
        throw;
    }
    if (source_function_dispatch_depth == 0U)
        notify_class_changes(before);
    // The constructor the simulation ran itself has completed (cbCreateObj,
    // IEEE 1800-2023 38.36.1).
    if (foreign_probes && class_created_hook
        && (run_constructor
            || (api_chain
                && !(service_chain && interpreter->scheduler().running())))) {
        class_created_hook(handle);
    }
    return handle;
}

void Simulation::Impl::construct_uvm_base(
    const semantic::sv::ClassSpecialization& specialization,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals,
    const std::span<const std::string> actual_names,
    const std::string_view allocation_scope,
    const runtime::SystemVerilogUvmRootHandle requested_root,
    UvmBaseConstruction& state)
{
    if (is_systemverilog_uvm_type(
            built.compiled_systemverilog_class_specializations,
            specialization,
            support::UvmRole::object)) {
        // The class of a generated scope or design element instance is
        // registered when its first object is made.
        ensure_systemverilog_uvm_object_type(
            built.compiled_systemverilog_class_specializations,
            specialization, uvm_objects);
        uvm_objects.initialize(handle);
        state.object_initialized = true;
        std::size_t name_index { };
        if (!actual_names.empty()) {
            const auto found = std::ranges::find(actual_names, "name");
            if (found != actual_names.end()) {
                name_index = static_cast<std::size_t>(
                    std::distance(actual_names.begin(), found));
            }
        }
        if (name_index < string_actuals.size()
            && !string_actuals[name_index].empty()) {
            uvm_objects.set_name(handle, string_actuals[name_index]);
        }
    }
    // A TLM port's second constructor argument is its parent, and an
    // imp's is the object implementing it.
    if (is_systemverilog_uvm_type(
            built.compiled_systemverilog_class_specializations,
            specialization, support::UvmRole::port_base)
        && actuals.size() > 1U && actuals[1].low_word().bval == 0U) {
        uvm_tlm_owners[handle] = actuals[1].low_word().aval;
        // uvm_port_base::new(name, parent, port_type, ...): the port type
        // of an imp, the last literal of the library's uvm_port_type_e
        // (UVM_IMPLEMENTATION, IEEE 1800.2-2020 5.4.2.1).
        if (actuals.size() > 2U && actuals[2].low_word().bval == 0U) {
            if (const auto implementation
                = uvm_port_implementation_value(built.systemverilog_hir);
                implementation
                && actuals[2].low_word().aval == *implementation) {
                uvm_tlm_implementations.insert(handle);
            }
        }
    }
    if (state.component) {
        std::size_t name_index { };
        std::size_t parent_index { 1U };
        if (!actual_names.empty()) {
            const auto named_index = [&](const std::string_view name,
                                         const std::size_t fallback) {
                const auto found = std::ranges::find(actual_names, name);
                return found == actual_names.end()
                    ? fallback
                    : static_cast<std::size_t>(
                          std::distance(actual_names.begin(), found));
            };
            name_index = named_index("name", 0U);
            parent_index = named_index("parent", 1U);
        }
        std::string name = name_index < string_actuals.size()
            ? string_actuals[name_index]
            : std::string { };
        if (name.empty()) {
            name = "COMP_" + std::to_string(uvm_objects.instance_id(handle));
        }
        const auto parent = parent_index < actuals.size()
            ? actuals[parent_index].low_word().aval
            : runtime::SystemVerilogClassHandle { };
        auto root = requested_root;
        if (parent == 0 && root == 0) {
            state.automatic_root_identity = allocation_scope.empty()
                ? "$simulation"
                : std::string { allocation_scope };
            state.automatic_root_created = !uvm_roots_by_scope.contains(state.automatic_root_identity);
            root = component_root(allocation_scope);
            state.automatic_root = root;
        }
        uvm_components.initialize(handle, std::move(name), parent, root);
        construct_uvm_component_ports(specialization, handle,
            allocation_scope);
    }
}

runtime::SystemVerilogClassHandle Simulation::Impl::allocate_uvm_lowered(
    const runtime::SystemVerilogUvmTypeHandle wrapper,
    const std::string_view lowered,
    const runtime::SystemVerilogUvmRootHandle requested_root)
{
    const auto registered = uvm_registry.snapshot(wrapper);
    bool listed { };
    for (std::size_t begin { }; begin < lowered.size() && !listed;) {
        auto end = lowered.find('\n', begin);
        if (end == std::string_view::npos) {
            end = lowered.size();
        }
        listed = lowered.substr(begin, end - begin)
            == registered.specialization_identity;
        begin = end + 1U;
    }
    if (!listed) {
        return { };
    }
    return construct_class(
        "@no-constructor:" + registered.specialization_identity,
        registered.declaration_identity,
        std::span<const runtime::PackedLogic4> { },
        std::span<const std::string> { },
        std::span<const std::string> { },
        "$uvm-registry",
        requested_root);
}

void Simulation::Impl::construct_uvm_component_ports(
    const semantic::sv::ClassSpecialization& specialization,
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view allocation_scope)
{
    // The ports the library constructors of uvm_driver and uvm_sequencer
    // create (IEEE 1800.2-2020 15.2, 15.5); the library constructors do not
    // run for a component the simulation's UVM services construct.
    // The port properties by their recorded roles (support::UvmRole).
    constexpr std::array ports {
        support::UvmRole::driver_seq_item_port,
        support::UvmRole::driver_rsp_port,
        support::UvmRole::sequencer_seq_item_export,
        support::UvmRole::sequencer_rsp_export,
    };
    const auto known = [&](const std::string_view identity) {
        return std::ranges::any_of(
            built.compiled_systemverilog_class_specializations,
            [&](const semantic::sv::ClassSpecialization& item) {
                return item.specialization_identity == identity
                    || item.declaration_identity == identity;
            });
    };
    std::set<std::string> visited;
    for (const auto* current = &specialization; current != nullptr
         && visited.insert(current->specialization_identity).second;) {
        for (const auto& property : current->properties) {
            const auto port = std::ranges::find(ports, property.uvm_role);
            const auto& type = property.type;
            const auto identity = !type.class_specialization.empty()
                ? std::string_view { type.class_specialization }
                : std::string_view { type.class_identity };
            if (port == ports.end()
                || property.static_storage || identity.empty()
                || !known(identity)) {
                continue;
            }
            if (packed_property_value(class_heap.property(
                    handle, property.canonical_identity))
                    .low_word().aval != 0U) {
                continue;
            }
            const std::array<runtime::PackedLogic4, 2U> actuals {
                runtime::PackedLogic4::from_aval_bval(64U, 0U, 0U),
                runtime::PackedLogic4::from_aval_bval(64U, handle, 0U),
            };
            const std::array<std::string, 2U> names {
                property.name, std::string { } };
            const auto created = construct_class(
                "@no-constructor:" + std::string { identity },
                class_specialization(identity).declaration_identity,
                actuals, names, std::span<const std::string> { },
                allocation_scope);
            // The allocation may move the heap's objects.
            assign_property_value(
                class_heap.property(handle, property.canonical_identity),
                property.canonical_identity,
                runtime::PackedLogic4::from_aval_bval(64U, created, 0U),
                class_heap.object(handle).specialization_identity);
        }
        current = current->base
            && known(current->base->specialization_identity)
            ? &class_specialization(current->base->specialization_identity)
            : nullptr;
    }
}

[[nodiscard]] Simulation::Impl::PackedSnapshot
Simulation::Impl::packed_class_snapshot() const
{
    PackedSnapshot result;
    for (const auto handle : class_heap.live_handles()) {
        const auto& object = class_heap.object(handle);
        for (std::size_t index = 0; index < object.properties.size(); ++index) {
            const auto& property = object.properties[index];
            if (property.kind
                == runtime::SystemVerilogClassPropertyKind::ClassHandle) {
                // A handle property changes when it refers to another
                // object (IEEE 1800-2017 9.4.2); the change hook sees only
                // packed values.
                result.emplace(
                    std::pair { handle, object.property_names[index] },
                    runtime::PackedLogic4::from_aval_bval(
                        64U, property.handle, 0U));
            } else if (property.packed.width() != 0) {
                result.emplace(
                    std::pair { handle, object.property_names[index] },
                    property.packed);
            }
        }
    }
    return result;
}

void Simulation::Impl::toggle_class_heap_change()
{
    if (!class_heap_change_signal) {
        return;
    }
    const auto& current = interpreter->signal_value(*class_heap_change_signal);
    const bool high = current.width() != 0U
        && current.get(0U) == runtime::Logic4::one;
    interpreter->deposit_signal(*class_heap_change_signal,
        runtime::PackedLogic4 { 1U,
            high ? runtime::Logic4::zero : runtime::Logic4::one });
}

void Simulation::Impl::notify_class_changes(const PackedSnapshot& before)
{
    if ((!class_property_change_hook && !class_heap_change_signal)
        || interpreter->package_static_initialization_active())
        return;
    const auto after = packed_class_snapshot();
    bool changed = false;
    for (const auto& [identity, value] : after) {
        const auto prior = before.find(identity);
        if (prior == before.end() || prior->second != value) {
            changed = true;
            if (class_property_change_hook
                && class_heap.property(identity.first, identity.second).kind
                    != runtime::SystemVerilogClassPropertyKind::ClassHandle) {
                class_property_change_hook(
                    identity.first, identity.second, value,
                    interpreter->scheduler().now(),
                    interpreter->scheduler().delta());
            }
        }
    }
    if (changed) {
        toggle_class_heap_change();
    }
}

[[nodiscard]] Simulation::Impl::StaticPackedSnapshot
Simulation::Impl::packed_static_snapshot() const
{
    StaticPackedSnapshot result;
    for (const auto& state : class_static_store.snapshots()) {
        for (std::size_t index = 0; index < state.properties.size(); ++index) {
            if (state.properties[index].packed.width() != 0) {
                result.emplace(
                    std::pair { state.specialization_identity,
                        state.property_names[index] },
                    state.properties[index].packed);
            }
        }
    }
    return result;
}

void Simulation::Impl::notify_static_changes(const StaticPackedSnapshot& before)
{
    if ((!class_static_property_change_hook && !class_heap_change_signal)
        || interpreter->package_static_initialization_active())
        return;
    bool changed = false;
    for (const auto& [identity, value] : packed_static_snapshot()) {
        const auto prior = before.find(identity);
        if (prior == before.end() || prior->second != value) {
            changed = true;
            if (class_static_property_change_hook) {
                class_static_property_change_hook(
                    identity.first, identity.second, value,
                    interpreter->scheduler().now(),
                    interpreter->scheduler().delta());
            }
        }
    }
    if (changed) {
        toggle_class_heap_change();
    }
}

[[nodiscard]] runtime::SystemVerilogClassInvocationResult
Simulation::Impl::invoke_class_method(
    const std::string_view canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
    const std::optional<std::uint32_t> virtual_slot)
{
    const auto before = packed_class_snapshot();
    const auto static_before = packed_static_snapshot();
    auto result = virtual_slot
        ? class_methods.invoke_virtual(*virtual_slot, this_handle, actuals)
        : class_methods.invoke(canonical_method, this_handle, actuals);
    notify_class_changes(before);
    notify_static_changes(static_before);
    return result;
}

[[nodiscard]] std::optional<runtime::simir::SignalId>
Simulation::Impl::backdoor_signal(const std::string_view path) const noexcept
{
    const auto found = std::ranges::find_if(
        built.design_ir.objects(), [&](const auto& object) {
            return design_object_is_signal_bearing(object)
                && object.path.valid()
                && object.path.value() < built.design_ir.hierarchy_paths().size()
                && built.design_ir.path(object.path) == path
                && object.runtime_index
                    <= std::numeric_limits<runtime::simir::SignalId>::max();
        });
    return found == built.design_ir.objects().end()
        ? std::nullopt
        : std::optional<runtime::simir::SignalId> { static_cast<
              runtime::simir::SignalId>(found->runtime_index) };
}

Simulation::Impl::~Impl()
{
    if (coverage_database_path && lifecycle != Lifecycle::finished) {
        try {
            save_coverage_database();
        } catch (...) {
        }
    }
    if (vpi_started && !vpi_ended) {
        try {
            end_vpi();
        } catch (...) {
        }
    }
    if (systemc_start_attempted && !systemc_ended) {
        try {
            end_systemc();
        } catch (...) {
        }
    }
    if (vpi_registry && vpi_value_state_observer) {
        (void)vpi_registry->remove_value_state_observer(
            *vpi_value_state_observer);
    }
    // UVM roles the run leaves live are released while the interpreter
    // whose scheduler times their activity reports still exists: members
    // are destroyed in reverse order, the interpreter before the services.
    uvm_sequences.teardown_all_roles();
}

[[nodiscard]] runtime::SystemVerilogVpiStoredValue Simulation::Impl::vpi_value(
    const SignalId signal,
    PackedLogic4 value) const
{
    return systemverilog_vpi_signal_value(
        std::move(value), vpi_scalar_kinds.at(signal),
        vpi_categories.at(signal));
}

[[nodiscard]] runtime::SystemVerilogVpiStoredValue Simulation::Impl::vpi_driver_value(
    const SignalId signal,
    const SystemVerilogVpiDriverBinding& binding) const
{
    auto result = vpi_value(signal,
        interpreter->driver_value(binding.process, signal));
    result.strength = binding.strength;
    return result;
}

void Simulation::Impl::require_vpi_value(
    const runtime::SystemVerilogVpiValueError error,
    const std::string_view operation)
{
    if (error == runtime::SystemVerilogVpiValueError::None) {
        return;
    }
    throw std::logic_error { "live VPI " + std::string { operation }
        + " failed with error "
        + std::to_string(static_cast<unsigned>(error)) };
}

[[nodiscard]] runtime::SystemVerilogVpiValueError Simulation::Impl::apply_vpi_value_state(
    const runtime::SystemVerilogVpiValueStateUpdate& update)
{
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    try {
        if (vpi_read_only_container_words.contains(update.object)) {
            return runtime::SystemVerilogVpiValueError::ReadOnly;
        }
        auto word = vpi_word_handles.find(update.object);
        // An element of a net array is a net (IEEE 1800-2023 37.17): one
        // with its own signal is forced and released as that signal is
        // (10.6.2, 38.34); a deposit still writes the memory word.
        if (word != vpi_word_handles.end()) {
            if (const auto element = vpi_handle_signals.find(update.object);
                element != vpi_handle_signals.end()
                && (update.forced_value
                    || vpi_forced_signals.contains(element->second))) {
                const auto object = vpi_registry->lookup(update.object);
                if (object
                    && object.value->kind
                        == runtime::SystemVerilogVpiObjectKind::Net) {
                    word = vpi_word_handles.end();
                }
            }
        }
        if (word != vpi_word_handles.end()) {
            if (update.forced_value
                || vpi_forced_words.contains(update.object)) {
                return force_vpi_bridge_word(update, word->second.first,
                    word->second.second);
            }
            auto value = vpi_container_value(word->second.first);
            // A word of a string container takes a string (IEEE 1800-2023
            // 38.35).
            if (vpi_container_categories.at(word->second.first)
                == runtime::SystemVerilogVpiValueCategory::String) {
                const auto* text
                    = std::get_if<std::string>(&update.value.payload);
                if (text == nullptr) {
                    return runtime::SystemVerilogVpiValueError::TypeMismatch;
                }
                auto& element
                    = value.string_elements.at(word->second.second);
                if (element != *text) {
                    element = *text;
                    interpreter->deposit_container_object(
                        word->second.first, std::move(value));
                }
                return runtime::SystemVerilogVpiValueError::None;
            }
            auto& element = value.elements.at(word->second.second);
            const auto replacement = systemverilog_vpi_packed_value(
                update.value,
                vpi_container_scalar_kinds.at(word->second.first),
                vpi_container_categories.at(word->second.first));
            if (element != replacement) {
                element = replacement;
                interpreter->deposit_container_object(
                    word->second.first, std::move(value));
            }
            return runtime::SystemVerilogVpiValueError::None;
        }
        // A string variable takes a string (IEEE 1800-2023 38.15, 38.35).
        if (const auto text_object = vpi_string_handles.find(update.object);
            text_object != vpi_string_handles.end()) {
            if (update.forced_value) {
                return runtime::SystemVerilogVpiValueError::ReadOnly;
            }
            const auto* text = std::get_if<std::string>(&update.value.payload);
            if (text == nullptr) {
                return runtime::SystemVerilogVpiValueError::TypeMismatch;
            }
            if (interpreter->string_object_value(text_object->second)
                != *text) {
                interpreter->deposit_string_object(
                    text_object->second, *text);
            }
            return runtime::SystemVerilogVpiValueError::None;
        }
        // A put to a named event triggers it; the value is ignored
        // (IEEE 1800-2023 38.35).
        if (const auto event = vpi_event_signals.find(update.object);
            event != vpi_event_signals.end()) {
            if (update.forced_value) {
                return runtime::SystemVerilogVpiValueError::ReadOnly;
            }
            const auto current
                = interpreter->stored_signal_value_snapshot(event->second);
            auto toggled = current;
            if (toggled.width() != 0U) {
                toggled.set(0U, current.get(0U) == runtime::Logic4::one
                        ? runtime::Logic4::zero
                        : runtime::Logic4::one);
            }
            interpreter->deposit_signal(event->second, std::move(toggled),
                runtime::simir::ProcessSchedulingDomain::systemverilog);
            return runtime::SystemVerilogVpiValueError::None;
        }
        const auto signal = vpi_handle_signals.find(update.object);
        if (signal == vpi_handle_signals.end()) {
            return runtime::SystemVerilogVpiValueError::None;
        }
        const auto bound = signal->second;
        const auto packed = [&](const runtime::SystemVerilogVpiStoredValue& value) {
            return systemverilog_vpi_packed_value(value,
                vpi_scalar_kinds.at(bound), vpi_categories.at(bound));
        };
        if (update.forced_value) {
            // A deposit beneath the force is the value the signal returns to
            // on release (IEEE 1800-2023 38.34).
            if (const auto stored = packed(update.value);
                interpreter->stored_signal_value_snapshot(bound) != stored) {
                interpreter->deposit_signal(bound, stored,
                    runtime::simir::ProcessSchedulingDomain::systemverilog);
            }
            const auto forced = packed(*update.forced_value);
            if (!interpreter->signal_has_override(bound)
                || interpreter->signal_value_snapshot(bound) != forced) {
                interpreter->force_signal(bound, forced,
                    runtime::simir::ProcessSchedulingDomain::systemverilog);
            }
            vpi_forced_signals.insert(bound);
        } else if (vpi_forced_signals.erase(bound) != 0U) {
            // A released variable keeps its forced value until its next
            // assignment; a net returns to its drivers (IEEE 1800-2023
            // 10.6.2, 38.34).
            const auto object = vpi_registry->lookup(update.object);
            const bool variable = object
                && object.value->kind
                    == runtime::SystemVerilogVpiObjectKind::Variable;
            if (variable) {
                const auto kept = packed(update.value);
                if (interpreter->stored_signal_value_snapshot(bound) != kept) {
                    interpreter->deposit_signal(bound, kept,
                        runtime::simir::ProcessSchedulingDomain::systemverilog);
                }
            }
            if (interpreter->signal_is_forced(bound)) {
                interpreter->release_signal(bound,
                    runtime::simir::ProcessSchedulingDomain::systemverilog);
            }
            if (!variable) {
                // A net the kernel owns returns to its drivers' value.
                interpreter->rerun_kernel_drivers(bound);
            }
        } else {
            // A signal the kernel owns takes the value in its slot; any other
            // through the scheduler's signal store.
            const auto stored = packed(update.value);
            if (!interpreter->deposit_kernel_signal(bound, stored)
                && interpreter->stored_signal_value_snapshot(bound) != stored) {
                interpreter->deposit_signal(bound, stored,
                    runtime::simir::ProcessSchedulingDomain::systemverilog);
            }
        }
        return runtime::SystemVerilogVpiValueError::None;
    } catch (...) {
        return runtime::SystemVerilogVpiValueError::ResourceLimit;
    }
}

runtime::SystemVerilogVpiValueError Simulation::Impl::force_vpi_bridge_word(
    const runtime::SystemVerilogVpiValueStateUpdate& update,
    const runtime::simir::ContainerObjectId container,
    const std::size_t ordinal)
{
    // A word of a variable array is not forced (IEEE 1800-2023 10.6.2); a
    // word of a net array is a net (37.17), held here by the bridge.
    const auto object = vpi_registry->lookup(update.object);
    const auto bridge = interpreter->container_bridge_signal(container);
    if (!object
        || object.value->kind != runtime::SystemVerilogVpiObjectKind::Net
        || !bridge) {
        return runtime::SystemVerilogVpiValueError::ReadOnly;
    }
    auto value = vpi_container_value(container);
    const auto count = runtime::simir::container_value_size(value);
    const auto width
        = interpreter->stored_signal_value_snapshot(*bridge).width();
    if (count == 0U || ordinal >= count || width % count != 0U) {
        return runtime::SystemVerilogVpiValueError::ReadOnly;
    }
    // Element 0 is in the most significant bits.
    const auto element_width = width / count;
    const auto offset = (count - 1U - ordinal) * element_width;
    if (!update.forced_value) {
        // A released net returns to its drivers' value (10.6.2).
        vpi_forced_words.erase(update.object);
        interpreter->release_signal_slice(*bridge, offset, element_width);
        interpreter->rerun_kernel_drivers(*bridge);
        return runtime::SystemVerilogVpiValueError::None;
    }
    value.elements.at(ordinal) = systemverilog_vpi_packed_value(
        *update.forced_value, vpi_container_scalar_kinds.at(container),
        vpi_container_categories.at(container));
    const auto packed = runtime::simir::pack_container_signal_value(
        value, false);
    interpreter->force_signal_slice(
        *bridge, packed.extract_bits(offset, element_width), offset);
    vpi_forced_words.insert(update.object);
    return runtime::SystemVerilogVpiValueError::None;
}

void Simulation::Impl::publish_vpi_stored_signal(const SignalId signal)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    if (vpi_event_handles.contains(signal)) {
        publish_vpi_event(signal);
    }
    // The effective value reaches the observers through the signal change
    // hook; a stored change under a force is invisible until the release.
    if (!vpi_signal_watched(signal)) {
        return;
    }
    if (const auto drivers = vpi_driver_bindings.find(signal);
        drivers != vpi_driver_bindings.end()) {
        for (const auto& driver : drivers->second) {
            if (vpi_handle_watched(signal, driver.handle)) {
                announce_vpi_driver(signal, driver);
            }
        }
    }
}

void Simulation::Impl::announce_vpi_driver(
    const SignalId signal,
    const SystemVerilogVpiDriverBinding& binding)
{
    auto value = vpi_driver_value(signal, binding);
    auto [announced, inserted]
        = vpi_driver_announced.try_emplace(binding.handle, value);
    if (!inserted) {
        if (announced->second == value) {
            return;
        }
        announced->second = value;
    }
    require_vpi_value(
        vpi_registry->notify_live_value(binding.handle, std::move(value)),
        "driver publication");
}

void Simulation::Impl::register_foreign_net_delays(
    runtime::simir::Interpreter& target)
{
    using namespace runtime::simir;
    // The delayed loads: copy -> the net the load reads.
    std::map<SignalId, SignalId> nets;
    for (const auto& record : built.design.sdf_net_delays()) {
        if (record.delayed && record.load_signal != record.destination.signal) {
            nets.emplace(record.load_signal, record.destination.signal);
        }
    }
    if (nets.empty()) {
        return;
    }
    // The copy drivers (split_sdf_net_delays names them ".$mipd_driver"):
    // a load whose drivers read the net itself has one delay for every
    // source, which the net's change already carries to the copy. A load
    // whose drivers read shadows of the net (one for each group of sources)
    // misses a write that no source made, so its groups are declared.
    struct Found {
        bool grouped { };
        std::vector<ForeignNetDelayGroup> groups;
    };
    std::map<SignalId, Found> loads;
    for (std::size_t index = 0U; index < design_process_count; ++index) {
        const auto driver = running_process(static_cast<ProcessId>(index));
        const auto& program = target.process_program(driver);
        if (program.name.find(".$mipd_driver") == std::string::npos) {
            continue;
        }
        std::optional<SignalId> read;
        std::uint32_t slice_width { };
        for (const auto& operation : program.operations) {
            if (const auto* const input = operation_get_if<ReadSignal>(&operation)) {
                read = input->signal;
            } else if (const auto* const extract
                = operation_get_if<Extract>(&operation)) {
                slice_width = extract->width;
            } else if (const auto* const delayed
                = operation_get_if<WriteNetDelay>(&operation)) {
                const auto copy = nets.find(delayed->signal);
                if (copy == nets.end() || !read) {
                    break;
                }
                auto& load = loads[delayed->signal];
                load.grouped = load.grouped || *read != copy->second;
                load.groups.push_back({ driver, delayed->offset.value_or(0U),
                    delayed->offset
                        ? slice_width
                        : static_cast<std::uint32_t>(
                            target.signal_value(delayed->signal).width()),
                    NetDelayTable::from_values(delayed->table) });
                break;
            } else if (const auto* const plain
                = operation_get_if<WriteUpdate>(&operation)) {
                const auto copy = nets.find(plain->signal);
                if (copy == nets.end() || !read) {
                    break;
                }
                auto& load = loads[plain->signal];
                load.grouped = load.grouped || *read != copy->second;
                load.groups.push_back({ driver, 0U,
                    static_cast<std::uint32_t>(
                        target.signal_value(plain->signal).width()),
                    NetDelayTable { } });
                break;
            } else if (const auto* const slice
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                const auto copy = nets.find(slice->signal);
                if (copy == nets.end() || !read) {
                    break;
                }
                auto& load = loads[slice->signal];
                load.grouped = load.grouped || *read != copy->second;
                load.groups.push_back(
                    { driver, slice->offset, slice_width, NetDelayTable { } });
                break;
            }
        }
    }
    for (auto& [copy, load] : loads) {
        if (load.grouped) {
            target.add_foreign_net_delay_load(
                nets.at(copy), copy, std::move(load.groups));
        }
    }
}

void Simulation::Impl::start_interpreter(const bool restoring)
{
    if (restoring) {
        interpreter->start_for_restore();
    } else {
        interpreter->start();
    }
    refresh_observation_hooks();
}

void Simulation::Impl::publish_vpi_driver(
    const runtime::simir::ProcessId process,
    const SignalId signal)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    if (!vpi_signal_watched(signal)) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto drivers = vpi_driver_bindings.find(signal);
    if (drivers == vpi_driver_bindings.end()) {
        return;
    }
    for (const auto& driver : drivers->second) {
        if (driver.process == process
            && vpi_handle_watched(signal, driver.handle)) {
            announce_vpi_driver(signal, driver);
        }
    }
}

std::optional<runtime::simir::ContainerObjectId>
Simulation::Impl::class_static_container_object(
    const std::string_view identity, const std::string_view specialization) const
{

    if (!interpreter) {
        return std::nullopt;
    }
    std::string storage { identity };
    if (!specialization.empty()) {
        try {
            const auto& selected = class_specialization(specialization);
            if (semantic::sv::has_own_static_storage(selected)) {
                storage = selected.specialization_identity + "::"
                    + std::string { static_property_parts(identity).second };
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    const auto found = class_static_containers.find(storage);
    if (found == class_static_containers.end()) {
        return std::nullopt;
    }
    return found->second;

}

void Simulation::Impl::publish_vpi_container(
    const runtime::simir::ContainerObjectId object)
{
    // A watched static array property of a class (IEEE 1800-2023 38.36.1).
    if (const auto watched = class_static_container_watches.find(object);
        watched != class_static_container_watches.end()
        && class_static_container_change_hook) {
        class_static_container_change_hook(
            watched->second.first, watched->second.second);
    }
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto live = vpi_live_memories.find(object);
    auto watched = vpi_watched_containers.find(object);
    if (watched == vpi_watched_containers.end() && vpi_watch_all_values
        && vpi_container_words.contains(object)) {
        watched = vpi_watched_containers.emplace(object, std::nullopt).first;
    }
    if (live == vpi_live_memories.end()
        && watched == vpi_watched_containers.end()) {
        return;
    }
    auto value = interpreter->container_object_value_snapshot(object);
    if (live != vpi_live_memories.end()) {
        const auto before = live->second.words.size();
        sync_vpi_live_memory(object, value);
        // A resized array announces its size before its words' value
        // changes (IEEE 1800-2023 37.17 detail 14, 38.36.1).
        if (vpi_size_change_hook && live->second.words.size() != before) {
            vpi_size_change_hook(
                live->second.memory, before, live->second.words.size());
        }
    }
    if (watched == vpi_watched_containers.end()) {
        return;
    }
    // Only the words whose value changed are announced (IEEE 1800-2023
    // 38.36.1); the previous value is kept for watched memories alone.
    const auto& previous = watched->second;
    // An associative array's word is compared with its key's previous value,
    // not with the word that held its position before a key was added or
    // removed.
    std::vector<std::string> current_names;
    std::unordered_map<std::string, std::size_t> previous_positions;
    const auto associative = previous && value.type.associative;
    if (associative) {
        current_names = live != vpi_live_memories.end()
            ? live->second.word_names
            : systemverilog_vpi_associative_word_names(value);
        auto previous_names
            = systemverilog_vpi_associative_word_names(*previous);
        for (std::size_t ordinal = 0; ordinal < previous_names.size();
             ++ordinal) {
            previous_positions.emplace(
                std::move(previous_names[ordinal]), ordinal);
        }
    }
    if (const auto words = vpi_container_words.find(object);
        words != vpi_container_words.end()) {
        for (const auto& [handle, ordinal] : words->second) {
            // A watched string word is polled at the safe points instead.
            if (vpi_watched_strings.contains(handle)) {
                continue;
            }
            auto current = systemverilog_vpi_container_word_value(value,
                ordinal, vpi_container_scalar_kinds.at(object),
                vpi_container_categories.at(object));
            auto before = std::optional<std::size_t> { ordinal };
            if (associative) {
                const auto found = ordinal < current_names.size()
                    ? previous_positions.find(current_names[ordinal])
                    : previous_positions.end();
                before = found == previous_positions.end()
                    ? std::nullopt
                    : std::optional<std::size_t> { found->second };
            }
            if (previous && before
                && *before < runtime::simir::container_value_size(*previous)
                && systemverilog_vpi_container_word_value(*previous, *before,
                       vpi_container_scalar_kinds.at(object),
                       vpi_container_categories.at(object))
                    == current) {
                continue;
            }
            require_vpi_value(
                vpi_registry->notify_live_value(handle, current),
                "memory-word publication");
        }
    }
    watched->second = std::move(value);
}

void Simulation::Impl::materialize_vpi_children(
    const fsim_vpi_handle_v1 parent)
{
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto deferred = vpi_deferred_children.find(parent);
    if (deferred == vpi_deferred_children.end()) {
        return;
    }
    const auto children = std::move(deferred->second);
    vpi_deferred_children.erase(deferred);
    auto made = materialize_systemverilog_vpi_children(
        *vpi_registry, built, *interpreter, parent, children);
    if (children.memory) {
        auto& published = vpi_container_words[children.memory->container];
        for (const auto& [handle, ordinal] : made.words) {
            vpi_word_handles.emplace(handle,
                std::pair { children.memory->container, ordinal });
            published.push_back({ handle, ordinal });
        }
    }
    for (auto& [signal, driver] : made.drivers) {
        vpi_driver_handles.emplace(
            driver.handle, std::pair { driver.process, signal });
        vpi_driver_bindings[signal].push_back(std::move(driver));
    }
}

void Simulation::Impl::sync_vpi_live_memory(
    const runtime::simir::ContainerObjectId object,
    const runtime::simir::ContainerValue& value)
{
    auto& memory = vpi_live_memories.at(object);
    auto& words = vpi_container_words[object];
    const auto size = runtime::simir::container_value_size(value);
    // An associative array's words are named by their keys (IEEE 1800-2023
    // 37.17 detail 28), and a word's handle stays valid while its key exists
    // (37.3.8): a word whose key remains moves to the key's new position,
    // and only the words of removed keys are released.
    if (value.type.associative) {
        auto names = systemverilog_vpi_associative_word_names(value);
        if (names != memory.word_names) {
            std::unordered_map<std::string_view, std::size_t> position;
            for (std::size_t ordinal = 0; ordinal < names.size(); ++ordinal) {
                position.emplace(names[ordinal], ordinal);
            }
            std::vector<std::optional<fsim_vpi_handle_v1>> kept(names.size());
            for (std::size_t ordinal = 0; ordinal < memory.words.size();
                 ++ordinal) {
                const auto handle = memory.words[ordinal];
                const auto found = ordinal < memory.word_names.size()
                    ? position.find(memory.word_names[ordinal])
                    : position.end();
                if (found != position.end() && !kept[found->second]) {
                    kept[found->second] = handle;
                    vpi_word_handles[handle] = std::pair { object, found->second };
                    continue;
                }
                if (vpi_registry->release(handle)
                    != runtime::SystemVerilogVpiObjectError::None) {
                    throw std::logic_error {
                        "live VPI memory cannot release a removed word"
                    };
                }
                vpi_word_handles.erase(handle);
            }
            memory.word_names = std::move(names);
            memory.words.clear();
            words.clear();
            for (std::size_t ordinal = 0; ordinal < kept.size(); ++ordinal) {
                auto handle = kept[ordinal];
                if (!handle) {
                    handle = create_systemverilog_vpi_live_word(
                        *vpi_registry, memory, ordinal);
                    require_vpi_value(vpi_registry->bind_live_value(*handle),
                        "memory-word creation");
                    vpi_word_handles.emplace(*handle, std::pair { object, ordinal });
                }
                words.push_back({ *handle, ordinal });
                memory.words.push_back(*handle);
            }
        }
    }
    while (memory.words.size() > size) {
        const auto handle = memory.words.back();
        if (vpi_registry->release(handle)
            != runtime::SystemVerilogVpiObjectError::None) {
            throw std::logic_error {
                "live VPI memory cannot release a removed word"
            };
        }
        vpi_word_handles.erase(handle);
        words.pop_back();
        memory.words.pop_back();
    }
    while (memory.words.size() < size) {
        const auto ordinal = memory.words.size();
        const auto handle = create_systemverilog_vpi_live_word(
            *vpi_registry, memory, ordinal);
        require_vpi_value(vpi_registry->bind_live_value(handle),
            "memory-word creation");
        vpi_word_handles.emplace(handle, std::pair { object, ordinal });
        words.push_back({ handle, ordinal });
        memory.words.push_back(handle);
    }
}

void Simulation::Impl::publish_vpi_event(const SignalId event)
{
    if (!vpi_runtime_updates_enabled) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto objects = vpi_event_handles.find(event);
    if (objects == vpi_event_handles.end()) {
        return;
    }
    for (const auto object : objects->second) {
        const auto dispatched = vpi_callbacks->dispatch_named_event(object);
        if (dispatched
            != runtime::SystemVerilogVpiCallbackError::None) {
            throw std::logic_error {
                "live VPI named-event callback dispatch failed with error "
                + std::to_string(static_cast<unsigned>(dispatched))
            };
        }
    }
}

void Simulation::Impl::publish_vpi_signal(
    const SignalId signal,
    const PackedLogic4& value)
{
    if (!vpi_runtime_updates_enabled || !vpi_signal_watched(signal)) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    // Only the handles a library watches hear of the change; watched
    // handles also hold the signal's driver and event objects.
    const auto& handle_map = vpi_watch_all_values
        ? vpi_signal_handles
        : vpi_watched_handles;
    const auto handles_found = handle_map.find(signal);
    if (handles_found == handle_map.end()) {
        return;
    }
    const auto* handles = &handles_found->second;
    std::optional<runtime::SystemVerilogVpiStoredValue> published;
    for (const auto object : *handles) {
        if (!vpi_handle_signals.contains(object)) {
            continue;
        }
        if (!published) {
            published = vpi_value(signal, value);
        }
        require_vpi_value(
            vpi_registry->notify_live_value(object, *published),
            "value-change publication");
    }
}

std::optional<runtime::SystemVerilogVpiStoredValue>
Simulation::Impl::vpi_live_value(
    const fsim_vpi_handle_v1 handle, const bool stored) const
{
    // A forced signal shows its forced value, any other its stored value
    // (IEEE 1800-2023 10.6.2).
    if (const auto signal = vpi_handle_signals.find(handle);
        signal != vpi_handle_signals.end()) {
        const auto bound = signal->second;
        if (auto owned = interpreter->kernel_signal_value(bound)) {
            return vpi_value(bound, std::move(*owned));
        }
        return vpi_value(bound, !stored && interpreter->signal_has_override(bound)
                ? interpreter->signal_value_snapshot(bound)
                : interpreter->stored_signal_value_snapshot(bound));
    }
    if (const auto word = vpi_word_handles.find(handle);
        word != vpi_word_handles.end()) {
        const auto [container, ordinal] = word->second;
        const auto value = vpi_container_value(container);
        if (ordinal >= runtime::simir::container_value_size(value)) {
            return std::nullopt;
        }
        return systemverilog_vpi_container_word_value(value, ordinal,
            vpi_container_scalar_kinds.at(container),
            vpi_container_categories.at(container));
    }
    if (const auto text_object = vpi_string_handles.find(handle);
        text_object != vpi_string_handles.end()) {
        runtime::SystemVerilogVpiStoredValue text;
        text.payload = interpreter->string_object_value(text_object->second);
        return text;
    }
    if (const auto driver = vpi_driver_handles.find(handle);
        driver != vpi_driver_handles.end()) {
        const auto signal = driver->second.second;
        const auto bindings = vpi_driver_bindings.find(signal);
        if (bindings != vpi_driver_bindings.end()) {
            for (const auto& binding : bindings->second) {
                if (binding.handle == handle) {
                    return vpi_driver_value(signal, binding);
                }
            }
        }
    }
    return std::nullopt;
}

runtime::simir::ContainerValue Simulation::Impl::vpi_container_value(
    const runtime::simir::ContainerObjectId object) const
{
    if (auto owned = interpreter->kernel_container_object_value(object)) {
        return std::move(*owned);
    }
    return interpreter->container_object_value_snapshot(object);
}

bool Simulation::Impl::vpi_handle_watched(
    const SignalId signal, const fsim_vpi_handle_v1 handle) const
{
    if (vpi_watch_all_values) {
        return true;
    }
    const auto watched = vpi_watched_handles.find(signal);
    return watched != vpi_watched_handles.end()
        && std::ranges::find(watched->second, handle) != watched->second.end();
}

bool Simulation::Impl::vpi_signal_watched(const SignalId signal) const noexcept
{
    return vpi_watch_all_values
        || (signal < vpi_watched_signals.size()
            && vpi_watched_signals[signal] != 0U);
}

void Simulation::Impl::watch_vpi_object(
    const std::optional<fsim_vpi_handle_v1> object)
{
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    const auto watch_signal = [&](const SignalId signal) {
        if (signal >= vpi_watched_signals.size()) {
            vpi_watched_signals.resize(signal + 1U);
        }
        vpi_watched_signals[signal] = 1U;
        vpi_some_signal_watched = true;
        auto& handles = vpi_watched_handles[signal];
        if (std::ranges::find(handles, *object) == handles.end()) {
            handles.push_back(*object);
        }
    };
    if (object) {
        if (const auto signal = vpi_handle_signals.find(*object);
            signal != vpi_handle_signals.end()) {
            watch_signal(signal->second);
        } else if (const auto driver = vpi_driver_handles.find(*object);
                   driver != vpi_driver_handles.end()) {
            const auto driven = driver->second.second;
            watch_signal(driven);
            // The driver's changes are told from its value at the watch.
            if (const auto bindings = vpi_driver_bindings.find(driven);
                bindings != vpi_driver_bindings.end()) {
                for (const auto& binding : bindings->second) {
                    if (binding.handle == *object) {
                        vpi_driver_announced.insert_or_assign(
                            *object, vpi_driver_value(driven, binding));
                    }
                }
            }
        } else if (const auto event = vpi_event_signals.find(*object);
                   event != vpi_event_signals.end()) {
            watch_signal(event->second);
        } else if (const auto word = vpi_word_handles.find(*object);
                   word != vpi_word_handles.end()) {
            const auto container = word->second.first;
            if (const auto category = vpi_container_categories.find(container);
                category != vpi_container_categories.end()
                && category->second
                    == runtime::SystemVerilogVpiValueCategory::String) {
                // A string element of an unpacked array is polled like a
                // string variable (IEEE 1800-2023 38.36.1).
                watch_vpi_string(*object);
            } else if (!vpi_watched_containers.contains(container)) {
                // The v4 kernel publishes the memory's element changes to
                // the host object, whose change hook announces the words.
                interpreter->watch_container_object_changes(container);
                vpi_watched_containers.emplace(container,
                    vpi_container_value(container));
            }
        } else if (vpi_string_handles.contains(*object)) {
            // A string object has no change hook of its own: its text is
            // compared at each safe point while a watch exists.
            watch_vpi_string(*object);
        } else {
            vpi_watch_all_values = true;
        }
    } else {
        vpi_watch_all_values = true;
    }
    if (vpi_watch_all_values) {
        for (const auto& [container, words] : vpi_container_words) {
            (void)words;
            if (!vpi_watched_containers.contains(container)) {
                interpreter->watch_container_object_changes(container);
                vpi_watched_containers.emplace(container,
                    vpi_container_value(container));
            }
        }
    }
    refresh_observation_hooks();
}

std::optional<std::string> Simulation::Impl::vpi_string_text(
    const fsim_vpi_handle_v1 handle,
    std::map<runtime::simir::ContainerObjectId,
        runtime::simir::ContainerValue>& snapshots) const
{
    // The caller holds the bridge lock.
    if (const auto text = vpi_string_handles.find(handle);
        text != vpi_string_handles.end()) {
        return interpreter->string_object_value(text->second);
    }
    const auto word = vpi_word_handles.find(handle);
    if (word == vpi_word_handles.end()) {
        return std::nullopt;
    }
    const auto [container, ordinal] = word->second;
    auto snapshot = snapshots.find(container);
    if (snapshot == snapshots.end()) {
        snapshot = snapshots
                       .emplace(container,
                           interpreter->container_object_value_snapshot(
                               container))
                       .first;
    }
    if (ordinal >= snapshot->second.string_elements.size()) {
        return std::nullopt;
    }
    return snapshot->second.string_elements[ordinal];
}

void Simulation::Impl::watch_vpi_string(const fsim_vpi_handle_v1 handle)
{
    // The caller holds the bridge lock.
    std::map<runtime::simir::ContainerObjectId,
        runtime::simir::ContainerValue>
        snapshots;
    const auto text = vpi_string_text(handle, snapshots);
    if (!text) {
        return;
    }
    vpi_watched_strings.try_emplace(handle, *text);
    if (vpi_string_watch_observer != 0U) {
        return;
    }
    vpi_string_watch_observer = next_safe_point_observer++;
    safe_point_observers.emplace(vpi_string_watch_observer,
        [this](runtime::Scheduler&, runtime::SchedulerPhase) {
            publish_vpi_strings();
        });
}

void Simulation::Impl::publish_vpi_strings()
{
    if (!vpi_runtime_updates_enabled || vpi_watched_strings.empty()) {
        return;
    }
    std::scoped_lock bridge_lock { vpi_bridge_mutex };
    std::map<runtime::simir::ContainerObjectId,
        runtime::simir::ContainerValue>
        snapshots;
    for (auto& [handle, announced] : vpi_watched_strings) {
        const auto current = vpi_string_text(handle, snapshots);
        if (!current || *current == announced) {
            continue;
        }
        // A string whose length changed announces its size first, before
        // its value (IEEE 1800-2023 37.17 detail 14).
        const auto before = announced.size();
        announced = *current;
        if (vpi_size_change_hook && before != current->size()) {
            vpi_size_change_hook(handle, before, current->size());
        }
        runtime::SystemVerilogVpiStoredValue value;
        value.payload = *current;
        require_vpi_value(
            vpi_registry->notify_live_value(handle, std::move(value)),
            "string-variable publication");
    }
}

void Simulation::Impl::publish_vpi_assertion(
    const ConcurrentAssertionEvent& event,
    const std::optional<runtime::simir::ProcessId> design_process)
{
    if (!vpi_runtime_updates_enabled || !vpi_assertions) {
        return;
    }
    const auto object = [&]() -> std::optional<fsim_vpi_handle_v1> {
        if (design_process) {
            if (const auto found = vpi_process_assertions.find(*design_process);
                found != vpi_process_assertions.end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }();
    if (!object) {
        return;
    }
    runtime::SystemVerilogVpiAssertionEvent published;
    published.kind
        = event.kind == ConcurrentAssertionCoverageKind::assumption
        ? runtime::SystemVerilogVpiAssertionKind::Assumption
        : event.kind == ConcurrentAssertionCoverageKind::cover
        ? runtime::SystemVerilogVpiAssertionKind::Cover
        : event.kind == ConcurrentAssertionCoverageKind::restriction
        ? runtime::SystemVerilogVpiAssertionKind::Restriction
        : runtime::SystemVerilogVpiAssertionKind::Assertion;
    published.outcome
        = event.outcome == ConcurrentAssertionOutcome::pass
        ? runtime::SystemVerilogVpiAssertionOutcome::Success
        : event.outcome == ConcurrentAssertionOutcome::failure
        ? runtime::SystemVerilogVpiAssertionOutcome::Failure
        : event.outcome == ConcurrentAssertionOutcome::disabled
        ? runtime::SystemVerilogVpiAssertionOutcome::Disabled
        : event.outcome == ConcurrentAssertionOutcome::vacuous
        ? runtime::SystemVerilogVpiAssertionOutcome::Vacuous
        : runtime::SystemVerilogVpiAssertionOutcome::Aborted;
    // Without an assertion callback the outcome only counts (vpiAssertion
    // statistics); its text is copied for a callback alone.
    if (!vpi_callbacks || !vpi_callbacks->has_assertion_registrations()) {
        const auto error = vpi_assertions->count(
            *object, published.kind, published.outcome);
        if (error != runtime::SystemVerilogVpiAssertionApiError::None) {
            throw std::logic_error {
                "live VPI assertion statistics failed with error "
                + std::to_string(static_cast<unsigned>(error))
            };
        }
        return;
    }
    published.name = event.name;
    published.process = event.process;
    published.instance_identity = event.instance_identity;
    published.slot = event.slot;
    published.source_span = event.source_span;
    published.action_suppressed = event.action_suppressed;
    published.attempt_start = event.attempt_start;
    published.fail_line = event.fail_line;
    published.fail_column = event.fail_column;
    const auto error = vpi_assertions->observe(
        *object, std::move(published));
    if (error != runtime::SystemVerilogVpiAssertionApiError::None) {
        throw std::logic_error {
            "live VPI assertion callback dispatch failed with error "
            + std::to_string(static_cast<unsigned>(error))
        };
    }
}

void Simulation::Impl::publish_assertion_attempt_start(
    const runtime::simir::ProcessId design_process, const std::int64_t start)
{
    if (!assertion_attempt_hook || !vpi_runtime_updates_enabled) {
        return;
    }
    const auto object = vpi_process_assertions.find(design_process);
    if (object == vpi_process_assertions.end()) {
        return;
    }
    // The callback may read values of the design.
    interpreter->prepare_output_callback_observation();
    assertion_attempt_hook(
        object->second, static_cast<runtime::SimulationTick>(start));
}

void Simulation::Impl::publish_assertion_disabled(
    const runtime::simir::ProcessId design_process, const std::string_view kind,
    const std::string_view name, const std::int64_t start)
{
    if (!vpi_assertions || !vpi_callbacks
        || !vpi_callbacks->has_registrations()) {
        return;
    }
    const auto occurrence = std::ranges::find_if(built.design_ir.processes(),
        [&](const auto& item) { return item.runtime_index == design_process; });
    if (occurrence == built.design_ir.processes().end()) {
        return;
    }
    ConcurrentAssertionEvent event;
    event.name = std::string { name };
    event.process = occurrence->name;
    event.kind = kind == "assumption"
        ? ConcurrentAssertionCoverageKind::assumption
        : kind == "cover"
        ? ConcurrentAssertionCoverageKind::cover
        : kind == "restriction"
        ? ConcurrentAssertionCoverageKind::restriction
        : ConcurrentAssertionCoverageKind::assertion;
    event.outcome = ConcurrentAssertionOutcome::disabled;
    event.time = interpreter->scheduler().now();
    event.delta = interpreter->scheduler().delta();
    event.attempt_start = static_cast<runtime::SimulationTick>(start);
    // The callback may read values of the design.
    interpreter->prepare_output_callback_observation();
    publish_vpi_assertion(event, design_process);
}

void Simulation::Impl::publish_assertion_step(
    const runtime::simir::ProcessId design_process,
    const runtime::sva::StepRecord& step)
{
    if (!assertion_step_hook || !vpi_runtime_updates_enabled) {
        return;
    }
    const auto object = vpi_process_assertions.find(design_process);
    if (object == vpi_process_assertions.end()) {
        return;
    }
    // The callback may read values of the design.
    interpreter->prepare_output_callback_observation();
    assertion_step_hook(object->second, step);
}

void Simulation::Impl::start_vpi(const bool restoring)
{
    if (vpi_started) {
        return;
    }
    const auto sealed = vpi_systems->seal_registrations();
    if (sealed != runtime::SystemVerilogVpiSystemError::None) {
        throw std::logic_error { "failed to seal VPI system registrations" };
    }
    // A restart (IEEE 1800-2023 D.9) is not a start of simulation: the model
    // is restored past it, and the only callbacks that exist are
    // cbStartOfRestart and cbEndOfRestart (38.36.3). So nothing registered for
    // cbStartOfSimulation hears this one, the host services' own
    // registrations (the VHPI bridge) included; each service sets its phase
    // from the restart events instead (IEEE 1076-2019 20.8 NOTE 2).
    // cbStartOfSimulation is the beginning of the time-zero cycle (38.36.3):
    // the engine calls this once static initialization (package variable
    // initializers included, 6.21, 10.5) is complete, so what a callback
    // schedules or puts runs in that cycle.
    if (!restoring) {
        interpreter->set_start_of_simulation_hook([this] {
            const auto callback = vpi_callbacks->dispatch_lifecycle_now(
                runtime::SystemVerilogVpiCallbackKind::StartOfSimulation);
            if (callback != runtime::SystemVerilogVpiCallbackError::None) {
                throw std::logic_error {
                    "failed to dispatch VPI start callbacks"
                };
            }
            // The callbacks may have registered observers.
            refresh_observation_hooks();
        });
    }
    vpi_started = true;
}

bool Simulation::Impl::perform_reset()
{
    const auto request = reset_requests.front();
    reset_requests.erase(reset_requests.begin());
    // IEEE 1800-2023 38.36.3 and D.8, IEEE 1076-2019 20.9, in this order:
    //  1. cbStartOfReset / vhpiCbStartOfReset run first, while the old run is
    //     intact ("before the simulation time has been reset to 0"). The VHPI
    //     hook then removes every callback but vhpiCbEndOfReset (20.9 b).
    //  2. The DPI hook, registered last, disables the DPI chains in flight and
    //     drops the user data tables (D.8 a), so a library frees its pointers
    //     in the start callback above and finds them cleared afterwards.
    for (std::size_t index = 0U; index < lifecycle_hooks.size(); ++index) {
        lifecycle_hooks[index](Simulation::LifecycleEvent::start_of_reset, {});
    }
    interpreter->scheduler().clear_stop();
    //  3. The engine returns to time zero with the initial values and every
    //     scheduled event cancelled (D.8 b; 20.9 c, d); the application's own
    //     state goes with it.
    // A design with the UVM library has package variables that hold objects
    // (and a twin that cannot allocate them): the application's class heap,
    // statics and UVM services are cleared first, and the twin's package
    // initializers run through this run's class hooks, so the objects they
    // make are the time-zero objects of the run (IEEE 1800-2023 D.8, 6.21).
    const bool class_state_rebuilt = uvm_state_present();
    if (class_state_rebuilt) {
        class_heap.clear();
        class_owner_parameter_values.clear();
        class_static_store.reset_values();
        reset_uvm_state();
        interpreter->set_reset_twin_class_hooks(true);
    }
    const bool engine_reset = interpreter->reset_run_state();
    interpreter->set_reset_twin_class_hooks(false);
    if (!engine_reset) {
        throw std::runtime_error {
            interpreter->reset_failure().empty()
                ? std::string { "FSIM-SAVE-0018: the engine cannot reset the "
                                "run" }
                : interpreter->reset_failure()
        };
    }
    reset_application_state(class_state_rebuilt);
    //  4. $reset_count and $reset_value are the only state that survives (D.8).
    ++reset_count;
    reset_value = request.reset_value;
    interpreter->set_reset_values(
        static_cast<std::int32_t>(reset_count), reset_value);
    //  5. cbEndOfReset / vhpiCbEndOfReset run last: after the reset to time 0
    //     and the initial values, before the first procedural statement
    //     (38.36.3; 20.9 e; no callback fires between 3 and 5, 21.3.8.4 NOTE 2).
    for (std::size_t index = 0U; index < lifecycle_hooks.size(); ++index) {
        lifecycle_hooks[index](Simulation::LifecycleEvent::end_of_reset, {});
    }
    return request.stop_value != 0;
}

void Simulation::Impl::reset_application_state(const bool class_state_rebuilt)
{
    // Class objects and their static members are variables (D.8: all
    // variables contain their initial values); a delayed vpi_put_value is a
    // scheduled event, which the engine's reset cancelled.
    if (!class_state_rebuilt) {
        class_heap.clear();
        class_owner_parameter_values.clear();
        class_static_store.reset_values();
    }
    class_static_store.initialize_all();
    // The UVM hierarchy, phases, objections, configuration, factory overrides
    // and report counts are variables of the host services (D.8); they were
    // cleared before the twin ran, and the testbench builds them again from
    // time zero (#188).
    if (class_state_rebuilt) {
        reapply_uvm_command_line();
    }
    if (vpi_values) {
        vpi_values->reset_records();
    }
}

void Simulation::Impl::end_vpi()
{
    if (!vpi_started || vpi_ended) {
        return;
    }
    // The class objects still alive are reclaimed before the library hears
    // of the end (IEEE 1800-2023 38.36.1 cbReclaimObj).
    if (class_reclaim_hook) {
        class_reclaim_hook();
    }
    const auto callback = vpi_callbacks->dispatch_lifecycle_now(
        runtime::SystemVerilogVpiCallbackKind::EndOfSimulation);
    if (callback != runtime::SystemVerilogVpiCallbackError::None) {
        throw std::logic_error { "failed to dispatch VPI end callbacks" };
    }
    vpi_control->mark_finished();
    vpi_ended = true;
}

void Simulation::Impl::start_systemc()
{
    if (systemc_start_attempted) {
        return;
    }
    systemc_start_attempted = true;
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
        registry.start_simulation(roots);
    });
}

void Simulation::Impl::end_systemc(
    const std::optional<runtime::SimulationTick> current_time)
{
    if (!systemc_start_attempted || systemc_ended) {
        return;
    }
    for_each_systemc_registry([&](auto& registry, const auto& roots) {
        registry.end_simulation(
            roots,
            current_time.value_or(interpreter->scheduler().now()));
    });
    systemc_ended = true;
}
} // namespace fsim::app
