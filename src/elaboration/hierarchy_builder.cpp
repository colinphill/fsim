// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"
#include "../diagnostic/thread_cpu_clock.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <type_traits>

namespace fsim::elaboration {

namespace {

template <typename Range>
bool same_optional_range(
    const std::optional<Range>& left,
    const std::optional<Range>& right)
{
    return left.has_value() == right.has_value()
        && (!left || std::tie(left->left, left->right, left->descending)
            == std::tie(right->left, right->right, right->descending));
}

bool same_concurrent_signal_layout(
    const SignalInfo& left,
    const SignalInfo& right,
    const Signal& left_runtime,
    const Signal& right_runtime)
{
    if (left.width != right.width || left.type_name != right.type_name
        || left.nominal_type != right.nominal_type
        || left.source_domain != right.source_domain
        || left.resolution != right.resolution
        || left.is_signed != right.is_signed
        || left.systemverilog_scalar != right.systemverilog_scalar
        || left.systemverilog_net_type != right.systemverilog_net_type
        || !same_optional_range(left.packed_range, right.packed_range)
        || !same_optional_range(left.integer_range, right.integer_range)
        || !same_optional_range(
            left.enumeration_range, right.enumeration_range)
        || left.enumeration_literals != right.enumeration_literals
        || static_cast<bool>(left.vhdl_array)
            != static_cast<bool>(right.vhdl_array)
        || left.vhdl_access || right.vhdl_access
        || left.vhdl_physical || right.vhdl_physical
        || !left.packed_members.empty() || !right.packed_members.empty()
        || !left.vhdl_mode_view_bindings.empty()
        || !right.vhdl_mode_view_bindings.empty()
        || left_runtime.value_kind != right_runtime.value_kind
        || left_runtime.resolution != right_runtime.resolution
        || left_runtime.initial_value.width()
            != right_runtime.initial_value.width()) {
        return false;
    }
    if (!left.vhdl_array) {
        return true;
    }
    const auto& a = *left.vhdl_array;
    const auto& b = *right.vhdl_array;
    if (!a.element_types.empty() || !b.element_types.empty()
        || a.index_subtype != b.index_subtype
        || a.element_spelling != b.element_spelling
        || a.element_named_type != b.element_named_type
        || a.element_domain != b.element_domain
        || a.unconstrained != b.unconstrained
        || a.flat_width != b.flat_width
        || !same_optional_range(
            a.index_base_range, b.index_base_range)
        || a.dimensions.size() != b.dimensions.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.dimensions.size(); ++index) {
        const auto& x = a.dimensions[index];
        const auto& y = b.dimensions[index];
        if (x.index_subtype != y.index_subtype
            || !same_optional_range(x.index_base_range, y.index_base_range)
            || !same_optional_range(x.range, y.range)
            || x.null != y.null || x.stride != y.stride
            || x.unconstrained != y.unconstrained) {
            return false;
        }
    }
    return true;
}

// Scalar vhdlconst-v1 identities encode the effective subtype and value.
// Source spans and actual expression IDs are instance provenance, so compare
// the canonical identity and exact binding declaration instead.
bool same_concurrent_scalar_actuals(
    const semantic::SpecializedHirOverlay& first,
    const semantic::SpecializedHirOverlay& second)
{
    if (first.unit != second.unit || first.scope != second.scope
        || first.language != second.language
        || first.hierarchy_identities != second.hierarchy_identities
        || first.residual_expressions != second.residual_expressions
        || first.dependent_generates != second.dependent_generates
        || first.actual_identities.size()
            != second.actual_identities.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.actual_identities.size();
         ++index) {
        const auto& left = first.actual_identities[index];
        const auto& right = second.actual_identities[index];
        if (left.declaration != right.declaration
            || left.identity != right.identity
            || left.actual_declaration != right.actual_declaration) {
            return false;
        }
    }
    return true;
}

bool remap_concurrent_operation(
    Operation& operation,
    const std::map<SignalId, SignalId>& signals,
    const std::string_view from_hierarchy,
    const std::string_view to_hierarchy)
{
    const auto map_signal = [&](SignalId& signal) {
        const auto found = signals.find(signal);
        if (found == signals.end()) {
            return false;
        }
        signal = found->second;
        return true;
    };
    bool accepted { };
    visit_operation([&](auto& value) {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, DebugPoint>) {
            if (!value.scope.empty()) {
                const auto scope = std::string_view { value.scope };
                if (!scope.starts_with(from_hierarchy)
                    || (scope.size() > from_hierarchy.size()
                        && scope[from_hierarchy.size()] != '.')) {
                    return;
                }
                value.scope = std::string { to_hierarchy }
                    + std::string { scope.substr(from_hierarchy.size()) };
            }
            accepted = true;
        } else if constexpr (std::is_same_v<Type, ReadSignal>) {
            accepted = !value.clock && !value.gate
                && map_signal(value.signal);
        } else if constexpr (std::is_same_v<Type, WriteBlocking>
            || std::is_same_v<Type, WriteUpdate>
            || std::is_same_v<Type, WriteBlockingSlice>
            || std::is_same_v<Type, WriteUpdateSlice>
            || std::is_same_v<Type, WriteUpdateDynamicPartSlice>
            || std::is_same_v<Type, WriteProjected>
            || std::is_same_v<Type, WriteProjectedSlice>) {
            accepted = map_signal(value.signal);
        } else {
            accepted = std::is_same_v<Type, WaitSensitivity>
                || std::is_same_v<Type, CopyRegister>
                || std::is_same_v<Type, IntegerCheck>
                || std::is_same_v<Type, LoadConstant>
                || std::is_same_v<Type, DynamicInsert>
                || std::is_same_v<Type, DynamicPartInsert>
                || std::is_same_v<Type, DynamicPartSelect>
                || std::is_same_v<Type, IntegerBinary>
                || std::is_same_v<Type, DynamicExtract>
                || std::is_same_v<Type, CallableFramePop>
                || std::is_same_v<Type, CallableFramePush>
                || std::is_same_v<Type, Call>
                || std::is_same_v<Type, Jump>
                || std::is_same_v<Type, Binary>
                || std::is_same_v<Type, UnaryNot>
                || std::is_same_v<Type, LogicalNot>
                || std::is_same_v<Type, LogicalBinary>
                || std::is_same_v<Type, Reduction>
                || std::is_same_v<Type, Shift>
                || std::is_same_v<Type, Concatenate>
                || std::is_same_v<Type, ConditionalSelect>
                || std::is_same_v<Type, Branch>
                || std::is_same_v<Type, Insert>
                || std::is_same_v<Type, Return>
                || std::is_same_v<Type, Extract>;
        }
    }, operation);
    return accepted;
}

} // namespace

std::optional<Process>
HierarchyBuilder::lower_cached_vhdl_concurrent_statement(
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& owner,
    const semantic::SpecializedHirUnit& specialized,
    Lowerer& lowerer,
    const semantic::StatementId statement,
    const std::string_view path,
    const std::size_t order)
{
    const auto invocation_before =
        lowerer.next_hir_callable_invocation_identity();
    const auto lower_ordinary = [&] {
        ++concurrent_template_rejections_;
        return lowerer.lower_hir_concurrent_statement(
            statement, frontend::Language::Vhdl2008, path, order);
    };
    const auto source = specialized.find_statement(statement);
    if (!source || source->vhdl == nullptr
        || source->vhdl->kind
            != semantic::vhdl::StatementKind::signal_assignment
        || !source->vhdl->target || source->vhdl->guarded_assignment
        || source->vhdl->guard || source->vhdl->disconnection_delay
        || source->vhdl->conditional_assignment
        || source->vhdl->delay || source->vhdl->rejection_limit
        || source->vhdl->unaffected
        || source->vhdl->waveform.size() > 1U
        || (!source->vhdl->waveform.empty()
            && (source->vhdl->waveform.front().delay
                || source->vhdl->waveform.front().disconnect))
        || !specialized.selected_generates().empty()
        || !specialized.vhdl_declarations().empty()
        || !specialized.vhdl_types().empty()
        || !specialized.vhdl_expressions().empty()
        || !specialized.vhdl_statements().empty()
        || !specialized.vhdl_processes().empty()
        || !specialized.vhdl_instances().empty()
        || !specialized.systemverilog_declarations().empty()
        || !specialized.systemverilog_types().empty()
        || !specialized.systemverilog_expressions().empty()
        || !specialized.systemverilog_statements().empty()
        || !specialized.systemverilog_processes().empty()
        || !specialized.systemverilog_instances().empty()) {
        return lower_ordinary();
    }
    const auto target = specialized.find_expression(
        *source->vhdl->target);
    const auto value_id = source->vhdl->waveform.empty()
        ? source->vhdl->value
        : std::optional {
            source->vhdl->waveform.front().value
        };
    const auto value = value_id
        ? specialized.find_expression(*value_id) : std::nullopt;
    if (!target || target->vhdl == nullptr
        || target->vhdl->kind != semantic::vhdl::ExpressionKind::name
        || !target->vhdl->referenced_name
        || !target->vhdl->referenced_name->selected
        || !value || value->vhdl == nullptr
        || value->vhdl->kind != semantic::vhdl::ExpressionKind::call
        || !value->vhdl->referenced_name) {
        return lower_ordinary();
    }
    const auto& call_name = *value->vhdl->referenced_name;
    const auto call_id = call_name.selected
        ? call_name.selected
        : call_name.overloads.size() == 1U
            ? std::optional { call_name.overloads.front() }
            : std::nullopt;
    const auto callable = call_id
        ? specialized.find_declaration(*call_id) : std::nullopt;
    if (!callable || callable->vhdl == nullptr
        || !callable->vhdl->callable
        || !callable->vhdl->callable->function
        || !callable->vhdl->callable->pure) {
        return lower_ordinary();
    }
    for (const auto& actual :
        specialized.specialization().actual_identities) {
        const auto formal = specialized.find_declaration(
            actual.declaration);
        if (!formal || formal->vhdl == nullptr
            || formal->vhdl->form
                != semantic::vhdl::DeclarationForm::generic_constant
            || !formal->vhdl->subtype
            || formal->vhdl->subtype->domain
                != semantic::vhdl::ValueDomain::integer
            || !actual.identity.starts_with("vhdlconst-v1;")
            || actual.vhdl_type || actual.systemverilog_type
            || actual.vhdl_packed_value) {
            return lower_ordinary();
        }
    }

    const auto profile = [&](const SignalId signal) {
        return signal < design_.signals_.size()
            && signal < design_.signal_info_.size();
    };
    const auto equivalent_binding = [&](const SignalId first,
                                        const SignalId second) {
        return profile(first) && profile(second)
            && same_concurrent_signal_layout(
                design_.signal_info_[first], design_.signal_info_[second],
                design_.signals_[first], design_.signals_[second]);
    };
    for (const auto& cached : concurrent_process_templates_) {
        if (cached.unit != owner.id || cached.statement != statement
            || !same_concurrent_scalar_actuals(
                cached.overlay, specialized.specialization())
            || cached.language_standard != owner.standard
            || cached.compatibility_profile
                != owner.compatibility_profile
            || cached.callable_invocation_before
                != invocation_before) {
            continue;
        }
        std::map<SignalId, SignalId> signal_remap;
        std::set<SignalId> distinct_targets;
        bool compatible = true;
        for (std::size_t index = 0; index < cached.formals.size(); ++index) {
            const auto [formal, original] = cached.formals[index];
            const auto actual = lowerer.hir_concurrent_port_signal(formal);
            if (!actual || !equivalent_binding(original, *actual)
                || lowerer.hir_concurrent_signal_read_only(*actual)
                    != cached.read_only_roles[index]
                || !distinct_targets.insert(*actual).second) {
                compatible = false;
                break;
            }
            signal_remap.emplace(original, *actual);
        }
        if (!compatible) {
            continue;
        }
        Process replay = cached.process;
        replay.id = static_cast<ProcessId>(design_.processes_.size());
        const auto old_name_prefix = cached.hierarchy + ".";
        if (!replay.name.starts_with(old_name_prefix)) {
            continue;
        }
        const auto suffix = source->vhdl->label.empty()
            ? "concurrent_" + std::to_string(order)
            : source->vhdl->label;
        replay.name = std::string { path } + "." + suffix;
        OperationList::Storage operations;
        operations.reserve(cached.process.operations.size());
        for (std::size_t index = 0;
             index < cached.process.operations.size(); ++index) {
            auto operation = cached.process.operations.expanded(index);
            if (!remap_concurrent_operation(
                    operation, signal_remap,
                    cached.process.name, replay.name)) {
                compatible = false;
                break;
            }
            operations.push_back(std::move(operation));
        }
        if (!compatible) {
            continue;
        }
        replay.operations = std::move(operations);
        for (auto& sensitivity : replay.static_sensitivity) {
            const auto found = signal_remap.find(sensitivity.signal);
            if (found == signal_remap.end()) {
                compatible = false;
                break;
            }
            sensitivity.signal = found->second;
        }
        if (!compatible) {
            continue;
        }
        std::ranges::sort(replay.static_sensitivity, {},
            [&](const Sensitivity& sensitivity) {
                return std::pair {
                    std::string_view {
                        design_.signal_info_[sensitivity.signal].name },
                    static_cast<std::uint8_t>(sensitivity.edge)
                };
            });
        if (std::ranges::adjacent_find(replay.static_sensitivity)
            != replay.static_sensitivity.end()) {
            continue;
        }
        for (auto& driver : replay.driver_regions) {
            const auto found = signal_remap.find(driver.signal);
            if (found == signal_remap.end()) {
                compatible = false;
                break;
            }
            driver.signal = found->second;
        }
        if (!compatible) {
            continue;
        }
        replay.language_standard = owner.standard;
        replay.compatibility_profile = owner.compatibility_profile;
        if (!lowerer.advance_hir_callable_invocation_identity(
                invocation_before,
                cached.callable_invocation_after)) {
            continue;
        }
        ++concurrent_template_hits_;
        return replay;
    }

    ++concurrent_template_misses_;
    const auto diagnostics_before = diagnostics_.size();
    const bool measure = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    const auto started = measure
        ? diagnostic::thread_cpu_now() : std::nullopt;
    auto lowered = lowerer.lower_hir_concurrent_statement(
        statement, frontend::Language::Vhdl2008, path, order);
    const auto invocation_after =
        lowerer.next_hir_callable_invocation_identity();
    if (const auto elapsed = diagnostic::thread_cpu_elapsed(
            started, measure ? diagnostic::thread_cpu_now() : std::nullopt)) {
        concurrent_template_lower_cpu_ns_ += static_cast<std::uint64_t>(
            elapsed->count());
    }
    if (!lowered || diagnostics_.size() != diagnostics_before
        || lowerer.has_generated_processes()
        || lowered->string_register_count != 0U
        || lowered->container_register_count != 0U
        || !lowered->static_trigger_regions.empty()
        || lowered->driver_regions.size() != 1U
        || !lowered->driver_regions.front().whole
        || invocation_after < invocation_before) {
        return lowered;
    }
    std::map<SignalId, SignalId> formal_signals;
    std::vector<std::pair<semantic::DeclarationId, SignalId>> formals;
    std::vector<bool> read_only_roles;
    for (const auto declaration_id : entity.declarations) {
        const auto declaration = specialized.find_declaration(
            declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || declaration->vhdl->form
                != semantic::vhdl::DeclarationForm::port) {
            continue;
        }
        const auto signal = lowerer.hir_concurrent_port_signal(
            declaration_id);
        if (!signal || !profile(*signal)
            || !formal_signals.emplace(*signal, *signal).second) {
            return lowered;
        }
        formals.emplace_back(declaration_id, *signal);
        read_only_roles.push_back(
            lowerer.hir_concurrent_signal_read_only(*signal));
    }
    if (formals.empty()) {
        return lowered;
    }
    for (std::size_t index = 0; index < lowered->operations.size();
         ++index) {
        auto operation = lowered->operations.expanded(index);
        if (!remap_concurrent_operation(
                operation, formal_signals,
                lowered->name, lowered->name)) {
            return lowered;
        }
    }
    for (const auto& sensitivity : lowered->static_sensitivity) {
        if (!formal_signals.contains(sensitivity.signal)) {
            return lowered;
        }
    }
    for (const auto& driver : lowered->driver_regions) {
        if (!formal_signals.contains(driver.signal)) {
            return lowered;
        }
    }
    lowered->language_standard = owner.standard;
    lowered->compatibility_profile = owner.compatibility_profile;
    concurrent_process_templates_.push_back(ConcurrentProcessTemplate {
        owner.id, statement, specialized.specialization(),
        owner.standard, owner.compatibility_profile,
        std::string { path }, *lowered,
        invocation_before, invocation_after,
        std::move(formals),
        std::move(read_only_roles)
    });
    return lowered;
}

void HierarchyBuilder::canonicalize_process_operations(Process& process)
{
    if (!process_operations_shareable(process)) {
        return;
    }

    ProcessOperationGroupingKey key;
    key.operation_count = process.operations.size();
    key.register_count = process.register_count;
    key.string_register_count = process.string_register_count;
    key.container_register_count = process.container_register_count;
    key.register_value_kinds = process.register_value_kinds;
    key.operation_kinds.reserve(process.operations.size());
    for (const auto& operation : process.operations) {
        key.operation_kinds.emplace_back(
            operation_group_index(operation),
            operation_alternative_index(operation));
    }
    key.sensitivity_edges.reserve(process.static_sensitivity.size());
    for (const auto& sensitivity : process.static_sensitivity) {
        key.sensitivity_edges.push_back(sensitivity.edge);
    }
    key.trigger_regions.reserve(process.static_trigger_regions.size());
    for (const auto& region : process.static_trigger_regions) {
        key.trigger_regions.emplace_back(
            region.begin, region.end, region.mask);
    }
    key.language_standard = process.language_standard;
    key.compatibility_profile = process.compatibility_profile;

    auto& representatives = process_operation_representatives_[key];
    std::erase_if(
        representatives,
        [&](const ProcessId representative) {
            return representative >= design_.processes_.size();
        });
    for (const auto representative : representatives) {
        if (share_process_operations(
                design_.processes_[representative],
                process,
                design_.signals_,
                &operation_scratch_)) {
            return;
        }
    }
    representatives.push_back(process.id);
}

HierarchyBuilder::HierarchyBuilder(
    const semantic::ValidatedCompiledDesign compiled,
    ElaboratedDesign& design,
    std::vector<Diagnostic>& diagnostics,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries)
    : validated_compiled_(compiled),
      compiled_(&compiled.design()),
      design_(design),
      diagnostics_(diagnostics),
      systemc_provider_(systemc_provider),
      systemc_candidates_(
          systemc_provider != nullptr
              ? systemc_provider->candidates()
              : std::vector<SystemCFactoryCandidate>{}),
      systemc_libraries_(
          systemc_provider != nullptr
              ? systemc_provider->libraries()
              : std::vector<std::string>{}),
      search_libraries_(
          search_libraries.begin(), search_libraries.end()) {
    for (const auto& binding : bindings) {
        if (!bindings_.emplace(binding.instance, &binding).second) {
            report(
                "FSIM-ELAB-BIND-010",
                "duplicate binding for instance '" + binding.instance + "'",
                {});
        }
    }
    for (const auto& instance : systemc_instances) {
        if (!systemc_instances_.emplace(instance.path, &instance).second) {
            report(
                "FSIM-ELAB-BIND-032",
                "duplicate constructed SystemC instance path '"
                    + instance.path + "'",
                {});
        }
    }
    std::set<std::pair<std::string, std::string>> factories;
    for (const auto& candidate : systemc_candidates_) {
        if (!factories.emplace(candidate.library, candidate.name).second) {
            report(
                "FSIM-ELAB-BIND-018",
                "duplicate SystemC factory '" + candidate.name
                    + "' in logical library '" + candidate.library + "'",
                {});
        }
    }
    for (const auto& unit : compiled_->systemverilog_units()) {
        if (unit.kind == semantic::sv::UnitKind::package) {
            register_systemverilog_resolution_functions(unit);
        }
    }
    validate_systemverilog_extern_declarations();
}

} // namespace fsim::elaboration
