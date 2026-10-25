// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"
#include "application_phase_profile.hpp"
#include "../elaboration/elaborated_design_process_access.hpp"
#include "../runtime/simir_static_kernel.hpp"
#if defined(FSIM_HAS_LLVM)
#include "application_design_artifact_codec_internal.hpp"
#include "fsim/compiler/object_cache.hpp"
#include "fsim/compiler/static_kernel_codegen.hpp"
#endif

#include <algorithm>
#include <exception>
#include <charconv>
#include <limits>

namespace fsim::app {

namespace {

// A package object's runtime storage index as its storage id type, when it
// fits.
template <typename Id, typename Index>
[[nodiscard]] std::optional<Id> narrow_runtime_index(
    const std::optional<Index> index)
{
    if (!index || *index > std::numeric_limits<Id>::max()) {
        return std::nullopt;
    }
    return static_cast<Id>(*index);
}

using PackageClassHandleInitializer = std::pair<
    runtime::simir::SignalId, semantic::ExpressionId>;

// A class-handle container property as a container of 64-bit handle values
// of `type` (IEEE 1800-2017 7.4, 7.8, 7.10): sequential elements in order,
// associative elements in key order.
[[nodiscard]] runtime::simir::ContainerValue handle_container_value(
    const runtime::SystemVerilogClassHandleContainer& handles,
    const runtime::simir::ContainerType& type)
{
    using Kind = runtime::SystemVerilogClassContainerKind;
    if (handles.kind() == Kind::UnpackedAggregate
        || type.element_kind != runtime::simir::ContainerElementKind::Packed) {
        throw std::invalid_argument {
            "class handle container has no value-container form"
        };
    }
    auto value = runtime::simir::default_container_value(type);
    value.elements.clear();
    value.keys.clear();
    value.string_keys.clear();
    const auto element = [&](const runtime::SystemVerilogClassHandle handle) {
        return runtime::PackedLogic4::from_aval_bval(
            type.element_width, handle, 0);
    };
    if (handles.kind() != Kind::AssociativeArray) {
        for (const auto handle : handles.sequential_values()) {
            value.elements.push_back(element(handle));
        }
        return value;
    }
    std::vector<std::pair<runtime::PackedLogic4,
        runtime::SystemVerilogClassHandle>> integral;
    for (const auto& [key, handle] : handles.keyed_values()) {
        if (type.string_indices) {
            value.string_keys.push_back(key);
            value.elements.push_back(element(handle));
            continue;
        }
        std::uint64_t number { };
        const auto parsed = std::from_chars(
            key.data(), key.data() + key.size(), number);
        if (parsed.ec != std::errc { }
            || parsed.ptr != key.data() + key.size()) {
            throw std::invalid_argument {
                "class handle associative key is not integral"
            };
        }
        integral.emplace_back(runtime::PackedLogic4::from_aval_bval(
                                  type.index_width, number, 0),
            handle);
    }
    std::ranges::sort(integral, [&](const auto& left, const auto& right) {
        return runtime::simir::associative_index_key_less(
            type, left.first, right.first);
    });
    for (auto& [key, handle] : integral) {
        value.keys.push_back(std::move(key));
        value.elements.push_back(element(handle));
    }
    return value;
}

// Replace the handles of a class-handle container property with `value`.
void assign_handle_container(runtime::SystemVerilogClassHeap& heap,
    runtime::SystemVerilogClassHandleContainer& handles,
    const runtime::simir::ContainerValue& value)
{
    using Kind = runtime::SystemVerilogClassContainerKind;
    const auto handle = [&](const std::size_t index) {
        const auto word = value.elements.at(index).low_word();
        if (word.bval != 0) {
            throw std::invalid_argument {
                "class handle container element contains X or Z"
            };
        }
        return static_cast<runtime::SystemVerilogClassHandle>(word.aval);
    };
    if (handles.kind() == Kind::UnpackedAggregate) {
        throw std::invalid_argument {
            "class handle container has no value-container form"
        };
    }
    if (handles.kind() != Kind::AssociativeArray) {
        if (handles.kind() != Kind::FixedArray) {
            handles.resize(value.elements.size());
        }
        for (std::size_t index { };
             index < std::min(value.elements.size(), handles.size());
             ++index) {
            handles.set(heap, index, handle(index));
        }
        return;
    }
    std::vector<std::string> stale;
    for (const auto& [key, _] : handles.keyed_values()) {
        stale.push_back(key);
    }
    for (const auto& key : stale) {
        (void)handles.erase(key);
    }
    for (std::size_t index { }; index < value.elements.size(); ++index) {
        if (value.type.string_indices) {
            handles.set(heap, value.string_keys.at(index), handle(index));
            continue;
        }
        const auto word = value.keys.at(index).low_word();
        handles.set(heap, std::to_string(word.aval), handle(index));
    }
}

[[nodiscard]] std::vector<elaboration::SystemVerilogPackageClassInitializerInfo>
copy_package_class_startup_initializers(const BuiltProject& built)
{
    const auto rows = built.design.systemverilog_package_class_initializers();
    return { rows.begin(), rows.end() };
}

[[nodiscard]] std::vector<
    elaboration::SystemVerilogPackageClassStaticInitializerInfo>
copy_package_static_class_startup_initializers(const BuiltProject& built)
{
    const auto rows
        = built.design.systemverilog_package_class_static_initializers();
    return { rows.begin(), rows.end() };
}

[[nodiscard]] std::vector<runtime::simir::SignalId>
collect_package_static_startup_signal_ids(const BuiltProject& built)
{
    std::vector<runtime::simir::SignalId> result;
    for (const auto& process : built.design.processes()) {
        if (process.startup_phase
            != runtime::simir::ProcessStartupPhase::package_static_initializer) {
            continue;
        }
        for (const auto& region : process.driver_regions) {
            result.push_back(region.signal);
        }
    }
    const auto append_writable_signals = [&result](const auto& rows) {
        for (const auto& row : rows) {
            result.insert(result.end(), row.writable_signal_ids.begin(),
                row.writable_signal_ids.end());
        }
    };
    append_writable_signals(
        built.design.systemverilog_package_class_initializers());
    append_writable_signals(
        built.design.systemverilog_package_class_static_initializers());
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

[[nodiscard]] std::vector<PackageClassHandleInitializer>
compiled_systemverilog_package_class_handle_initializers(
    const BuiltProject& built)
{
    std::vector<PackageClassHandleInitializer> result;
    std::set<std::uint32_t> seen_declarations;
    for (const auto& unit : built.systemverilog_hir.units()) {
        if (unit.kind != semantic::sv::UnitKind::package) {
            continue;
        }
        for (const auto declaration_id : unit.declarations) {
            const auto* declaration
                = built.systemverilog_hir.find_declaration(declaration_id);
            if (declaration == nullptr
                || declaration->scope != unit.scope
                || declaration->form
                    != semantic::sv::DeclarationForm::variable
                || !declaration->initializer) {
                continue;
            }
            const auto initializer = std::ranges::find(
                built.systemverilog_hir.expressions(),
                *declaration->initializer, &semantic::sv::Expression::id);
            if (initializer == built.systemverilog_hir.expressions().end()
                || initializer->kind
                    != semantic::sv::ExpressionKind::class_allocation) {
                continue;
            }
            if (!declaration->declared_value) {
                throw std::logic_error {
                    "package class allocation has no declaration value identity"
                };
            }
            std::optional<runtime::simir::SignalId> signal;
            for (const auto& object : built.design_ir.objects()) {
                if (object.kind != semantic::design::ObjectKind::signal
                    || object.declaration_value != declaration->declared_value) {
                    continue;
                }
                if (object.runtime_index
                        > std::numeric_limits<runtime::simir::SignalId>::max()
                    || object.runtime_index >= built.design.signals().size()) {
                    throw std::logic_error {
                        "package class allocation maps to an invalid runtime signal"
                    };
                }
                const auto candidate
                    = static_cast<runtime::simir::SignalId>(
                        object.runtime_index);
                if (signal && *signal != candidate) {
                    throw std::logic_error {
                        "package class allocation has multiple runtime signals"
                    };
                }
                signal = candidate;
            }
            if (!signal) {
                throw std::logic_error {
                    "package class allocation has no exact runtime signal binding"
                };
            }
            if (seen_declarations.insert(declaration_id.value()).second) {
                result.push_back({ *signal, *declaration->initializer });
            }
        }
    }
    return result;
}

[[nodiscard]] const semantic::sv::Unit* package_unit_for_class(
    const BuiltProject& built,
    const semantic::sv::ClassSpecialization& specialization)
{
    const auto scope = std::ranges::find(
        built.semantics.scopes(), specialization.declaration_scope,
        &semantic::Scope::id);
    if (scope == built.semantics.scopes().end()) {
        throw std::logic_error {
            "compiled class initializer has no exact semantic scope"
        };
    }
    const auto unit = std::ranges::find(
        built.systemverilog_hir.units(), scope->unit,
        &semantic::sv::Unit::id);
    return unit == built.systemverilog_hir.units().end() ? nullptr : &*unit;
}

void validate_package_class_startup_initializers(
    const BuiltProject& built,
    const std::vector<PackageClassHandleInitializer>& expected_handles,
    const std::vector<elaboration::SystemVerilogPackageClassInitializerInfo>&
        handle_rows,
    const std::vector<
        elaboration::SystemVerilogPackageClassStaticInitializerInfo>&
        static_rows)
{
    if (expected_handles.size() != handle_rows.size()) {
        throw std::logic_error {
            "package class-handle startup metadata does not cover every HIR initializer"
        };
    }
    std::set<std::uint32_t> seen_handle_declarations;
    for (const auto& row : handle_rows) {
        if (!row.unit.valid() || !row.declaration.valid()
            || !row.initializer.valid()
            || !seen_handle_declarations.insert(row.declaration.value()).second) {
            throw std::logic_error {
                "package class-handle startup metadata has an invalid identity"
            };
        }
        const auto unit = std::ranges::find(
            built.systemverilog_hir.units(), row.unit,
            &semantic::sv::Unit::id);
        const auto* declaration
            = built.systemverilog_hir.find_declaration(row.declaration);
        const auto initializer = std::ranges::find(
            built.systemverilog_hir.expressions(), row.initializer,
            &semantic::sv::Expression::id);
        if (unit == built.systemverilog_hir.units().end()
            || unit->kind != semantic::sv::UnitKind::package
            || declaration == nullptr
            || declaration->scope != unit->scope
            || declaration->form != semantic::sv::DeclarationForm::variable
            || declaration->initializer != row.initializer
            || !declaration->declared_value
            || initializer == built.systemverilog_hir.expressions().end()
            || initializer->kind
                != semantic::sv::ExpressionKind::class_allocation) {
            throw std::logic_error {
                "package class-handle startup metadata does not match its HIR declaration"
            };
        }
        if (std::ranges::count_if(expected_handles,
                [&](const auto& candidate) {
                    return candidate.first == row.signal
                        && candidate.second == row.initializer;
                }) != 1) {
            throw std::logic_error {
                "package class-handle startup metadata has no unique runtime binding"
            };
        }
    }

    const auto property_row = [&](const std::string_view specialization,
                                  const semantic::DeclarationId declaration) {
        return std::ranges::find_if(static_rows, [&](const auto& row) {
            return row.specialization_identity == specialization
                && row.property_declaration == declaration;
        });
    };
    std::set<std::pair<std::string, std::uint32_t>> seen_static_properties;
    for (const auto& row : static_rows) {
        if (!row.unit.valid() || row.class_identity.empty()
            || row.specialization_identity.empty()
            || !row.property_declaration.valid() || row.property_name.empty()
            || row.canonical_identity.empty() || !row.initializer.valid()
            || !seen_static_properties.emplace(row.specialization_identity,
                   row.property_declaration.value()).second) {
            throw std::logic_error {
                "package static-property startup metadata has an invalid identity"
            };
        }
        const auto specialization = std::ranges::find(
            built.compiled_systemverilog_class_specializations,
            row.specialization_identity,
            &semantic::sv::ClassSpecialization::specialization_identity);
        if (specialization
                == built.compiled_systemverilog_class_specializations.end()
            || specialization->declaration_identity != row.class_identity) {
            throw std::logic_error {
                "package static-property startup metadata has no exact class specialization"
            };
        }
        const auto* unit = package_unit_for_class(built, *specialization);
        if (unit == nullptr || unit->kind != semantic::sv::UnitKind::package
            || unit->id != row.unit) {
            throw std::logic_error {
                "package static-property startup metadata has the wrong owning package"
            };
        }
        const auto property = std::ranges::find(
            specialization->properties, row.property_declaration,
            &semantic::sv::SpecializedClassProperty::declaration);
        const auto initializer = std::ranges::find(
            built.systemverilog_hir.expressions(), row.initializer,
            &semantic::sv::Expression::id);
        if (property == specialization->properties.end()
            || !property->static_storage || property->name != row.property_name
            || property->canonical_identity != row.canonical_identity
            || property->initializer != row.initializer
            || initializer == built.systemverilog_hir.expressions().end()
            || initializer->kind
                != semantic::sv::ExpressionKind::class_allocation) {
            throw std::logic_error {
                "package static-property startup metadata does not match its HIR initializer"
            };
        }
    }

    for (const auto& specialization :
        built.compiled_systemverilog_class_specializations) {
        const auto* unit = package_unit_for_class(built, specialization);
        if (unit == nullptr || unit->kind != semantic::sv::UnitKind::package) {
            continue;
        }
        for (const auto& property : specialization.properties) {
            if (!property.static_storage || !property.initializer) {
                continue;
            }
            const auto initializer = std::ranges::find(
                built.systemverilog_hir.expressions(), *property.initializer,
                &semantic::sv::Expression::id);
            if (initializer == built.systemverilog_hir.expressions().end()
                || initializer->kind
                    != semantic::sv::ExpressionKind::class_allocation) {
                continue;
            }
            const auto row = property_row(
                specialization.specialization_identity, property.declaration);
            if (row == static_rows.end() || row->unit != unit->id
                || row->initializer != *property.initializer
                || row->class_identity != specialization.declaration_identity
                || row->property_name != property.name
                || row->canonical_identity != property.canonical_identity) {
                throw std::logic_error {
                    "package static class allocation is missing exact startup metadata"
                };
            }
        }
    }
}

#if defined(FSIM_HAS_LLVM)
/// The static-kernel plan persisted for a loaded artifact (elaborate --aot
/// stores it; simulate restores it), keyed by the artifact's content and
/// the planner.
struct PersistedPlanSlot {
    std::optional<compiler::ObjectCache> cache;
    std::string key;
};

PersistedPlanSlot persisted_plan_slot(const BuiltProject& built,
    const std::string_view kind = "plan")
{
    PersistedPlanSlot slot;
    // FSIM_STATIC_KERNEL_PLAN_CACHE=0 plans every run.
    const char* enabled = std::getenv("FSIM_STATIC_KERNEL_PLAN_CACHE");
    // FSIM_STATIC_KERNEL_PLAN_CACHE=<kind> skips just that kind.
    if (built.cache_path.empty() || built.artifact_identity.empty()
        || (enabled != nullptr
            && (std::string_view { enabled } == "0"
                || std::string_view { enabled } == kind))) {
        return slot;
    }
    slot.cache.emplace(built.cache_path / "static-kernel");
    compiler::CacheKeyBuilder key;
    key.add("planner", elaboration::detail::static_kernel_plan_identity());
    if (kind == "specializations") {
        key.add("specializer", runtime::simir::static_kernel_specializer_identity());
    } else if (kind == "bodies") {
        key.add("compiler", runtime::simir::static_kernel_compiler_identity());
    } else if (kind == "native") {
        key.add("canonical", runtime::simir::static_kernel_canonical_identity());
    }
    key.add("kind", kind);
    key.add("design", built.artifact_identity);
    slot.key = key.finish();
    return slot;
}

/// Persisted VHDL specializations (see StaticKernelSpecializations).
[[nodiscard]] std::string serialize_specializations(
    const runtime::simir::StaticKernelSpecializations& specializations)
{
    codec_detail::Writer writer;
    writer.write(static_cast<std::uint64_t>(specializations.members.size()));
    for (const auto& entry : specializations.members) {
        writer.write(entry.process);
        writer.write(entry.original_size);
        writer.write(entry.specialized);
        writer.write(entry.operations);
        writer.write(entry.origin);
    }
    if (!writer.complete()) {
        return { };
    }
    return std::move(writer).finish();
}

[[nodiscard]] std::shared_ptr<runtime::simir::StaticKernelSpecializations>
restore_specializations(const std::string_view bytes)
{
    codec_detail::Reader reader { bytes };
    std::uint64_t count { };
    if (!reader.read(count) || count > bytes.size()) {
        return nullptr;
    }
    auto result = std::make_shared<runtime::simir::StaticKernelSpecializations>();
    result->members.resize(static_cast<std::size_t>(count));
    for (auto& entry : result->members) {
        if (!reader.read(entry.process) || !reader.read(entry.original_size)
            || !reader.read(entry.specialized) || !reader.read(entry.operations)
            || !reader.read(entry.origin)
            || entry.origin.size() != entry.operations.size()) {
            return nullptr;
        }
    }
    if (reader.remaining() != 0U) {
        return nullptr;
    }
    return result;
}
#endif

} // namespace

bool application_detail::static_kernel_requested(
    const SimulationEngine engine) noexcept
{
    if (engine == SimulationEngine::v4) {
        return true;
    }
    const char* requested = std::getenv("FSIM_STATIC_KERNEL");
    return requested != nullptr && std::string_view { requested } == "1";
}

namespace {

std::unique_ptr<runtime::simir::Interpreter> create_simulation_interpreter(
    BuiltProject& built, const std::uint64_t max_deltas,
    const SimulationEngine engine)
{
    const application_detail::ScopedPhaseProfile profile {
        "create_simulation_interpreter"
    };
    const runtime::SchedulerOptions options { max_deltas, 32 };
    // Engine v4 static kernel (docs/simulation-engine-v4-plan.md):
    // `--engine v4` or FSIM_STATIC_KERNEL=1, and always in an AOT
    // elaboration, which builds the kernel's code. It has the same
    // observability limits as cone fusion.
    bool kernel = application_detail::static_kernel_requested(engine);
#if defined(FSIM_HAS_LLVM)
    kernel = kernel || compiler::StaticKernelAheadOfTimeScope::active();
#endif
    if (built.cone_fusion && kernel) {
        auto plan = [&] {
            const application_detail::ScopedPhaseProfile planning {
                "static_kernel_plan"
            };
#if defined(FSIM_HAS_LLVM)
            const auto slot = persisted_plan_slot(built);
            const bool ahead_of_time
                = compiler::StaticKernelAheadOfTimeScope::active();
            if (slot.cache && !ahead_of_time) {
                std::error_code error;
                if (const auto bytes = slot.cache->load(slot.key, error)) {
                    if (auto restored = elaboration::detail::restore_static_kernel_plan(
                            built.design,
                            std::string_view { reinterpret_cast<const char*>(
                                                   bytes->data()),
                                bytes->size() })) {
                        return std::move(*restored);
                    }
                }
            }
            auto planned = built.design.plan_static_kernel();
            if (slot.cache && ahead_of_time) {
                const auto bytes
                    = elaboration::detail::serialize_static_kernel_plan(planned);
                if (!bytes.empty()) {
                    std::error_code error;
                    static_cast<void>(slot.cache->store(slot.key,
                        std::as_bytes(std::span { bytes.data(), bytes.size() }),
                        error));
                }
            }
            return planned;
#else
            return built.design.plan_static_kernel();
#endif
        }();
        if (std::getenv("FSIM_PROFILE_PHASES") != nullptr) {
            std::cerr << "fsim-profile: static-kernel disabled=" << plan.disabled
                      << " reason=" << plan.disabled_reason
                      << " members=" << plan.members
                      << " owned_signals=" << plan.owned_signals
                      << " outputs=" << plan.boundary_outputs
                      << " inputs=" << plan.boundary_inputs
                      << " containers=" << plan.owned_containers
                      << " processes=" << plan.processes
                      << " host=" << plan.host_processes
                      << '\n';
            for (const auto& row : plan.host_reasons) {
                std::cerr << "fsim-profile: static-kernel-host reason="
                          << row.reason << " count=" << row.count
                          << " example=" << row.example << '\n';
            }
        }
        if (!plan.disabled) {
            // Partition combinational members by module instance.
            const auto& design_ir = built.design_ir;
            std::vector<std::uint32_t> instance_of;
            for (const auto& occurrence : design_ir.processes()) {
                const auto specialization = occurrence.specialization.value();
                if (specialization >= design_ir.specializations().size()) {
                    continue;
                }
                if (occurrence.runtime_index >= instance_of.size()) {
                    instance_of.resize(occurrence.runtime_index + 1U,
                        std::numeric_limits<std::uint32_t>::max());
                }
                instance_of[occurrence.runtime_index] = static_cast<std::uint32_t>(
                    design_ir.specializations()[specialization].instance.value());
            }
            for (auto& member : plan.spec->members) {
                if (member.process < instance_of.size()) {
                    member.partition = instance_of[member.process];
                }
            }
#if defined(FSIM_HAS_LLVM)
            if (const char* native = std::getenv("FSIM_STATIC_KERNEL_NATIVE");
                native == nullptr || std::string_view { native } != "0") {
                plan.spec->codegen = compiler::make_static_kernel_codegen({
                    built.cache_path.empty()
                        ? std::filesystem::path { }
                        : built.cache_path / "static-kernel",
                    compiler::StaticKernelAheadOfTimeScope::active(),
                });
            }
            // VHDL specializations: recorded ahead of time, restored after.
            const auto specializations = persisted_plan_slot(built, "specializations");
            std::shared_ptr<runtime::simir::StaticKernelSpecializations> recorded;
            if (specializations.cache) {
                if (compiler::StaticKernelAheadOfTimeScope::active()) {
                    recorded = std::make_shared<
                        runtime::simir::StaticKernelSpecializations>();
                    plan.spec->record_specializations = recorded;
                } else {
                    std::error_code error;
                    if (const auto bytes = specializations.cache->load(
                            specializations.key, error)) {
                        plan.spec->specializations = restore_specializations(
                            std::string_view { reinterpret_cast<const char*>(
                                                   bytes->data()),
                                bytes->size() });
                    }
                }
            }
            // Compiled member bodies, likewise.
            const auto bodies = persisted_plan_slot(built, "bodies");
            std::shared_ptr<std::string> recorded_bodies;
            if (bodies.cache) {
                if (compiler::StaticKernelAheadOfTimeScope::active()) {
                    recorded_bodies = std::make_shared<std::string>();
                    plan.spec->record_compiled_bodies = recorded_bodies;
                } else {
                    std::error_code error;
                    if (const auto bytes = bodies.cache->load(bodies.key, error)) {
                        plan.spec->compiled_bodies = std::make_shared<const std::string>(
                            reinterpret_cast<const char*>(bytes->data()), bytes->size());
                    }
                }
            }
            // Canonical templates and bindings, likewise.
            const auto native_image = persisted_plan_slot(built, "native");
            std::shared_ptr<std::string> recorded_native;
            if (native_image.cache) {
                if (compiler::StaticKernelAheadOfTimeScope::active()) {
                    recorded_native = std::make_shared<std::string>();
                    plan.spec->record_native_image = recorded_native;
                } else {
                    std::error_code error;
                    if (const auto bytes = native_image.cache->load(
                            native_image.key, error)) {
                        plan.spec->native_image = std::make_shared<const std::string>(
                            reinterpret_cast<const char*>(bytes->data()), bytes->size());
                    }
                }
            }
            auto interpreter = std::move(built.design).create_interpreter(
                options, built.seed, plan);
            if (recorded_native && !recorded_native->empty()) {
                std::error_code error;
                static_cast<void>(native_image.cache->store(native_image.key,
                    std::as_bytes(std::span { recorded_native->data(),
                        recorded_native->size() }),
                    error));
            }
            if (recorded_bodies && !recorded_bodies->empty()) {
                std::error_code error;
                static_cast<void>(bodies.cache->store(bodies.key,
                    std::as_bytes(std::span { recorded_bodies->data(),
                        recorded_bodies->size() }),
                    error));
            }
            if (recorded) {
                const auto bytes = serialize_specializations(*recorded);
                if (!bytes.empty()) {
                    std::error_code error;
                    static_cast<void>(specializations.cache->store(specializations.key,
                        std::as_bytes(std::span { bytes.data(), bytes.size() }), error));
                }
            }
            return interpreter;
#else
            return std::move(built.design).create_interpreter(
                options, built.seed, plan);
#endif
        }
    }
    if (built.cone_fusion
        && std::getenv("FSIM_DISABLE_CONE_FUSION") == nullptr) {
        const auto plan = built.design.plan_cone_fusion();
        if (std::getenv("FSIM_PROFILE_PHASES") != nullptr) {
            std::cerr << "fsim-profile: cone-fusion disabled=" << plan.disabled
                      << " reason=" << plan.disabled_reason
                      << " candidates=" << plan.candidate_processes
                      << " cones=" << plan.cones.size()
                      << " dormant=" << plan.dormant.size()
                      << " internal_nets=" << plan.internal_nets << '\n';
        }
        if (!plan.disabled && !plan.cones.empty()) {
            return std::move(built.design).create_interpreter(
                options, built.seed, plan);
        }
    }
    return std::move(built.design).create_interpreter(options, built.seed);
}

// Restore an object's random number generator from a get_randstate
// string; a string of another form leaves the state unchanged (IEEE
// 1800-2017 18.13.5).
void restore_random_state(runtime::SystemVerilogClassObject& object,
    const std::string_view state)
{
    constexpr std::string_view prefix { "fsim-randstate:" };
    if (!state.starts_with(prefix)) {
        return;
    }
    auto rest = state.substr(prefix.size());
    const auto parse_number = [](const std::string_view text)
        -> std::optional<std::uint64_t> {
        std::uint64_t value { };
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc { }
            || parsed.ptr != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    };
    const auto seed_end = rest.find(';');
    if (seed_end == std::string_view::npos) {
        return;
    }
    const auto seed = parse_number(rest.substr(0, seed_end));
    if (!seed) {
        return;
    }
    rest.remove_prefix(seed_end + 1U);
    std::map<std::string, std::uint64_t, std::less<>> ordinals;
    while (!rest.empty()) {
        const auto colon = rest.find(':');
        if (colon == std::string_view::npos) {
            return;
        }
        const auto length = parse_number(rest.substr(0, colon));
        if (!length || *length > rest.size() - colon - 1U) {
            return;
        }
        const auto name = rest.substr(colon + 1U,
            static_cast<std::size_t>(*length));
        rest.remove_prefix(colon + 1U + static_cast<std::size_t>(*length));
        if (!rest.starts_with('=')) {
            return;
        }
        const auto end = rest.find(';');
        if (end == std::string_view::npos) {
            return;
        }
        const auto ordinal = parse_number(rest.substr(1U, end - 1U));
        if (!ordinal) {
            return;
        }
        ordinals.emplace(std::string { name }, *ordinal);
        rest.remove_prefix(end + 1U);
    }
    object.random_object_seed = *seed;
    object.random_call_ordinals = std::move(ordinals);
}

} // namespace

Simulation::Impl::Impl(
    BuiltProject project,
    const std::uint64_t max_deltas,
    const SimulationEngine engine,
    const SystemVerilogVpiRuntimeUpdates vpi_runtime_updates)
    : built(std::move(project))
    , package_class_handle_initializers(built.trusted_artifact
              ? std::vector<PackageClassHandleInitializer> { }
              : compiled_systemverilog_package_class_handle_initializers(
                  built))
    , package_class_startup_initializers(
          copy_package_class_startup_initializers(built))
    , package_static_class_startup_initializers(
          copy_package_static_class_startup_initializers(built))
    , package_static_startup_signal_ids(
          collect_package_static_startup_signal_ids(built))
    , vpi_runtime_updates_enabled(
          vpi_runtime_updates == SystemVerilogVpiRuntimeUpdates::enabled)
    , class_heap(
          { }, built.seed)
    , class_methods(class_heap, { }, &class_static_store)
    , uvm_objects(
          class_heap,
          [this](const std::string_view specialization,
              const std::string_view declared,
              const std::string_view) {
              return construct_class(
                  specialization,
                  declared,
                  std::span<const runtime::PackedLogic4> { },
                  std::span<const std::string> { },
                  std::span<const std::string> { },
                  "$uvm-clone");
          })
    , uvm_components(class_heap, uvm_objects)
    , uvm_activity()
    , uvm_phases(uvm_components)
    , uvm_objections(uvm_objects, uvm_components, uvm_phases)
    , uvm_tlm1(class_heap, uvm_components)
    , uvm_tlm2(uvm_components)
    , uvm_sequences(
          class_heap, uvm_objects, uvm_components, uvm_phases,
          uvm_objections)
    , uvm_callbacks(uvm_objects, uvm_components)
    , uvm_transactions(uvm_objects, uvm_components, uvm_callbacks)
    , uvm_register_model(uvm_components)
    , uvm_foreign(
          uvm_phases, uvm_objections, uvm_tlm1, uvm_tlm2, uvm_activity)
    , uvm_registry(
          class_heap,
          uvm_objects,
          uvm_components,
          [this](const std::string_view specialization,
              const std::string_view name) {
              const auto handle = construct_class(
                  specialization,
                  class_specialization(specialization).declaration_identity,
                  std::span<const runtime::PackedLogic4> { },
                  std::span<const std::string> { },
                  std::span<const std::string> { },
                  "$uvm-registry");
              uvm_objects.set_name(handle, std::string { name });
              return handle;
          },
          [this](const std::string_view specialization,
              const std::string_view name,
              const runtime::SystemVerilogClassHandle parent,
              const runtime::SystemVerilogUvmRootHandle root) {
              std::array actuals {
                  runtime::PackedLogic4(64),
                  runtime::PackedLogic4::from_aval_bval(64, parent, 0)
              };
              std::array string_actuals {
                  std::string { name }, std::string { }
              };
              return construct_class(
                  specialization,
                  class_specialization(specialization).declaration_identity,
                  actuals,
                  string_actuals,
                  std::span<const std::string> { },
                  "$uvm-registry",
                  root);
          })
    , uvm_factory(uvm_registry)
    , uvm_resources(&class_heap)
    , uvm_synchronization(uvm_objects)
    , uvm_config_db(uvm_resources)
    , uvm_command_line(uvm_factory, uvm_resources, uvm_config_db)
    , uvm_test_runner(
          uvm_objects, uvm_components, uvm_factory, uvm_command_line)
    , uvm_reports(uvm_objects, uvm_components)
    , class_execution(
          class_heap,
          uvm_components,
          uvm_phases,
          uvm_roots_by_scope)
    , class_hir_execution(
          built.compiled_systemverilog_class_specializations,
          built.systemverilog_hir,
          class_heap)
    , interpreter(create_simulation_interpreter(built, max_deltas, engine))
{
    if (interpreter) {
        interpreter->retain_state_until_exit();
    }
    // A trusted artifact's startup metadata is not re-derived from HIR;
    // FSIM_VERIFY_ARTIFACT=1 keeps the check.
    if (!built.trusted_artifact) {
        validate_package_class_startup_initializers(built,
            package_class_handle_initializers,
            package_class_startup_initializers,
            package_static_class_startup_initializers);
    }
    uvm_components.set_post_destroy_hook(
        [this](const runtime::SystemVerilogUvmComponentSnapshot& object) {
            class_owner_parameter_values.erase(object.object);
        });
    class_hir_execution.set_construct_service(
        [this](const std::string_view class_identity,
            const std::span<const runtime::PackedLogic4> actuals,
            const std::span<const std::string> string_actuals,
            const std::span<const std::string> actual_names,
            const runtime::SystemVerilogClassHandle owner_handle) {
            return construct_class(class_identity, class_identity, actuals,
                string_actuals, actual_names, "$class", 0U, std::nullopt,
                owner_handle);
        });
    class_hir_execution.set_owner_parameter_service(
        [this](const runtime::SystemVerilogClassHandle handle,
            const semantic::DeclarationId declaration) {
            application_detail::SystemVerilogClassHirExecution::
                OwnerParameterBinding result;
            // A class value parameter read inside a method takes the value
            // of the object's specialization or of the base specialization
            // that declares it (IEEE 1800-2017 8.25).
            if (class_heap.contains(handle)) {
                std::string identity
                    = class_heap.object(handle).specialization_identity;
                for (std::size_t depth = 0U;
                    depth < 64U && !identity.empty(); ++depth) {
                    const auto found = std::ranges::find(
                        built.compiled_systemverilog_class_specializations,
                        identity,
                        &semantic::sv::ClassSpecialization::
                            specialization_identity);
                    if (found
                        == built.compiled_systemverilog_class_specializations
                            .end()) {
                        break;
                    }
                    const auto parameter = std::ranges::find(
                        found->parameters, declaration,
                        &semantic::sv::SpecializedClassParameter::
                            declaration);
                            if (parameter != found->parameters.end()) {
                        constexpr std::string_view prefix {
                            "value{integer:" };
                        const std::string_view text {
                            parameter->canonical_identity };
                        if (!parameter->type_parameter
                            && text.starts_with(prefix)
                            && text.ends_with('}')) {
                            std::int64_t value { };
                            const auto digits = text.substr(prefix.size(),
                                text.size() - prefix.size() - 1U);
                            const auto parsed = std::from_chars(
                                digits.data(), digits.data() + digits.size(),
                                value);
                            if (parsed.ec == std::errc { }
                                && parsed.ptr
                                    == digits.data() + digits.size()) {
                                const bool narrow = value
                                        >= std::numeric_limits<
                                            std::int32_t>::min()
                                    && value
                                        <= std::numeric_limits<
                                            std::int32_t>::max();
                                result.context_available = true;
                                result.found = true;
                                result.value
                                    = runtime::PackedLogic4::from_aval_bval(
                                        narrow ? 32U : 64U,
                                        static_cast<std::uint64_t>(value),
                                        0U);
                                return result;
                            }
                        }
                        break;
                    }
                    identity = found->base
                        ? found->base->specialization_identity
                        : std::string { };
                }
            }
            const auto owner = class_owner_parameter_values.find(handle);
            if (owner == class_owner_parameter_values.end()) {
                return result;
            }
            result.context_available = true;
            if (owner->second.owner_design_process) {
                const semantic::design::ProcessOccurrence*
                    owner_occurrence = nullptr;
                for (const auto& process : built.design_ir.processes()) {
                    if (process.runtime_index
                        != *owner->second.owner_design_process) {
                        continue;
                    }
                    if (owner_occurrence != nullptr) {
                        throw std::logic_error {
                            "class owner process context is ambiguous"
                        };
                    }
                    owner_occurrence = &process;
                }
                if (owner_occurrence == nullptr) {
                    throw std::logic_error {
                        "class owner process context is stale"
                    };
                }
                const auto* exact = exact_specialization(
                    built.design_ir, owner_occurrence->specialization);
                if (exact == nullptr) {
                    throw std::logic_error {
                        "class owner process has no exact specialization"
                    };
                }
                const auto& owner_specialization = *exact;
                if (owner_specialization.language
                        == semantic::Language::system_verilog
                    && owner_specialization.unit.valid()
                    && owner_specialization.instance.valid()) {
                    const semantic::design::InstanceOccurrence*
                        owner_instance = nullptr;
                    for (const auto& instance : built.design_ir.instances()) {
                        if (instance.id != owner_specialization.instance) {
                            continue;
                        }
                        if (owner_instance != nullptr) {
                            throw std::logic_error {
                                "class owner instance context is ambiguous"
                            };
                        }
                        owner_instance = &instance;
                    }
                    if (owner_instance != nullptr && owner_instance->path.valid()) {
                        result.owner_unit = owner_specialization.unit;
                        result.owner_path = std::string {
                            built.design_ir.path(owner_instance->path)
                        };
                    }
                }
            }
            const auto binding = owner->second.parameters.find(declaration);
            if (binding == owner->second.parameters.end()) {
                return result;
            }
            result.found = true;
            result.value = binding->second;
            return result;
        });
    class_hir_execution.set_output_services(
        [this](const std::string_view text, const bool newline) {
            if (output_hook) {
                output_hook(0U, text, newline,
                    interpreter->scheduler().now(),
                    interpreter->scheduler().delta());
            }
        },
        [this] {
            return static_cast<std::uint64_t>(interpreter->scheduler().now());
        });
    // A displayed UVM report prints on the simulation output (IEEE
    // 1800.2-2020 6.4.2); the composed record ends in its newline.
    uvm_reports.server().set_display_sink([this](const std::string_view text) {
        if (output_hook) {
            output_hook(0U,
                text.ends_with('\n') ? text.substr(0, text.size() - 1U) : text,
                true, interpreter->scheduler().now(),
                interpreter->scheduler().delta());
        }
    });
    class_hir_execution.set_string_property_services(
        [this](const runtime::SystemVerilogClassHandle handle,
            const std::string_view property) -> std::optional<std::string> {
            try {
                const auto& value = class_heap.property(handle, property);
                if (value.kind == runtime::SystemVerilogClassPropertyKind::String) {
                    return value.string;
                }
            } catch (const std::exception&) {
            }
            return std::nullopt;
        },
        [this](const runtime::SystemVerilogClassHandle handle,
            const std::string_view property, const std::string_view text) {
            try {
                auto& value = class_heap.property(handle, property);
                if (value.kind == runtime::SystemVerilogClassPropertyKind::String) {
                    value.string = std::string { text };
                    return true;
                }
            } catch (const std::exception&) {
            }
            return false;
        });
    class_hir_execution.set_class_property_container_read_service(
        [this](const runtime::SystemVerilogClassHandle handle,
            const std::string_view property)
            -> std::optional<runtime::simir::ContainerValue> {
            if (!class_heap.contains(handle)) {
                return std::nullopt;
            }
            return class_heap.property(handle, property).container;
        });
    const auto invoke_source_with_notifications = [this](auto&& invoke) {
        const bool outermost = source_function_dispatch_depth == 0U;
        const auto before = outermost
            ? packed_class_snapshot() : PackedSnapshot { };
        const auto static_before = outermost
            ? packed_static_snapshot() : StaticPackedSnapshot { };
        ++source_function_dispatch_depth;
        runtime::PackedLogic4 result;
        try {
            result = invoke();
        } catch (...) {
            --source_function_dispatch_depth;
            if (outermost) {
                // HIR property writes are direct store updates. If a nested
                // invocation failed after changing state, publish the final
                // diff once at the outer dispatch boundary while preserving
                // the original invocation exception.
                try {
                    notify_class_changes(before);
                } catch (...) {
                }
                try {
                    notify_static_changes(static_before);
                } catch (...) {
                }
            }
            throw;
        }
        --source_function_dispatch_depth;
        if (outermost) {
            notify_class_changes(before);
            notify_static_changes(static_before);
        }
        return result;
    };
    class_hir_execution.set_runtime_services(
        [this, invoke_source_with_notifications](
            const auto handle, const auto identity, auto& actuals,
            auto& string_actuals, const auto names, const auto directions,
            const bool virtual_dispatch, const auto actual_references) {
            return invoke_source_with_notifications([&] {
                return invoke_source_function(handle, identity, actuals,
                    string_actuals, names, directions, virtual_dispatch,
                    actual_references);
            });
        },
        [this, invoke_source_with_notifications](
            const auto identity, auto& actuals, auto& string_actuals,
            const auto names, const auto directions,
            const auto actual_references) {
            return invoke_source_with_notifications([&] {
                return invoke_source_static_function(
                    identity, actuals, string_actuals, names, directions,
                    actual_references);
            });
        },
        [this](const auto handle, const auto property) {
            return packed_property_value(class_heap.property(handle, property));
        },
        [this](const auto handle, const auto property, const auto& value) {
            assign_property_value(
                class_heap.property(handle, property), property, value,
                class_heap.object(handle).specialization_identity);
        },
        [this](const auto property, const auto handle) {
            const auto [owner, name] = static_property_parts(property);
            return packed_property_value(class_static_store.property(
                static_property_specialization(owner, handle), name));
        },
        [this](const auto property, const auto& value, const auto handle) {
            const auto [owner, name] = static_property_parts(property);
            assign_property_value(class_static_store.property(
                static_property_specialization(owner, handle), name),
                property, value);
        });
    class_hir_execution.set_class_handle_container_write_service(
        [this](const auto owner, const auto property, const auto key,
            const auto value) {
            auto& storage = class_heap.property(owner, property);
            if (!storage.handle_container
                || storage.handle_container->kind()
                    != runtime::SystemVerilogClassContainerKind::AssociativeArray) {
                throw std::invalid_argument {
                    "class property is not an associative handle container"
                };
            }
            storage.handle_container->set(
                class_heap, std::string { key }, value);
        });
    class_hir_execution.set_class_handle_fixed_array_write_service(
        [this](const auto owner, const auto property, const auto index,
            const auto value) {
            auto& storage = class_heap.property(owner, property);
            if (!storage.handle_container
                || storage.handle_container->kind()
                    != runtime::SystemVerilogClassContainerKind::FixedArray) {
                throw std::invalid_argument {
                    "class property is not a fixed class-handle array"
                };
            }
            storage.handle_container->set(class_heap, index, value);
        });
    class_hir_execution.set_static_property_value_read_service(
        [this](const std::string_view identity)
            -> std::optional<runtime::SystemVerilogClassPropertyValue> {
            try {
                const auto [owner, name] = static_property_parts(identity);
                return class_static_store.property(owner, name);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        });
    const auto package_variable
        = [this](const semantic::DeclarationId declaration_id)
        -> const semantic::sv::Declaration* {
        const auto* found
            = built.systemverilog_hir.find_declaration(declaration_id);
        if (found == nullptr
            || (found->form != semantic::sv::DeclarationForm::variable
                && found->form != semantic::sv::DeclarationForm::net)) {
            return nullptr;
        }
        const bool package_member = std::ranges::any_of(
            built.systemverilog_hir.units(), [&](const auto& unit) {
                return unit.kind == semantic::sv::UnitKind::package
                    && unit.scope == found->scope
                    && std::ranges::find(unit.declarations, declaration_id)
                        != unit.declarations.end();
            });
        return package_member ? &*found : nullptr;
    };
    const auto package_runtime_object = [this, package_variable](
        const semantic::DeclarationId declaration_id,
        const semantic::design::ObjectKind kind)
        -> std::optional<std::uint64_t> {
        const auto* declaration = package_variable(declaration_id);
        if (declaration == nullptr || !declaration->declared_value) {
            return std::nullopt;
        }
        std::optional<std::uint64_t> runtime_index;
        for (const auto& object : built.design_ir.objects()) {
            if (object.kind != kind
                || object.declaration_value != declaration->declared_value) {
                continue;
            }
            if (runtime_index && *runtime_index != object.runtime_index) {
                throw std::logic_error {
                    "package declaration has multiple runtime storage identities"
                };
            }
            runtime_index = object.runtime_index;
        }
        return runtime_index;
    };
    class_hir_execution.set_package_global_services(
        [this, package_runtime_object](const semantic::DeclarationId id)
            -> std::optional<runtime::PackedLogic4> {
            const auto index = narrow_runtime_index<runtime::simir::SignalId>(
                package_runtime_object(
                    id, semantic::design::ObjectKind::signal));
            if (!index) {
                return std::nullopt;
            }
            return interpreter->signal_value_snapshot(*index);
        },
        [this, package_runtime_object](const semantic::DeclarationId id,
            const runtime::PackedLogic4& value) {
            const auto index = narrow_runtime_index<runtime::simir::SignalId>(
                package_runtime_object(
                    id, semantic::design::ObjectKind::signal));
            if (!index) {
                return false;
            }
            const auto current = interpreter->signal_value_snapshot(*index);
            interpreter->deposit_signal(*index,
                application_detail::SystemVerilogClassExecution::resize_packed(
                    value, current.width()));
            return true;
        },
        [this, package_runtime_object](
            const semantic::DeclarationId id) -> std::optional<std::string> {
            const auto index
                = narrow_runtime_index<runtime::simir::StringObjectId>(
                    package_runtime_object(
                        id, semantic::design::ObjectKind::string));
            if (!index) {
                return std::nullopt;
            }
            return interpreter->string_object_value(*index);
        },
        [this, package_runtime_object](
            const semantic::DeclarationId id, const std::string_view value) {
            const auto index
                = narrow_runtime_index<runtime::simir::StringObjectId>(
                    package_runtime_object(
                        id, semantic::design::ObjectKind::string));
            if (!index) {
                return false;
            }
            interpreter->deposit_string_object(*index, value);
            return true;
        },
        [this, package_runtime_object](const semantic::DeclarationId id)
            -> std::optional<runtime::simir::ContainerValue> {
            const auto index
                = narrow_runtime_index<runtime::simir::ContainerObjectId>(
                    package_runtime_object(
                        id, semantic::design::ObjectKind::container));
            if (!index) {
                return std::nullopt;
            }
            return interpreter->container_object_value_snapshot(*index);
        },
        [this, package_runtime_object](const semantic::DeclarationId id,
            runtime::simir::ContainerValue value) {
            const auto index
                = narrow_runtime_index<runtime::simir::ContainerObjectId>(
                    package_runtime_object(
                        id, semantic::design::ObjectKind::container));
            if (!index) {
                return false;
            }
            interpreter->deposit_container_object(*index, std::move(value));
            return true;
        });
    const auto resolution = application_detail::magnitude_and_unit(
        built.time_resolution);
    const auto factor = resolution
        ? application_detail::unit_femtoseconds(resolution->unit)
        : std::nullopt;
    if (!resolution || !factor || resolution->magnitude == 0
        || resolution->magnitude
            > std::numeric_limits<std::uint64_t>::max() / *factor) {
        throw std::logic_error {
            "built project has an invalid time resolution"
        };
    }
    interpreter->set_time_resolution_femtoseconds(
        resolution->magnitude * *factor);
    interpreter->set_system_command_hook(
        [](const std::optional<std::string_view> command) {
            if (!command) {
                return static_cast<std::int32_t>(std::system(nullptr));
            }
            const auto owned = std::string { *command };
            return static_cast<std::int32_t>(
                std::system(owned.c_str()));
        });
    if (built.code_coverage_enabled) {
        const auto& inventory = built.design.code_coverage_inventory();
        std::size_t counter_count
            = inventory ? inventory->total_points : 0U;
        if (!inventory) {
            for (const auto& process_info : built.design_ir.processes()) {
                const auto process =
                    runtime::simir::InterpreterProgramAccess::view(
                        *interpreter, process_info.runtime_index);
                for (std::size_t instruction = 0U;
                    instruction < process.operations().size(); ++instruction) {
                    const auto* hit = runtime::simir::operation_get_if<
                        runtime::simir::CodeCoverageHit>(
                        &process.operations()[instruction]);
                    if (hit == nullptr) {
                        continue;
                    }
                    const auto counter
                        = process.operations().code_coverage_counter(
                            instruction, hit->counter);
                    counter_count = std::max(
                        counter_count,
                        static_cast<std::size_t>(counter.value) + 1U);
                }
            }
        }
        interpreter->set_code_coverage_counters(
            std::vector<std::uint64_t>(counter_count));
    }
    coverage_execution_mode = engine == SimulationEngine::interpreter
            || engine == SimulationEngine::v4
        ? frontend::SystemVerilogCoverageExecutionMode::Interpreter
        : built.optimization == fsim::project::Optimization::o0
        ? frontend::SystemVerilogCoverageExecutionMode::LlvmO0
        : frontend::SystemVerilogCoverageExecutionMode::LlvmO2;
    uvm_phases.set_scheduler(interpreter->scheduler());
    uvm_synchronization.set_scheduler(interpreter->scheduler());
    uvm_activity.set_scheduler(interpreter->scheduler());
    uvm_phases.set_activity_service(uvm_activity);
    uvm_objections.set_scheduler(interpreter->scheduler());
    uvm_objections.set_activity_service(uvm_activity);
    uvm_phases.set_objection_service(uvm_objections);
    uvm_tlm1.set_scheduler(interpreter->scheduler());
    uvm_tlm1.set_activity_service(uvm_activity);
    uvm_tlm1.set_phase_service(uvm_phases);
    uvm_tlm2.set_scheduler(interpreter->scheduler());
    uvm_tlm2.set_activity_service(uvm_activity);
    uvm_sequences.set_activity_service(uvm_activity);
    uvm_sequences.set_role_services(uvm_tlm1, uvm_config_db);
    uvm_register_model.set_frontdoor_services(uvm_sequences, uvm_tlm1,
        uvm_phases);
    uvm_register_model.set_activity_service(uvm_activity);
    uvm_register_model.set_backdoor_transport({ [this](const auto, const std::string_view path)
                                                    -> std::optional<std::size_t> {
                                                   const auto signal = backdoor_signal(path);
                                                   return signal ? std::optional<std::size_t> {
                                                       interpreter->signal_value_snapshot(*signal).width()
                                                   }
                                                                 : std::nullopt;
                                               },
        [this](const auto, const std::string_view path) {
            const auto signal = backdoor_signal(path);
            if (!signal)
                throw std::invalid_argument { "UVM HDL path does not resolve" };
            return interpreter->signal_value_snapshot(*signal);
        },
        [this](const auto, const std::string_view path, const auto kind,
            const runtime::PackedLogic4& value) {
            const auto signal = backdoor_signal(path);
            if (!signal)
                throw std::invalid_argument { "UVM HDL path does not resolve" };
            switch (kind) {
            case runtime::SystemVerilogUvmRegisterBackdoorKind::Deposit:
                interpreter->deposit_signal(*signal, value);
                return;
            case runtime::SystemVerilogUvmRegisterBackdoorKind::Force:
                interpreter->force_signal(*signal, value);
                return;
            case runtime::SystemVerilogUvmRegisterBackdoorKind::Release:
                interpreter->release_signal(*signal);
                return;
            case runtime::SystemVerilogUvmRegisterBackdoorKind::Read:
                break;
            }
            throw std::invalid_argument { "invalid UVM HDL write operation" };
        } });
    uvm_callbacks.set_activity_service(uvm_activity);
    uvm_transactions.set_scheduler(interpreter->scheduler());
    uvm_transactions.set_activity_service(uvm_activity);
    uvm_foreign.set_scheduler(interpreter->scheduler());
    uvm_foreign.set_integrated_services(
        uvm_sequences, uvm_callbacks, uvm_transactions, uvm_register_model);
    (void)uvm_phases.create_standard_schedule();
    if (built.systemverilog_uvm_checkpoint) {
        const auto& saved = *built.systemverilog_uvm_checkpoint;
        const auto error = runtime::verify_systemverilog_uvm_checkpoint(
            saved, uvm_foreign, saved.provenance);
        if (error != runtime::SystemVerilogUvmCheckpointError::None) {
            throw std::logic_error {
                "portable SystemVerilog UVM bootstrap state does not match "
                "the fresh simulation"
            };
        }
    }
    register_systemverilog_uvm_object_types(
        built.compiled_systemverilog_class_specializations, uvm_objects);
    register_systemverilog_uvm_registry_types(
        built.compiled_systemverilog_class_specializations, uvm_registry);
    interpreter->set_class_allocate_context_hook(
        [this](const runtime::simir::ProcessId report_process,
            const runtime::simir::ProcessId owner_design_process,
            const std::string_view scope,
            const std::string_view specialization,
            const std::string_view declared_type,
            const std::span<const runtime::PackedLogic4> actuals,
            const std::span<const std::string> string_actuals,
            const std::span<const std::string> actual_names) {
            class_heap.set_next_object_seed(
                interpreter->take_class_allocation_seed());
            return construct_class(
                specialization, declared_type, actuals, string_actuals,
                actual_names, scope, 0U, owner_design_process, 0U,
                report_process);
        });
    // A string class property read or written from a process travels as
    // the single string actual of a reserved method identity,
    // `@string-property-read:<property>` or `@string-property-write:...`.
    static constexpr std::string_view string_property_prefix {
        "@string-property-"
    };
    const auto access_string_property
        = [](runtime::SystemVerilogClassPropertyValue& property,
              const std::string_view method,
              std::vector<std::string>& string_actuals) {
              if (property.kind
                      != runtime::SystemVerilogClassPropertyKind::String
                  || string_actuals.size() != 1U) {
                  throw std::invalid_argument {
                      "class string property access has no string "
                      "property or actual"
                  };
              }
              if (method.starts_with("@string-property-write:")) {
                  property.string = string_actuals.front();
              } else {
                  string_actuals.front() = property.string;
              }
              return runtime::PackedLogic4(1U, runtime::Logic4::zero);
          };
    interpreter->set_class_property_container_hook(
        [this](const std::uint64_t handle, const std::string_view property,
            runtime::simir::ContainerValue& value, const bool write) {
            if (!class_heap.contains(handle)) {
                throw std::invalid_argument {
                    "class container property access through a null or "
                    "released handle"
                };
            }
            auto& held = class_heap.property(handle, property);
            // A class-handle container keeps its handle storage; the value
            // container is a copy of it (IEEE 1800-2017 7.4, 8.4).
            if (held.handle_container) {
                if (write) {
                    assign_handle_container(
                        class_heap, *held.handle_container, value);
                } else {
                    value = handle_container_value(
                        *held.handle_container, value.type);
                }
                return;
            }
            auto& stored = held.container;
            if (write) {
                stored = value;
            } else if (stored) {
                // Elements a randomize call created keep the declared type
                // the reading process gives them.
                if (stored->type != value.type
                    && stored->type.element_kind == value.type.element_kind
                    && stored->type.element_width == value.type.element_width
                    && stored->type.element_kind
                        == runtime::simir::ContainerElementKind::Packed) {
                    stored->type = value.type;
                }
                value = *stored;
            }
        });
    interpreter->set_class_property_read_hook(
        [this](const std::uint64_t handle, const std::string_view property) {
            return packed_property_value(class_heap.property(handle, property));
        });
    class_heap_change_signal = built.design.find_signal(
        "$sv_class_heap_change");
    interpreter->set_class_property_write_hook(
        [this](const std::uint64_t handle,
            const std::string_view property,
            const runtime::PackedLogic4& value) {
            const auto before = packed_class_snapshot();
            assign_property_value(
                class_heap.property(handle, property), property, value,
                class_heap.object(handle).specialization_identity);
            notify_class_changes(before);
        });
    interpreter->set_class_method_call_context_hook(
        [this, access_string_property, invoke_source_with_notifications](
            const runtime::simir::ProcessId report_process,
            const std::uint64_t handle,
            const std::string_view method,
            std::vector<runtime::PackedLogic4>& actuals,
            std::vector<std::string>& string_actuals,
            const std::span<const std::string> names,
            const std::span<const std::uint8_t> directions,
            const std::span<const runtime::SystemVerilogConstraintTemplate>
                inline_constraints,
            const bool virtual_dispatch) {
            return invoke_source_with_notifications([&] {
                // `new handle` copies the object's properties into a new object
                // of the same class (IEEE 1800-2017 8.12).
                if (method == "@shallow-copy") {
                    if (handle == 0U) {
                        throw std::invalid_argument {
                            "shallow copy of a null class handle"
                        };
                    }
                    const auto source = class_heap.object(handle);
                    const auto copy = allocate_class(
                        source.specialization_identity, source.declared_type,
                        source.random_root_identity);
                    try {
                        class_heap.copy_properties(copy, handle);
                        const auto owner = class_owner_parameter_values.find(handle);
                        if (owner != class_owner_parameter_values.end()) {
                            const bool inserted = class_owner_parameter_values
                                .emplace(copy, owner->second).second;
                            if (!inserted) {
                                throw std::logic_error {
                                    "shallow copy reused a live owner context handle"
                                };
                            }
                        }
                    } catch (...) {
                        class_owner_parameter_values.erase(copy);
                        (void)class_heap.release(copy);
                        throw;
                    }
                    return runtime::PackedLogic4::from_aval_bval(64U, copy, 0U);
                }
                // `obj.srandom(seed)` (IEEE 1800-2017 18.13.3).
                if (method == "@object-srandom") {
                    if (handle == 0U) {
                        throw std::invalid_argument {
                            "srandom of a null class handle"
                        };
                    }
                    const auto seed = actuals.empty()
                        ? std::uint64_t { }
                        : actuals.front().low_word().aval;
                    class_heap.reseed_random(handle, seed);
                    return runtime::PackedLogic4::from_aval_bval(1U, 0U, 0U);
                }
                // The object's random number generator state (IEEE
                // 1800-2017 18.13.4, 18.13.5).
                if (method == "@builtin-get-randstate"
                    || method == "@object-set-randstate") {
                    if (handle == 0U || string_actuals.empty()) {
                        throw std::invalid_argument {
                            "random state access requires an object and a "
                            "string"
                        };
                    }
                    auto& object = class_heap.object(handle);
                    if (method == "@builtin-get-randstate") {
                        auto state = "fsim-randstate:"
                            + std::to_string(object.random_object_seed) + ";";
                        for (const auto& [call, ordinal] :
                            object.random_call_ordinals) {
                            state += std::to_string(call.size()) + ":" + call
                                + "=" + std::to_string(ordinal) + ";";
                        }
                        string_actuals.back() = std::move(state);
                    } else {
                        restore_random_state(object, string_actuals.front());
                    }
                    return runtime::PackedLogic4(1U, runtime::Logic4::zero);
                }
                if (method.starts_with(string_property_prefix)) {
                    auto result = access_string_property(
                        class_heap.property(handle,
                            method.substr(method.find(':') + 1U)),
                        method, string_actuals);
                    return result;
                }
                auto result = method.starts_with("@container-")
                    ? invoke_class_container(handle, method, actuals)
                    : method.starts_with("@checked-cast:")
                    ? invoke_checked_class_cast(handle, method)
                    : method == "@builtin-randomize"
                    ? invoke_source_randomize(
                          handle, names, actuals, inline_constraints)
                    : method.starts_with("@builtin-rand-mode:")
                        || method.starts_with("@builtin-constraint-mode:")
                    ? invoke_source_randomization_mode(
                          handle, method, actuals)
                    : invoke_source_function(
                          handle, method, actuals, string_actuals,
                          names, directions, virtual_dispatch, {},
                          report_process);
                return result;
            });
        });
    interpreter->set_class_static_property_read_hook(
        [this](const std::string_view property) {
            const auto [owner, name] = static_property_parts(property);
            return packed_property_value(
                class_static_store.property(owner, name));
        });
    interpreter->set_class_static_property_write_hook(
        [this](const std::string_view property,
            const runtime::PackedLogic4& value) {
            const auto before = packed_static_snapshot();
            const auto [owner, name] = static_property_parts(property);
            assign_property_value(
                class_static_store.property(owner, name), property, value);
            notify_static_changes(before);
        });
    interpreter->set_class_static_method_call_context_hook(
        [this, access_string_property, invoke_source_with_notifications](
            const runtime::simir::ProcessId report_process,
            const std::string_view method,
            std::vector<runtime::PackedLogic4>& actuals,
            std::vector<std::string>& string_actuals,
            const std::span<const std::string> names,
            const std::span<const std::uint8_t> directions) {
            return invoke_source_with_notifications([&] {
                if (method.starts_with(string_property_prefix)) {
                    const auto [owner, name] = static_property_parts(
                        method.substr(method.find(':') + 1U));
                    auto result = access_string_property(
                        class_static_store.property(owner, name),
                        method, string_actuals);
                    return result;
                }
                return invoke_source_static_function(
                    method, actuals, string_actuals, names, directions, {},
                    report_process);
            });
        });
    interpreter->set_coverage_sample_hook(
        [this](const std::string_view identity,
            const std::span<const runtime::PackedLogic4> actuals,
            const std::span<const std::uint8_t> signed_actuals,
            const std::span<const frontend::SystemVerilogScalarKind> scalar_kinds,
            const runtime::simir::CoverageSampleTrigger trigger) {
            auto& coverage = built.systemverilog_coverage;
            auto* const hir_instance
                = find_systemverilog_covergroup_instance(
                    built.systemverilog_hir, identity);
            if (hir_instance == nullptr) {
                throw std::out_of_range {
                    "SystemVerilog covergroup instance '"
                    + std::string { identity } + "' is unavailable"
                };
            }
            if (stopped_covergroups.contains(std::string { identity })) {
                return;
            }
            const auto* const hir_declaration
                = find_systemverilog_covergroup_declaration(
                    built.systemverilog_hir,
                    hir_instance->declaration_identity);
            if (hir_declaration == nullptr) {
                throw std::out_of_range {
                    "SystemVerilog covergroup declaration '"
                    + hir_instance->declaration_identity + "' is unavailable"
                };
            }
            const auto procedural = trigger
                == runtime::simir::CoverageSampleTrigger::procedural;
            if (procedural
                && (!hir_declaration->sampling
                    || hir_declaration->sampling->kind
                        != semantic::sv::CovergroupSamplingKind::
                            with_function_sample)) {
                throw std::invalid_argument {
                    "procedural covergroup sampling requires a resolved sample profile"
                };
            }
            const auto expected_actuals = procedural
                ? hir_declaration->sampling->formals.size()
                : static_cast<std::size_t>(std::ranges::count_if(
                      hir_declaration->items,
                      [](const auto& item) {
                          return item.kind
                              == semantic::sv::CoverageItemKind::coverpoint;
                      }));
            if (actuals.size() != expected_actuals
                || signed_actuals.size() != actuals.size()
                || scalar_kinds.size() != actuals.size()) {
                throw std::invalid_argument {
                    "covergroup sample actuals do not match the resolved "
                    "sample profile"
                };
            }
            std::vector<frontend::SystemVerilogCovergroupSampleInput> inputs;
            inputs.reserve(hir_declaration->items.size());
            std::size_t coverpoint_index { };
            for (const auto& item : hir_declaration->items) {
                if (item.kind
                    != semantic::sv::CoverageItemKind::coverpoint) {
                    continue;
                }
                if (procedural
                    && (item.expression_tokens.size() != 1U
                        || item.expression_tokens.front().kind
                            != static_cast<std::uint16_t>(
                                frontend::TokenKind::Identifier))) {
                    throw std::invalid_argument {
                        "executable covergroup sampling currently requires "
                        "each coverpoint to be one direct sample formal"
                    };
                }
                std::size_t actual_index { coverpoint_index++ };
                if (procedural) {
                    const auto formal = std::ranges::find(
                        hir_declaration->sampling->formals,
                        item.expression_tokens.front().text,
                        &semantic::sv::CovergroupFormal::name);
                    if (formal
                        == hir_declaration->sampling->formals.end()) {
                        throw std::invalid_argument {
                            "coverpoint expression does not resolve to a sample formal"
                        };
                    }
                    actual_index = static_cast<std::size_t>(std::distance(
                        hir_declaration->sampling->formals.begin(), formal));
                }
                const auto& value = actuals[actual_index];
                frontend::SystemVerilogCoverageSampleValue sample;
                sample.width = static_cast<std::uint32_t>(value.width());
                sample.signed_value = signed_actuals[actual_index] != 0U;
                sample.scalar_kind = scalar_kinds[actual_index];
                if (sample.is_real()) {
                    if (value.is_logic9()
                        || (sample.scalar_kind
                                    == frontend::SystemVerilogScalarKind::ShortReal
                                ? value.width() != 32U
                                : value.width() != 64U)) {
                        throw std::invalid_argument {
                            "real covergroup sample actual has an invalid payload"
                        };
                    }
                    sample.scalar_bits = value.low_word().aval;
                }
                if (value.width() <= 64U && !value.is_logic9()) {
                    const auto scalar = value.low_word();
                    sample.value = static_cast<std::int64_t>(scalar.aval);
                    sample.unknown_mask = scalar.bval;
                }
                sample.value_bits.reserve(value.width());
                sample.unknown_bits.reserve(value.width());
                for (std::size_t bit = value.width(); bit > 0U; --bit) {
                    const auto state = runtime::to_logic4(
                        value.get_logic9(bit - 1U));
                    sample.value_bits.push_back(
                        state == runtime::Logic4::one
                                || state == runtime::Logic4::x
                            ? '1'
                            : '0');
                    sample.unknown_bits.push_back(
                        state == runtime::Logic4::x
                                || state == runtime::Logic4::z
                            ? '1'
                            : '0');
                }
                inputs.push_back({
                    static_cast<std::size_t>(item.declaration_index),
                    std::move(sample) });
            }
            auto instance = std::ranges::find(
                coverage.instances, identity,
                &semantic::sv::CovergroupInstance::runtime_identity);
            if (instance == coverage.instances.end()) {
                throw std::logic_error {
                    "SystemVerilog coverage runtime adapter is inconsistent "
                    "with compiled HIR"
                };
            }
            auto instance_adapter
                = project_systemverilog_covergroup_instance(
                    *instance, built.semantics);
            const auto declaration_adapter
                = project_systemverilog_covergroup_declaration(
                    *hir_declaration, built.semantics);
            const auto result
                = frontend::execute_systemverilog_covergroup_sample(
                    instance_adapter, declaration_adapter,
                    trigger == runtime::simir::CoverageSampleTrigger::procedural
                        ? frontend::SystemVerilogCoverageSampleTrigger::Procedural
                        : trigger == runtime::simir::CoverageSampleTrigger::event
                        ? frontend::SystemVerilogCoverageSampleTrigger::Event
                        : frontend::SystemVerilogCoverageSampleTrigger::Explicit,
                    coverage_execution_mode, inputs,
                    std::span<const frontend::SystemVerilogCoverageCallback> { },
                    coverage_execution_state, coverage_diagnostics);
            synchronize_systemverilog_covergroup_runtime_state(
                *instance, instance_adapter, built.semantics);
            coverage.callback_events.insert(
                coverage.callback_events.end(),
                result.events.begin(), result.events.end());
            auto trace_events
                = frontend::build_systemverilog_coverage_trace_events(
                    result, interpreter->scheduler().now(),
                    interpreter->scheduler().delta(), coverage.aliases);
            coverage.trace_events.insert(
                coverage.trace_events.end(),
                std::make_move_iterator(trace_events.begin()),
                std::make_move_iterator(trace_events.end()));
            refresh_systemverilog_coverage_reports(
                coverage, built.systemverilog_hir, built.semantics);
            if (!result.accepted) {
                throw std::runtime_error {
                    coverage_diagnostics.empty()
                        ? "SystemVerilog covergroup sample was rejected"
                        : coverage_diagnostics.back().message
                };
            }
        });
    interpreter->set_coverage_query_hook([this](
                                             const auto kind,
                                             const std::string& identity) {
        using runtime::simir::CoverageQueryKind;
        const auto& coverage = built.systemverilog_coverage;
        const auto adapter = project_systemverilog_coverage_state(
            coverage, built.systemverilog_hir, built.semantics);
        const auto declaration_named = [&](const std::string& name)
            -> const frontend::SystemVerilogCovergroupDeclaration& {
            const auto found = std::ranges::find(
                adapter.declarations, name,
                &frontend::SystemVerilogCovergroupDeclaration::
                    canonical_identity);
            if (found == adapter.declarations.end()) {
                throw std::runtime_error {
                    "SystemVerilog coverage query names no covergroup '"
                    + name + "'"
                };
            }
            return *found;
        };
        // IEEE 1800-2023 19.8: get_inst_coverage() is the coverage of one
        // instance and get_coverage() that of the covergroup type; both,
        // like the overall queries, are unrounded reals (19.11).
        frontend::SystemVerilogCoveragePercentage percentage;
        if (kind == CoverageQueryKind::covergroup_instance) {
            const auto instance = std::ranges::find(
                adapter.instances, identity,
                &frontend::SystemVerilogCovergroupInstance::runtime_identity);
            if (instance == adapter.instances.end()) {
                throw std::runtime_error {
                    "SystemVerilog coverage query names no covergroup "
                    "instance '" + identity + "'"
                };
            }
            percentage = frontend::
                calculate_systemverilog_covergroup_instance_percentage(
                    declaration_named(instance->declaration_identity),
                    *instance)
                    .coverage;
        } else if (kind == CoverageQueryKind::covergroup_type) {
            percentage = frontend::
                calculate_systemverilog_covergroup_type_percentage(
                    declaration_named(identity), adapter.instances)
                    .coverage;
        } else if (kind == CoverageQueryKind::overall_instance) {
            percentage = frontend::
                calculate_systemverilog_overall_instance_coverage_percentage(
                    adapter.declarations, adapter.instances);
        } else {
            percentage = frontend::
                calculate_systemverilog_overall_coverage_percentage(
                    adapter.declarations, adapter.instances);
        }
        const auto encoded = runtime::encode_systemverilog_scalar_payload(
            runtime::SystemVerilogScalarValue::real(percentage.percent()));
        if (!encoded) {
            throw std::logic_error {
                "SystemVerilog overall coverage cannot be encoded as real"
            };
        }
        return encoded.value;
    });
    interpreter->set_coverage_database_control_hook(
        [this](const auto& event) {
            control_coverage_database(event);
        });
    interpreter->set_coverage_access_hook(
        [this](const auto& event) {
            return access_standard_coverage(event);
        });
    interpreter->set_coverage_control_hook(
        [this](const auto& event) {
            return control_standard_coverage(event);
        });
    std::map<std::string, std::size_t> declaration_counts;
    for (const auto& specialization :
        built.compiled_systemverilog_class_specializations) {
        ++declaration_counts[specialization.declaration_identity];
    }
    for (const auto& specialization :
        built.compiled_systemverilog_class_specializations) {
        runtime::SystemVerilogClassStaticDescriptor descriptor;
        descriptor.specialization_identity = specialization.specialization_identity;
        descriptor.base_specialization_identity = specialization.base
            ? specialization.base->specialization_identity
            : std::string { };
        if (declaration_counts[specialization.declaration_identity] == 1
            && specialization.declaration_identity
                != specialization.specialization_identity) {
            descriptor.aliases.push_back(specialization.declaration_identity);
        }
        for (const auto& property : specialization.properties) {
            if (property.static_storage) {
                descriptor.properties.push_back(
                    class_property_descriptor(
                        property, built.systemverilog_hir));
            }
        }
        class_static_store.register_specialization(std::move(descriptor));
    }
    class_static_store.initialize_all();
    for (const auto& row : package_class_startup_initializers) {
        interpreter->register_package_static_startup_callback(
            row.startup_order, row.writable_signal_ids,
            row.writable_string_object_ids, row.writable_container_object_ids,
            [this, signal = row.signal, initializer = row.initializer] {
                const auto value
                    = class_hir_execution.evaluate_initializer(initializer);
                if (!value || value->width() != 64U) {
                    throw std::logic_error {
                        "compiled package class-handle initializer could not be evaluated"
                    };
                }
                interpreter->deposit_signal(signal, *value);
            });
    }
    for (const auto& row : package_static_class_startup_initializers) {
        interpreter->register_package_static_startup_callback(
            row.startup_order, row.writable_signal_ids,
            row.writable_string_object_ids, row.writable_container_object_ids,
            [this, specialization = row.specialization_identity,
                property = row.property_name,
                canonical = row.canonical_identity,
                initializer = row.initializer] {
                const auto value
                    = class_hir_execution.evaluate_initializer(initializer);
                if (!value) {
                    throw std::logic_error {
                        "compiled package static class-property initializer could not be evaluated"
                    };
                }
                assign_property_value(
                    class_static_store.property(specialization, property),
                    canonical, *value);
            });
    }
    // Nonpackage static class properties retain their eager pre-process
    // construction. Package-owned `new` initializers are registered above so
    // their effects share declaration order with package variable initializers.
    for (const auto& specialization :
        built.compiled_systemverilog_class_specializations) {
        const auto* owner_unit = package_unit_for_class(built, specialization);
        for (const auto& property : specialization.properties) {
            if (!property.static_storage || !property.initializer
                || property.type.class_identity.empty()
                || property.type.container_form) {
                continue;
            }
            const auto initializer = std::ranges::find(
                built.systemverilog_hir.expressions(), *property.initializer,
                &semantic::sv::Expression::id);
            if (initializer == built.systemverilog_hir.expressions().end()
                || initializer->kind
                    != semantic::sv::ExpressionKind::class_allocation) {
                continue;
            }
            if (owner_unit != nullptr
                && owner_unit->kind == semantic::sv::UnitKind::package) {
                const auto row = std::ranges::find_if(
                    package_static_class_startup_initializers,
                    [&](const auto& candidate) {
                        return candidate.specialization_identity
                                == specialization.specialization_identity
                            && candidate.property_declaration
                                == property.declaration;
                    });
                if (row == package_static_class_startup_initializers.end()) {
                    throw std::logic_error {
                        "package static class allocation is missing startup callback metadata"
                    };
                }
                continue;
            }
            if (const auto value = class_hir_execution.evaluate_initializer(
                    *property.initializer)) {
                assign_property_value(
                    class_static_store.property(
                        specialization.specialization_identity, property.name),
                    property.canonical_identity, *value);
            }
        }
    }
    interpreter->set_file_root(built.file_root);
    if (vpi_runtime_updates
        == SystemVerilogVpiRuntimeUpdates::omitted) {
        vpi_registry = make_empty_systemverilog_vpi_registry();
    } else {
        auto published_vpi
            = make_systemverilog_vpi_design(built, *interpreter);
        vpi_registry = std::move(published_vpi.registry);
        vpi_signal_handles = std::move(published_vpi.signals);
        vpi_handle_signals = std::move(published_vpi.handles);
        vpi_driver_bindings = std::move(published_vpi.drivers);
        vpi_event_handles = std::move(published_vpi.events);
        for (const auto& [signal, handles] : vpi_event_handles) {
            for (const auto handle : handles) {
                vpi_event_signals.emplace(handle, signal);
            }
        }
        vpi_assertion_handles = std::move(published_vpi.assertions);
        vpi_scalar_kinds = std::move(published_vpi.scalar_kinds);
        vpi_categories = std::move(published_vpi.categories);
        vpi_container_words = std::move(published_vpi.container_words);
        vpi_read_only_container_words
            = std::move(published_vpi.read_only_container_words);
        vpi_word_handles = std::move(published_vpi.word_handles);
        vpi_live_memories = std::move(published_vpi.live_memories);
        vpi_container_scalar_kinds
            = std::move(published_vpi.container_scalar_kinds);
        vpi_container_categories
            = std::move(published_vpi.container_categories);
    }
    for (const auto& process : built.design_ir.processes()) {
        if (const auto assertion = vpi_assertion_handles.find(process.name);
            assertion != vpi_assertion_handles.end()) {
            vpi_assertion_processes[assertion->second].insert(
                static_cast<runtime::simir::ProcessId>(process.runtime_index));
        }
    }
    vpi_time = std::make_unique<runtime::SystemVerilogVpiTimeService>(
        interpreter->scheduler(),
        systemverilog_vpi_time_profile(built.time_resolution));
    vpi_data_read
        = std::make_unique<runtime::SystemVerilogVpiDataReadService>(
            *vpi_registry,
            runtime::SystemVerilogVpiDataReadAccess::LimitedInteractive,
            std::string { },
            [this] {
                return runtime::SystemVerilogVpiDataReadPosition {
                    interpreter->scheduler().now(),
                    interpreter->scheduler().delta()
                };
            });
    constexpr runtime::StableOrder vpi_callback_order
        = runtime::StableOrder { 1 } << 60U;
    constexpr runtime::StableOrder vpi_value_order
        = runtime::StableOrder { 2 } << 60U;
    constexpr runtime::StableOrder vpi_control_order
        = runtime::StableOrder { 3 } << 60U;
    vpi_callbacks
        = std::make_unique<runtime::SystemVerilogVpiCallbackManager>(
            *vpi_registry,
            interpreter->scheduler(),
            *vpi_time,
            vpi_callback_order,
            [this](runtime::SystemVerilogVpiCallbackKind,
                std::optional<fsim_vpi_handle_v1> object) {
                if (object) {
                    const auto signal = vpi_handle_signals.find(*object);
                    if (signal != vpi_handle_signals.end()) {
                        interpreter->prepare_signal_observation(signal->second);
                        return;
                    }
                }
                // An object without an exact signal binding may cover a scope
                // or aggregate. Retain a conservative barrier for all exposed
                // signal, original-driver, and named-event state.
                for (const auto& [signal, handles] : vpi_signal_handles) {
                    (void)handles;
                    interpreter->prepare_signal_observation(signal);
                }
                for (const auto& [signal, drivers] : vpi_driver_bindings) {
                    (void)drivers;
                    interpreter->prepare_signal_observation(signal);
                }
                for (const auto& [signal, handles] : vpi_event_handles) {
                    (void)handles;
                    interpreter->prepare_signal_observation(signal);
                }
            });
    vpi_values = std::make_unique<runtime::SystemVerilogVpiValueControl>(
        *vpi_registry, interpreter->scheduler(), vpi_value_order);
    vpi_control
        = std::make_unique<runtime::SystemVerilogVpiControlService>(
            *vpi_registry,
            interpreter->scheduler(),
            *vpi_callbacks,
            vpi_control_order,
            false,
            false);
    vpi_assertions
        = std::make_unique<runtime::SystemVerilogVpiAssertionApi>(
            *vpi_registry,
            *vpi_callbacks,
            [this](
                const runtime::SystemVerilogVpiAssertionControlOperation operation,
                const std::optional<fsim_vpi_handle_v1> object) {
                std::set<runtime::simir::ProcessId> selected;
                if (object) {
                    const auto processes = vpi_assertion_processes.find(*object);
                    if (processes == vpi_assertion_processes.end()) {
                        return false;
                    }
                    selected = processes->second;
                } else {
                    selected = concurrent_assertion_design_processes;
                }
                switch (operation) {
                case runtime::SystemVerilogVpiAssertionControlOperation::Reset:
                    return true;
                case runtime::SystemVerilogVpiAssertionControlOperation::Enable:
                    if (!object) {
                        concurrent_assertions_enabled = true;
                        disabled_vpi_assertion_processes.clear();
                        enabled_vpi_assertion_processes.clear();
                    } else {
                        for (const auto process : selected) {
                            disabled_vpi_assertion_processes.erase(process);
                        }
                        enabled_vpi_assertion_processes.insert(
                            selected.begin(), selected.end());
                    }
                    return true;
                case runtime::SystemVerilogVpiAssertionControlOperation::Disable:
                    if (!object) {
                        concurrent_assertions_enabled = false;
                        disabled_vpi_assertion_processes.clear();
                        enabled_vpi_assertion_processes.clear();
                    } else {
                        for (const auto process : selected) {
                            enabled_vpi_assertion_processes.erase(process);
                        }
                        disabled_vpi_assertion_processes.insert(
                            selected.begin(), selected.end());
                    }
                    return true;
                case runtime::SystemVerilogVpiAssertionControlOperation::Kill:
                    if (!object) {
                        concurrent_assertions_enabled = false;
                        disabled_vpi_assertion_processes.clear();
                        enabled_vpi_assertion_processes.clear();
                    } else {
                        for (const auto process : selected) {
                            enabled_vpi_assertion_processes.erase(process);
                        }
                        disabled_vpi_assertion_processes.insert(
                            selected.begin(), selected.end());
                    }
                    const std::vector<runtime::simir::ProcessId>
                        selected_processes { selected.begin(), selected.end() };
                    interpreter->kill_dynamic_processes(selected_processes);
                    interpreter->kill_sva_monitors(selected_processes);
                    std::erase_if(
                        pending_concurrent_assertions,
                        [&](const auto& item) {
                            return selected.contains(
                                item.second.design_process);
                        });
                    return true;
                }
                return false;
            });
    configure_standard_vpi_coverage();
    vpi_systems
        = std::make_unique<runtime::SystemVerilogVpiSystemRegistry>(
            *vpi_registry);
    if (vpi_runtime_updates
        != SystemVerilogVpiRuntimeUpdates::omitted) {
        vpi_value_state_observer = vpi_registry->add_value_state_observer(
            [this](const runtime::SystemVerilogVpiValueStateUpdate& update) {
                return apply_vpi_value_state(update);
            });
        if (!vpi_value_state_observer) {
            throw std::logic_error {
                "failed to connect the live VPI value bridge"
            };
        }
    }
    vhdl_psl = std::make_unique<VhdlPslExecution>(built.vhdl_hir,
        built.semantics, built.design, built.design_ir,
        [this](const runtime::simir::SignalId signal) {
            return interpreter->signal_value_snapshot(signal);
        });
    vhdl_psl->set_completion_hook(
        [this](const runtime::VhdlPslAttemptSnapshot& attempt) {
            if (vhdl_psl_attempt_hook) {
                try {
                    vhdl_psl_attempt_hook(attempt);
                } catch (...) {
                }
            }
            ConcurrentAssertionEvent event;
            event.name = attempt.monitor;
            event.process = attempt.instance_identity;
            event.instance_identity = attempt.instance_identity;
            event.source_span = attempt.source_span;
            event.slot = attempt.slot;
            event.time = attempt.end_time;
            event.delta = attempt.end_delta;
            switch (attempt.directive_kind) {
            case runtime::VhdlPslDirectiveKind::assumption:
                event.kind = ConcurrentAssertionCoverageKind::assumption;
                break;
            case runtime::VhdlPslDirectiveKind::restriction:
                event.kind = ConcurrentAssertionCoverageKind::restriction;
                break;
            case runtime::VhdlPslDirectiveKind::cover:
                event.kind = ConcurrentAssertionCoverageKind::cover;
                break;
            case runtime::VhdlPslDirectiveKind::assertion:
                event.kind = ConcurrentAssertionCoverageKind::assertion;
                break;
            }
            switch (attempt.outcome) {
            case runtime::VhdlPslAttemptOutcome::pass:
                event.outcome = ConcurrentAssertionOutcome::pass;
                break;
            case runtime::VhdlPslAttemptOutcome::failure:
                event.outcome = ConcurrentAssertionOutcome::failure;
                break;
            case runtime::VhdlPslAttemptOutcome::vacuous:
                event.outcome = ConcurrentAssertionOutcome::vacuous;
                break;
            case runtime::VhdlPslAttemptOutcome::aborted:
                event.outcome = ConcurrentAssertionOutcome::aborted;
                break;
            case runtime::VhdlPslAttemptOutcome::pending:
                return;
            }
            publish_vpi_assertion(event);
            if (concurrent_assertion_hook) {
                try {
                    concurrent_assertion_hook(event);
                } catch (...) {
                }
            }
            if (attempt.outcome != runtime::VhdlPslAttemptOutcome::failure
                || attempt.directive_kind
                    == runtime::VhdlPslDirectiveKind::cover
                || !report_hook) {
                return;
            }
            auto kind = std::string_view { "assert" };
            auto severity = runtime::simir::AssertionSeverity::error;
            if (attempt.directive_kind
                == runtime::VhdlPslDirectiveKind::assumption) {
                kind = "assume";
            } else if (attempt.directive_kind
                == runtime::VhdlPslDirectiveKind::restriction) {
                kind = "restrict";
                severity = runtime::simir::AssertionSeverity::warning;
            }
            try {
                report_hook(std::numeric_limits<runtime::simir::ProcessId>::max(),
                    "VHDL PSL " + std::string { kind } + " '"
                        + attempt.monitor + "' failed",
                    severity,
                    runtime::simir::SourceLocation {
                        "vhdl-span:" + std::to_string(attempt.source_span),
                        1U, 1U },
                    attempt.end_time, attempt.end_delta);
            } catch (...) {
            }
        });
    interpreter->set_vhdl_psl_api_hook(
        [this](const runtime::simir::VhdlPslApiKind kind,
            const std::optional<bool> enable) {
            return vhdl_psl->apply_api(kind, enable);
        });
    setup_execution(engine);
    refresh_observation_hooks();
    interpreter->set_fork_spawn_filter(
        [this](const runtime::simir::ProcessId process) {
            return !concurrent_assertion_design_processes.contains(process)
                || enabled_vpi_assertion_processes.contains(process)
                || (concurrent_assertions_enabled
                    && !disabled_vpi_assertion_processes.contains(process));
        });
    interpreter_output_hook =
        [this](
            const runtime::simir::ProcessId process,
            const std::string_view text,
            const bool newline,
            const SimulationTick time,
            const std::uint64_t delta) {
            if (trusted_builtin_stdout_output && !text.empty()
                && text.front() == '\x1f') {
                interpreter->prepare_output_callback_observation();
            }
            constexpr std::string_view assertion_marker {
                "\x1f"
                "fsim.concurrent-assertion|"
            };
            constexpr std::string_view pending_assertion_marker {
                "\x1f"
                "fsim.concurrent-assertion-pending|"
            };
            constexpr std::string_view control_marker {
                "\x1f"
                "fsim.assertion-control|"
            };
            constexpr std::string_view coverage_control_marker {
                "\x1f"
                "fsim.coverage-control|"
            };
            constexpr std::string_view print_timescale_marker {
                "\x1f"
                "fsim.printtimescale|"
            };
            if (text.starts_with(print_timescale_marker)) {
                const auto runtime_process = interpreter->design_process(process);
                const auto process_occurrence = std::ranges::find(
                    built.design_ir.processes(),
                    runtime_process,
                    &semantic::design::ProcessOccurrence::runtime_index);
                if (process_occurrence == built.design_ir.processes().end()) {
                    throw std::runtime_error {
                        "$printtimescale process has no DesignIR occurrence"
                    };
                }
                const auto& current_specialization
                    = built.design_ir.specializations().at(
                        process_occurrence->specialization.value());
                const auto& current_instance = built.design_ir.instances().at(
                    current_specialization.instance.value());
                const auto requested = text.substr(
                    print_timescale_marker.size());
                const auto find_instance = [&](const std::string_view path) {
                    return std::ranges::find_if(
                        built.design_ir.instances(), [&](const auto& instance) {
                            return built.design_ir.path(instance.path) == path;
                        });
                };
                auto selected = built.design_ir.instances().end();
                if (requested.empty()) {
                    selected = built.design_ir.instances().begin()
                        + static_cast<std::ptrdiff_t>(
                            current_specialization.instance.value());
                } else {
                    auto target = requested;
                    constexpr std::string_view root_prefix { "$root." };
                    if (target.starts_with(root_prefix)) {
                        target.remove_prefix(root_prefix.size());
                    }
                    selected = find_instance(target);
                    auto prefix = std::string {
                        built.design_ir.path(current_instance.path) };
                    while (selected == built.design_ir.instances().end()
                        && !prefix.empty()) {
                        selected = find_instance(prefix + "."
                            + std::string { target });
                        const auto separator = prefix.rfind('.');
                        if (separator == std::string::npos) {
                            prefix.clear();
                        } else {
                            prefix.erase(separator);
                        }
                    }
                }
                if (selected == built.design_ir.instances().end()) {
                    throw std::runtime_error {
                        "$printtimescale references unavailable instance '"
                        + std::string { requested } + "'"
                    };
                }
                const auto& specialization
                    = built.design_ir.specializations().at(
                        selected->specialization.value());
                if (!specialization.unit.valid()
                    || specialization.unit.value()
                        >= built.semantics.units().size()) {
                    throw std::runtime_error {
                        "$printtimescale instance has no semantic unit"
                    };
                }
                const auto& semantic_unit = built.semantics.units().at(
                    specialization.unit.value());
                const auto unit = std::ranges::find_if(
                    built.systemverilog_hir.units(),
                    [&](const semantic::sv::Unit& candidate) {
                        return candidate.library == semantic_unit.library
                            && candidate.name == semantic_unit.name;
                    });
                if (unit == built.systemverilog_hir.units().end()) {
                    throw std::runtime_error {
                        "$printtimescale instance has no SystemVerilog HIR unit"
                    };
                }
                const auto time_unit = unit->compilation.time_unit.empty()
                    ? std::string_view { "1s" }
                    : std::string_view { unit->compilation.time_unit };
                const auto time_precision
                    = unit->compilation.time_precision.empty()
                    ? std::string_view { "1s" }
                    : std::string_view { unit->compilation.time_precision };
                if (output_hook) {
                    output_hook(
                        process,
                        "Time scale of ("
                            + std::string {
                                built.design_ir.path(selected->path) }
                            + ") is "
                            + std::string { time_unit } + " / "
                            + std::string { time_precision },
                        true,
                        time,
                        delta);
                }
                return;
            }
            if (text.starts_with(pending_assertion_marker)) {
                const auto payload = text.substr(
                    pending_assertion_marker.size());
                std::vector<std::string_view> fields;
                std::size_t begin { };
                while (begin <= payload.size()) {
                    const auto end = payload.find('|', begin);
                    fields.push_back(payload.substr(
                        begin, end == std::string_view::npos ? payload.size() - begin : end - begin));
                    if (end == std::string_view::npos)
                        break;
                    begin = end + 1U;
                }
                if (fields.size() < 4U
                    || (fields[2] != "failure"
                        && fields[2] != "vacuous"
                        && fields[2] != "obligation"
                        && fields[2] != "obligation-vacuous")) {
                    throw std::runtime_error {
                        "malformed pending concurrent-assertion marker"
                    };
                }
                PendingConcurrentAssertion pending;
                pending.process = interpreter->dynamic_process_root(process);
                pending.design_process = interpreter->design_process(process);
                pending.kind = fields[1];
                pending.vacuous = fields[2] == "vacuous"
                    || fields[2] == "obligation-vacuous";
                pending.name = fields[3];
                const auto parsed_slot = std::from_chars(
                    fields[0].data(),
                    fields[0].data() + fields[0].size(), pending.slot);
                if (parsed_slot.ec != std::errc { }
                    || parsed_slot.ptr
                        != fields[0].data() + fields[0].size()) {
                    throw std::runtime_error {
                        "malformed pending concurrent-assertion slot"
                    };
                }
                const auto decode = [](const std::string_view encoded) {
                    const auto nibble = [](const char character)
                        -> std::optional<unsigned> {
                        if (character >= '0' && character <= '9')
                            return static_cast<unsigned>(character - '0');
                        if (character >= 'a' && character <= 'f')
                            return static_cast<unsigned>(
                                character - 'a' + 10);
                        return std::nullopt;
                    };
                    if (encoded.size() % 2U != 0U)
                        return std::optional<std::string> { };
                    std::string result;
                    result.reserve(encoded.size() / 2U);
                    for (std::size_t index = 0U;
                        index < encoded.size(); index += 2U) {
                        const auto high = nibble(encoded[index]);
                        const auto low = nibble(encoded[index + 1U]);
                        if (!high || !low)
                            return std::optional<std::string> { };
                        result.push_back(static_cast<char>(
                            (*high << 4U) | *low));
                    }
                    return std::optional<std::string> { std::move(result) };
                };
                for (std::size_t index = 4U; index < fields.size(); ++index) {
                    std::array<std::string_view, 8> action_fields;
                    std::size_t action_begin { };
                    for (std::size_t field = 0U;
                        field < action_fields.size(); ++field) {
                        const auto end = fields[index].find(',', action_begin);
                        action_fields[field] = fields[index].substr(
                            action_begin,
                            end == std::string_view::npos
                                ? fields[index].size() - action_begin
                                : end - action_begin);
                        action_begin = end == std::string_view::npos
                            ? fields[index].size() + 1U
                            : end + 1U;
                    }
                    PendingConcurrentAssertionAction action;
                    action.report = action_fields[0] == "R";
                    std::uint32_t severity { };
                    std::uint32_t newline_value { };
                    std::uint32_t generated_text { };
                    const auto severity_result = std::from_chars(
                        action_fields[1].data(),
                        action_fields[1].data() + action_fields[1].size(),
                        severity);
                    const auto newline_result = std::from_chars(
                        action_fields[2].data(),
                        action_fields[2].data() + action_fields[2].size(),
                        newline_value);
                    auto message = decode(action_fields[3]);
                    auto path = decode(action_fields[4]);
                    const auto line_result = std::from_chars(
                        action_fields[5].data(),
                        action_fields[5].data() + action_fields[5].size(),
                        action.source.line);
                    const auto column_result = std::from_chars(
                        action_fields[6].data(),
                        action_fields[6].data() + action_fields[6].size(),
                        action.source.column);
                    const auto generated_text_result = std::from_chars(
                        action_fields[7].data(),
                        action_fields[7].data() + action_fields[7].size(),
                        generated_text);
                    if ((action_fields[0] != "R" && action_fields[0] != "D")
                        || severity_result.ec != std::errc { }
                        || severity > static_cast<std::uint32_t>(
                               runtime::simir::AssertionSeverity::failure)
                        || newline_result.ec != std::errc { }
                        || newline_value > 1U || !message || !path
                        || line_result.ec != std::errc { }
                        || column_result.ec != std::errc { }
                        || generated_text_result.ec != std::errc { }
                        || generated_text
                            > static_cast<std::uint32_t>(
                                semantic::sv::GeneratedTextKind::
                                    systemverilog_file_macro_derived)) {
                        throw std::runtime_error {
                            "malformed pending concurrent-assertion action"
                        };
                    }
                    action.severity = static_cast<
                        runtime::simir::AssertionSeverity>(severity);
                    action.newline = newline_value != 0U;
                    action.text = std::move(*message);
                    action.source.path = std::move(*path);
                    pending.actions.push_back(std::move(action));
                }
                if (fields[2] == "obligation") {
                    // The actions of a monitor attempt that is still
                    // obligated when simulation ends.
                    sva_obligations[pending.design_process] = std::move(pending);
                    return;
                }
                if (fields[2] == "obligation-vacuous") {
                    sva_vacuous_obligations[pending.design_process]
                        = std::move(pending);
                    return;
                }
                const auto attempt = pending.process;
                pending_concurrent_assertions[attempt] = std::move(pending);
                return;
            }
            if (text.starts_with(control_marker)) {
                auto control = text.substr(control_marker.size());
                if (control.starts_with("assertcontrol|")) {
                    std::uint32_t control_type { };
                    const auto value = control.substr(
                        std::string_view { "assertcontrol|" }.size());
                    const auto parsed = std::from_chars(
                        value.data(), value.data() + value.size(), control_type);
                    if (parsed.ec == std::errc { }) {
                        switch (control_type) {
                        case 3:
                            control = "asserton";
                            break;
                        case 4:
                            control = "assertoff";
                            break;
                        case 5:
                            control = "assertkill";
                            break;
                        case 6:
                            control = "assertpasson";
                            break;
                        case 7:
                            control = "assertpassoff";
                            break;
                        case 8:
                            control = "assertfailon";
                            break;
                        case 9:
                            control = "assertfailoff";
                            break;
                        case 10:
                            control = "assertnonvacuouson";
                            break;
                        case 11:
                            control = "assertvacuousoff";
                            break;
                        default:
                            break;
                        }
                    }
                }
                if (control == "asserton") {
                    concurrent_assertions_enabled = true;
                    disabled_vpi_assertion_processes.clear();
                    enabled_vpi_assertion_processes.clear();
                } else if (control == "assertoff"
                    || control == "assertkill") {
                    concurrent_assertions_enabled = false;
                    disabled_vpi_assertion_processes.clear();
                    enabled_vpi_assertion_processes.clear();
                    if (control == "assertkill") {
                        const std::vector<runtime::simir::ProcessId>
                            processes {
                                concurrent_assertion_design_processes.begin(),
                                concurrent_assertion_design_processes.end()
                            };
                        interpreter->kill_dynamic_processes(processes);
                        interpreter->kill_sva_monitors(processes);
                        pending_concurrent_assertions.clear();
                    }
                } else if (control == "assertpasson") {
                    concurrent_assertion_pass_actions_enabled = true;
                    concurrent_assertion_vacuous_actions_enabled = true;
                } else if (control == "assertpassoff") {
                    concurrent_assertion_pass_actions_enabled = false;
                    concurrent_assertion_vacuous_actions_enabled = false;
                } else if (control == "assertnonvacuouson") {
                    concurrent_assertion_pass_actions_enabled = true;
                } else if (control == "assertvacuousoff") {
                    concurrent_assertion_vacuous_actions_enabled = false;
                } else if (control == "assertfailon") {
                    concurrent_assertion_failure_actions_enabled = true;
                } else if (control == "assertfailoff") {
                    concurrent_assertion_failure_actions_enabled = false;
                }
                return;
            }
            if (text.starts_with(coverage_control_marker)) {
                const auto control = text.substr(
                    coverage_control_marker.size());
                const auto separator = control.find('|');
                if (separator == std::string_view::npos
                    || separator + 1U == control.size()) {
                    throw std::runtime_error {
                        "malformed covergroup control marker"
                    };
                }
                const auto action = control.substr(0U, separator);
                const auto identity = control.substr(separator + 1U);
                if (find_systemverilog_covergroup_instance(
                        built.systemverilog_hir, identity)
                    == nullptr) {
                    throw std::runtime_error {
                        "covergroup control references unavailable instance '"
                        + std::string { identity } + "'"
                    };
                }
                if (action == "start") {
                    stopped_covergroups.erase(std::string { identity });
                } else if (action == "stop") {
                    stopped_covergroups.insert(std::string { identity });
                } else {
                    throw std::runtime_error {
                        "malformed covergroup control action"
                    };
                }
                return;
            }
            if (text.starts_with(assertion_marker)) {
                const auto payload = text.substr(assertion_marker.size());
                const auto slot_end = payload.find('|');
                const auto kind_end = slot_end == std::string_view::npos
                    ? std::string_view::npos
                    : payload.find('|', slot_end + 1U);
                const auto outcome_end = kind_end == std::string_view::npos
                    ? std::string_view::npos
                    : payload.find('|', kind_end + 1U);
                if (slot_end != std::string_view::npos
                    && kind_end != std::string_view::npos
                    && outcome_end != std::string_view::npos) {
                    const auto coverage_key
                        = interpreter->design_process(process);
                    const auto outcome = payload.substr(
                        kind_end + 1U,
                        outcome_end - kind_end - 1U);
                    if (outcome == "register") {
                        concurrent_assertion_design_processes.insert(
                            coverage_key);
                        return;
                    }
                    if (outcome == "abort") {
                        const std::array processes { coverage_key };
                        interpreter->kill_dynamic_processes(processes);
                        std::erase_if(
                            pending_concurrent_assertions,
                            [&](const auto& item) {
                                return item.second.design_process
                                    == coverage_key;
                            });
                        return;
                    }
                    pending_concurrent_assertions.erase(
                        interpreter->dynamic_process_root(process));
                    concurrent_assertion_actions_suppressed[process]
                        = outcome == "pass"
                        ? !concurrent_assertion_pass_actions_enabled
                        : outcome == "vacuous"
                        ? !concurrent_assertion_vacuous_actions_enabled
                        : !concurrent_assertion_failure_actions_enabled;
                    std::uint32_t slot { };
                    const auto slot_text = payload.substr(0, slot_end);
                    std::from_chars(
                        slot_text.data(), slot_text.data() + slot_text.size(),
                        slot);
                    const auto kind = payload.substr(
                        slot_end + 1U, kind_end - slot_end - 1U);
                    const auto coverage_index
                        = ensure_concurrent_assertion_coverage(
                            coverage_key, slot, kind,
                            payload.substr(outcome_end + 1U));
                    auto& coverage
                        = concurrent_assertion_coverage[coverage_index];
                    ConcurrentAssertionEvent event;
                    event.name = coverage.name;
                    event.process = coverage.process;
                    event.kind = coverage.kind;
                    event.slot = coverage.slot;
                    event.instance_identity = coverage.instance_identity;
                    event.source_span = coverage.source_span;
                    event.time = time;
                    event.delta = delta;
                    event.action_suppressed
                        = concurrent_assertion_actions_suppressed[process];
                    ++coverage.attempts;
                    if (outcome == "pass") {
                        event.outcome = ConcurrentAssertionOutcome::pass;
                        ++coverage.passes;
                    } else if (outcome == "vacuous") {
                        event.outcome = ConcurrentAssertionOutcome::vacuous;
                        ++coverage.vacuous;
                    } else {
                        event.outcome = ConcurrentAssertionOutcome::failure;
                        ++coverage.failures;
                    }
                    concurrent_assertion_events.push_back(std::move(event));
                    publish_vpi_assertion(
                        concurrent_assertion_events.back());
                    if (concurrent_assertion_hook) {
                        concurrent_assertion_hook(
                            concurrent_assertion_events.back());
                    }
                }
                return;
            }
            if (concurrent_assertion_actions_suppressed[process]) {
                return;
            }
            if (output_hook) {
                output_hook(process, text, newline, time, delta);
            }
        };
    interpreter->set_output_hook(interpreter_output_hook);
    // The interpreter-level hook filters on app-owned state only, so it is a
    // trusted text sink; it prepares observation itself before any user hook
    // that may inspect simulation state.
    const auto dispatch_report = [this](
        const std::optional<runtime::simir::ProcessId> invoking_process,
        const std::string_view message,
        const runtime::simir::AssertionSeverity severity,
        const runtime::simir::SourceLocation& source,
        const SimulationTick time,
        const std::uint64_t delta) {
        const auto process = invoking_process.value_or(
            std::numeric_limits<runtime::simir::ProcessId>::max());
        if (invoking_process) {
            const auto suppressed
                = concurrent_assertion_actions_suppressed.find(process);
            if (suppressed != concurrent_assertion_actions_suppressed.end()
                && suppressed->second) {
                return;
            }
        }
        constexpr std::string_view numeric_metavalue_message
            = "numeric_std.to_integer detected a metavalue and returned zero";
        if (severity == runtime::simir::AssertionSeverity::warning
            && message == numeric_metavalue_message
            && !numeric_metavalue_reports.emplace(
                                             process,
                                             source.path,
                                             source.line,
                                             source.column)
                .second) {
            return;
        }
        if (report_hook) {
            if (!trusted_text_report_hook) {
                interpreter->prepare_output_callback_observation();
            }
            report_hook(
                process, message, severity, source, time, delta);
        }
    };
    runtime::simir::InterpreterProgramAccess::set_trusted_text_report_hook(
        *interpreter,
        [dispatch_report](
            const runtime::simir::ProcessId process,
            const std::string_view message,
            const runtime::simir::AssertionSeverity severity,
            const runtime::simir::SourceLocation& source,
            const SimulationTick time,
            const std::uint64_t delta) {
            dispatch_report(process, message, severity, source, time, delta);
        });
    class_hir_execution.set_report_service(
        [this, dispatch_report](
            const std::optional<runtime::simir::ProcessId> process,
            const std::string_view message,
            const runtime::simir::AssertionSeverity severity,
            const semantic::SourceSpanId source_span) {
            runtime::simir::SourceLocation source;
            const auto& spans = built.semantics.source_spans();
            if (source_span.value() < spans.size()) {
                const auto& span = spans[source_span.value()];
                auto path = span.logical_name;
                const auto& files = built.semantics.source_files();
                if (path.empty() && span.file.value() < files.size()) {
                    path = files[span.file.value()].physical_name;
                }
                source = runtime::simir::SourceLocation {
                    std::move(path), span.begin.line, span.begin.column
                };
            }
            dispatch_report(
                process,
                message,
                severity,
                source,
                interpreter->scheduler().now(),
                interpreter->scheduler().delta());
        });
    interpreter->scheduler().set_safe_point_hook(
        [this](
            runtime::Scheduler& scheduler,
            const runtime::SchedulerPhase phase) {
            if (observation_hooks_refresh_pending
                && signal_observer_hook_depth == 0U
                && scalar_signal_observer_hook_depth == 0U) {
                refresh_observation_hooks();
            }
            if (phase == runtime::SchedulerPhase::update) {
                vhdl_psl->observe(scheduler.now(), scheduler.delta());
            }
            if (safe_point_hook) {
                safe_point_hook(scheduler, phase);
            }
            if (safe_point_observers.empty()) {
                return;
            }
            // Observers may synchronously remove themselves.
            std::vector<SafePointHook> callbacks;
            callbacks.reserve(safe_point_observers.size());
            for (const auto& [token, callback] : safe_point_observers) {
                (void)token;
                callbacks.push_back(callback);
            }
            for (const auto& callback : callbacks) {
                callback(scheduler, phase);
            }
        });
}

void Simulation::Impl::refresh_observation_hooks()
{
    if (signal_observer_hook_depth != 0U
        || scalar_signal_observer_hook_depth != 0U) {
        observation_hooks_refresh_pending = true;
        return;
    }
    observation_hooks_refresh_pending = false;

    const bool has_public_signal_observer
        = static_cast<bool>(signal_change_hook)
        || !signal_observers.empty();
    const bool needs_signal_bridge = has_public_signal_observer
        || (vpi_runtime_updates_enabled && !vpi_signal_handles.empty());

    if (signal_observation_bridge_installed != needs_signal_bridge) {
        if (needs_signal_bridge) {
            interpreter->set_signal_change_hook(
                [this](const SignalId signal,
                    const PackedLogic4& value,
                    const SimulationTick time) {
                    ++signal_observer_hook_depth;
                    try {
                        publish_vpi_signal(signal, value);
                        if (signal_change_hook) {
                            signal_change_hook(
                                signal, value, time,
                                interpreter->scheduler().delta());
                        }
                        {
                            const auto callbacks = signal_observer_snapshot;
                            const auto generation
                                = signal_observer_generation.next();
                            signal_observer_generation = generation;
                            for (const auto& observer : *callbacks) {
                                if (observer->visible_at(generation)) {
                                    observer->callback(signal, value, time,
                                        interpreter->scheduler().delta());
                                }
                            }
                        }
                    } catch (...) {
                        --signal_observer_hook_depth;
                        compact_signal_observer_snapshot();
                        throw;
                    }
                    --signal_observer_hook_depth;
                    compact_signal_observer_snapshot();
                });
        } else {
            interpreter->set_signal_change_hook({});
        }
        signal_observation_bridge_installed = needs_signal_bridge;
    }

    const bool has_public_scalar_observer
        = static_cast<bool>(scalar_signal_change_hook)
        || !scalar_signal_observers.empty();
    if (scalar_signal_observation_bridge_installed
        != has_public_scalar_observer) {
        if (has_public_scalar_observer) {
            interpreter->set_scalar_signal_change_hook(
                [this](const SignalId signal,
                    const runtime::SystemVerilogScalarValue& value,
                    const SimulationTick time) {
                    ++scalar_signal_observer_hook_depth;
                    try {
                        if (scalar_signal_change_hook) {
                            scalar_signal_change_hook(
                                signal, value, time,
                                interpreter->scheduler().delta());
                        }
                        {
                            const auto callbacks
                                = scalar_signal_observer_snapshot;
                            const auto generation
                                = scalar_signal_observer_generation.next();
                            scalar_signal_observer_generation = generation;
                            for (const auto& observer : *callbacks) {
                                if (observer->visible_at(generation)) {
                                    observer->callback(
                                        signal, value, time,
                                        interpreter->scheduler().delta());
                                }
                            }
                        }
                    } catch (...) {
                        --scalar_signal_observer_hook_depth;
                        compact_scalar_signal_observer_snapshot();
                        throw;
                    }
                    --scalar_signal_observer_hook_depth;
                    compact_scalar_signal_observer_snapshot();
                });
        } else {
            interpreter->set_scalar_signal_change_hook({});
        }
        scalar_signal_observation_bridge_installed
            = has_public_scalar_observer;
    }

    const bool has_vpi_observation = vpi_runtime_updates_enabled
        && vpi_callbacks->has_registrations()
        && (!vpi_signal_handles.empty() || !vpi_driver_bindings.empty()
            || !vpi_event_handles.empty());
    const bool needs_native_observation_hooks
        = needs_signal_bridge || has_vpi_observation;
    if (native_signal_observation_hooks_installed
        != needs_native_observation_hooks) {
        if (needs_native_observation_hooks) {
            interpreter->set_native_signal_observation_required_hook(
                [this](const SignalId signal) {
                    return signal_change_hook || !signal_observers.empty()
                        || (vpi_runtime_updates_enabled
                            && vpi_callbacks->has_registrations()
                            && (vpi_signal_handles.contains(signal)
                                || vpi_driver_bindings.contains(signal)
                                || vpi_event_handles.contains(signal)));
                });
            interpreter->set_native_signal_observation_any_hook(
                [this] {
                    return signal_change_hook || !signal_observers.empty()
                        || (vpi_runtime_updates_enabled
                            && vpi_callbacks->has_registrations()
                            && (!vpi_signal_handles.empty()
                                || !vpi_driver_bindings.empty()
                                || !vpi_event_handles.empty()));
                });
        } else {
            interpreter->set_native_signal_observation_required_hook({});
            interpreter->set_native_signal_observation_any_hook({});
        }
        native_signal_observation_hooks_installed
            = needs_native_observation_hooks;
    }

    if (vpi_observation_hooks_installed != vpi_runtime_updates_enabled) {
        if (vpi_runtime_updates_enabled) {
            interpreter->set_stored_signal_change_hook(
                [this](const SignalId signal, const SimulationTick) {
                    publish_vpi_stored_signal(signal);
                });
            interpreter->set_driver_change_hook(
                [this](const runtime::simir::ProcessId process,
                    const SignalId signal, const SimulationTick) {
                    publish_vpi_driver(process, signal);
                });
            interpreter->set_event_trigger_hook(
                [this](const SignalId event, const SimulationTick) {
                    publish_vpi_event(event);
                });
            interpreter->set_container_object_change_hook(
                [this](const runtime::simir::ContainerObjectId object,
                    const SimulationTick) {
                    publish_vpi_container(object);
                });
        } else {
            interpreter->set_stored_signal_change_hook({});
            interpreter->set_driver_change_hook({});
            interpreter->set_event_trigger_hook({});
            interpreter->set_container_object_change_hook({});
        }
        vpi_observation_hooks_installed = vpi_runtime_updates_enabled;
    }

}

[[nodiscard]] std::size_t Simulation::Impl::ensure_concurrent_assertion_coverage(
    const runtime::simir::ProcessId process,
    const std::uint32_t slot,
    const std::string_view kind,
    const std::string_view name)
{
    if (const auto found = concurrent_assertion_indices.find(process);
        found != concurrent_assertion_indices.end()) {
        return found->second;
    }
    ConcurrentAssertionCoverage coverage;
    coverage.slot = slot;
    coverage.kind = kind == "assumption"
        ? ConcurrentAssertionCoverageKind::assumption
        : kind == "cover"
        ? ConcurrentAssertionCoverageKind::cover
        : kind == "restriction"
        ? ConcurrentAssertionCoverageKind::restriction
        : ConcurrentAssertionCoverageKind::assertion;
    coverage.name = name;
    const auto occurrence = std::ranges::find_if(
        built.design_ir.processes(),
        [&](const auto& item) {
            return item.runtime_index == process;
        });
    coverage.process = occurrence != built.design_ir.processes().end()
        ? occurrence->name
        : coverage.name;
    if (occurrence != built.design_ir.processes().end()) {
        coverage.source_span
            = occurrence->source ? occurrence->source->value() : 0U;
        const auto specialization_index
            = occurrence->specialization.value();
        const auto& specializations = built.design_ir.specializations();
        const auto& instances = built.design_ir.instances();
        if (specialization_index < specializations.size()) {
            const auto instance_index
                = specializations[specialization_index].instance.value();
            if (instance_index < instances.size()) {
                coverage.instance_identity = std::string {
                    built.design_ir.path(
                        instances[instance_index].path) };
            }
        }
    }
    concurrent_assertion_coverage.push_back(std::move(coverage));
    const auto inserted = concurrent_assertion_coverage.size() - 1U;
    concurrent_assertion_indices.emplace(process, inserted);
    return inserted;
}

void Simulation::Impl::finish_concurrent_assertions(
    const SimulationTick time,
    const std::uint64_t delta)
{
    std::vector<std::pair<runtime::simir::ProcessId,
        const PendingConcurrentAssertion*>>
        finished;
    for (const auto& [process, pending] : pending_concurrent_assertions) {
        finished.emplace_back(process, &pending);
    }
    for (const auto& obligation : interpreter
             ? interpreter->finish_sva_monitors()
             : std::vector<runtime::simir::Interpreter::SvaObligation> { }) {
        if (const auto found = sva_obligations.find(obligation.process);
            found != sva_obligations.end()) {
            for (std::size_t count = 0; count < obligation.failures;
                ++count) {
                finished.emplace_back(obligation.process, &found->second);
            }
        }
        if (const auto found
            = sva_vacuous_obligations.find(obligation.process);
            found != sva_vacuous_obligations.end()) {
            for (std::size_t count = 0; count < obligation.holds; ++count) {
                finished.emplace_back(obligation.process, &found->second);
            }
        }
    }
    for (const auto& [process, pending_entry] : finished) {
        const auto& pending = *pending_entry;
        const auto index = ensure_concurrent_assertion_coverage(
            pending.design_process, pending.slot, pending.kind, pending.name);
        auto& coverage = concurrent_assertion_coverage[index];
        ++coverage.attempts;
        if (pending.vacuous)
            ++coverage.vacuous;
        else
            ++coverage.failures;
        ConcurrentAssertionEvent event;
        event.name = coverage.name;
        event.process = coverage.process;
        event.kind = coverage.kind;
        event.slot = coverage.slot;
        event.instance_identity = coverage.instance_identity;
        event.source_span = coverage.source_span;
        event.time = time;
        event.delta = delta;
        event.outcome = pending.vacuous
            ? ConcurrentAssertionOutcome::vacuous
            : ConcurrentAssertionOutcome::failure;
        event.action_suppressed = pending.vacuous
            ? !concurrent_assertion_vacuous_actions_enabled
            : !concurrent_assertion_failure_actions_enabled;
        concurrent_assertion_events.push_back(event);
        publish_vpi_assertion(concurrent_assertion_events.back());
        if (concurrent_assertion_hook) {
            concurrent_assertion_hook(concurrent_assertion_events.back());
        }
        if (event.action_suppressed)
            continue;
        for (const auto& action : pending.actions) {
            if (action.report) {
                if (report_hook) {
                    report_hook(
                        process, action.text, action.severity,
                        action.source, time, delta);
                }
            } else if (output_hook) {
                output_hook(
                    process, action.text, action.newline, time, delta);
            }
        }
    }
    pending_concurrent_assertions.clear();
}
} // namespace fsim::app
