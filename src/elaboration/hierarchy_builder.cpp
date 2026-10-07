// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "../runtime/simir_operation_list_sharing.hpp"
#include "specialization_cache.hpp"
#include "lowerer_internal.hpp"
#include "../diagnostic/thread_cpu_clock.hpp"

#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>

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

/// A plain Verilog variable or net: reading one is the same operation as
/// reading the other once width, range, signedness and kind agree.
[[nodiscard]] bool plain_verilog_value(const SignalInfo& signal)
{
    return (signal.type_name == "reg" || signal.type_name == "wire")
        && (signal.systemverilog_net_type.empty()
            || signal.systemverilog_net_type == "wire");
}

bool same_vhdl_array_metadata(
    const VhdlArrayMetadata& a, const VhdlArrayMetadata& b);

// Element and member type layouts compare field by field; source spans are
// provenance. Access and physical element types are not compared.
bool same_packed_type_metadata(
    const PackedTypeMetadata& left, const PackedTypeMetadata& right)
{
    if (left.domain != right.domain || left.spelling != right.spelling
        || left.systemverilog_scalar != right.systemverilog_scalar
        || left.systemverilog_net_type != right.systemverilog_net_type
        || left.systemverilog_resolution_function
            != right.systemverilog_resolution_function
        || !same_optional_range(left.packed_range, right.packed_range)
        || left.is_signed != right.is_signed
        || left.named_type != right.named_type
        || !same_optional_range(left.integer_range, right.integer_range)
        || !same_optional_range(
            left.integer_base_range, right.integer_base_range)
        || left.vhdl_integer_storage_width
            != right.vhdl_integer_storage_width
        || left.nominal_type != right.nominal_type
        || left.vhdl_type_declaration != right.vhdl_type_declaration
        || left.vhdl_resolution_function != right.vhdl_resolution_function
        || left.enumeration_literals != right.enumeration_literals
        || !same_optional_range(
            left.enumeration_range, right.enumeration_range)
        || !same_optional_range(
            left.enumeration_base_range, right.enumeration_base_range)
        || static_cast<bool>(left.vhdl_array)
            != static_cast<bool>(right.vhdl_array)
        || left.vhdl_access || right.vhdl_access
        || left.vhdl_physical || right.vhdl_physical
        || left.packed_aggregate != right.packed_aggregate
        || left.packed_members.size() != right.packed_members.size()) {
        return false;
    }
    if (left.vhdl_array
        && !same_vhdl_array_metadata(*left.vhdl_array, *right.vhdl_array)) {
        return false;
    }
    for (std::size_t index = 0; index < left.packed_members.size(); ++index) {
        const auto& x = left.packed_members[index];
        const auto& y = right.packed_members[index];
        if (x.name != y.name || x.domain != y.domain
            || x.spelling != y.spelling
            || !same_optional_range(x.packed_range, y.packed_range)
            || x.is_signed != y.is_signed || x.lsb_offset != y.lsb_offset
            || !std::ranges::equal(x.nested_types, y.nested_types,
                same_packed_type_metadata)) {
            return false;
        }
    }
    return true;
}

bool same_vhdl_array_metadata(
    const VhdlArrayMetadata& a, const VhdlArrayMetadata& b)
{
    if (!std::ranges::equal(a.element_types, b.element_types,
            same_packed_type_metadata)
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

bool same_concurrent_signal_layout(
    const SignalInfo& left,
    const SignalInfo& right,
    const Signal& left_runtime,
    const Signal& right_runtime,
    const bool read_only = false)
{
    // A read-only role only reads the signal, so `reg` and `wire` (and an
    // implicit or explicit `wire` net type) lower identically.
    const bool interchangeable = read_only && plain_verilog_value(left)
        && plain_verilog_value(right);
    if (left.width != right.width
        || (!interchangeable && left.type_name != right.type_name)
        || left.nominal_type != right.nominal_type
        || left.source_domain != right.source_domain
        || left.resolution != right.resolution
        || left.is_signed != right.is_signed
        || left.systemverilog_scalar != right.systemverilog_scalar
        || (!interchangeable
            && left.systemverilog_net_type != right.systemverilog_net_type)
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
    return !left.vhdl_array
        || same_vhdl_array_metadata(*left.vhdl_array, *right.vhdl_array);
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

bool complete_systemverilog_template_actual(
    const semantic::SpecializedHirActualIdentity& actual)
{
    if (actual.vhdl_type || actual.vhdl_packed_value) {
        return false;
    }
    if (actual.systemverilog_type) {
        return actual.identity.starts_with("sv-type-v3;");
    }
    return actual.identity.starts_with("svconst-v3:")
        || actual.identity.starts_with("svscalar-v1:")
        || actual.identity.starts_with("svstring-v1;bytes=");
}

bool same_systemverilog_template_type_actual(
    const std::optional<semantic::sv::TypeReference>& first,
    const std::optional<semantic::sv::TypeReference>& second)
{
    if (first.has_value() != second.has_value()) {
        return false;
    }
    if (!first) {
        return true;
    }

    auto left = *first;
    auto right = *second;
    left.target.source = { };
    right.target.source = { };
    if (left.packed_range) {
        left.packed_range->source = { };
    }
    if (right.packed_range) {
        right.packed_range->source = { };
    }
    return left == right;
}

// HIR actual source spans and expression IDs identify where an association
// came from. They do not affect a fully resolved specialization value. Keep
// every semantic identity, exact generated index, residual dependency and
// selected generate dependency in the key; ignore only that provenance after
// the actual has a complete canonical value or type identity.
bool same_systemverilog_template_overlay(
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
            || !complete_systemverilog_template_actual(left)
            || !complete_systemverilog_template_actual(right)
            || left.identity != right.identity
            || left.actual_declaration != right.actual_declaration
            || !same_systemverilog_template_type_actual(
                left.systemverilog_type, right.systemverilog_type)) {
            return false;
        }
    }
    return true;
}

// A specialization without replacement records is a pure function of its
// overlay, so its generate occurrences are too.
bool overlay_determines_specialization(
    const semantic::SpecializedHirUnit& specialized)
{
    return specialized.systemverilog_declarations().empty()
        && specialized.vhdl_declarations().empty()
        && specialized.systemverilog_types().empty()
        && specialized.vhdl_types().empty()
        && specialized.systemverilog_expressions().empty()
        && specialized.vhdl_expressions().empty()
        && specialized.systemverilog_statements().empty()
        && specialized.vhdl_statements().empty()
        && specialized.systemverilog_processes().empty()
        && specialized.vhdl_processes().empty()
        && specialized.systemverilog_instances().empty()
        && specialized.vhdl_instances().empty();
}

bool same_systemverilog_process_source(
    const semantic::sv::Process& first,
    const semantic::sv::Process& second)
{
    return first.id == second.id
        && first.scope == second.scope
        && first.kind == second.kind
        && first.name == second.name
        && first.source == second.source
        && first.origin == second.origin
        && first.declarations == second.declarations
        && std::ranges::equal(first.sensitivities,
            second.sensitivities, [](const auto& left,
                                      const auto& right) {
                return left.edge == right.edge
                    && left.signal == right.signal
                    && left.expression == right.expression
                    && left.source == right.source;
            })
        && first.statements == second.statements
        && first.concurrent_assertion == second.concurrent_assertion;
}

bool remap_concurrent_operation(
    Operation& operation,
    const std::map<SignalId, SignalId>& signals,
    const std::string_view from_hierarchy,
    const std::string_view to_hierarchy,
    bool* const operation_changed = nullptr)
{
    bool changed { };
    const auto map_signal = [&](SignalId& signal) {
        const auto found = signals.find(signal);
        if (found == signals.end()) {
            return false;
        }
        changed = changed || signal != found->second;
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
                const auto remapped = std::string { to_hierarchy }
                    + std::string { scope.substr(from_hierarchy.size()) };
                changed = changed || value.scope != remapped;
                value.scope = remapped;
            }
            accepted = true;
        } else if constexpr (std::is_same_v<Type, ReadSignal>) {
            accepted = map_signal(value.signal)
                && (!value.clock || map_signal(*value.clock))
                && (!value.gate || map_signal(*value.gate));
        } else if constexpr (std::is_same_v<Type, SignalEvent>
            || std::is_same_v<Type, SignalLastValue>
            || std::is_same_v<Type, SignalLastEvent>
            || std::is_same_v<Type, SignalActive>
            || std::is_same_v<Type, SignalLastActive>
            || std::is_same_v<Type, SignalDriving>
            || std::is_same_v<Type, SignalDrivingValue>) {
            accepted = map_signal(value.signal);
        } else if constexpr (std::is_same_v<Type, WriteBlocking>
            || std::is_same_v<Type, WriteUpdate>
            || std::is_same_v<Type, WriteBlockingSlice>
            || std::is_same_v<Type, WriteUpdateSlice>
            || std::is_same_v<Type, WriteUpdateDynamicPartSlice>
            || std::is_same_v<Type, WriteProjected>
            || std::is_same_v<Type, WriteProjectedSlice>
            || std::is_same_v<Type, WriteProjectedDynamicSlice>
            || std::is_same_v<Type, WriteProjectedWaveformSlice>
            || std::is_same_v<Type, WriteProjectedWaveformDynamicSlice>
            || std::is_same_v<Type, WriteBlockingDynamicSlice>
            || std::is_same_v<Type, WriteUpdateDynamicSlice>) {
            accepted = map_signal(value.signal);
        } else {
            accepted = std::is_same_v<Type, WaitSensitivity>
                || std::is_same_v<Type, CopyRegister>
                || std::is_same_v<Type, IntegerCheck>
                // A source location, a message and a condition register.
                || std::is_same_v<Type, Assert>
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
                // Constant drivers end in Halt (no signal or path).
                || std::is_same_v<Type, Halt>
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
    if (operation_changed != nullptr) {
        *operation_changed = changed;
    }
    return accepted;
}

std::optional<std::vector<SystemVerilogTemplateSignalRole>>
resolve_systemverilog_template_roles(
    const Lowerer& lowerer,
    const std::span<const SystemVerilogTemplateSignalRole> source_roles)
{
    std::map<semantic::DeclarationId,
        std::vector<Lowerer::HirConcurrentContainerElementSignal>>
        element_bindings;
    std::vector<SystemVerilogTemplateSignalRole> target_roles;
    target_roles.reserve(source_roles.size());
    bool compatible = true;
    for (std::size_t index = 0;
         compatible && index < source_roles.size(); ++index) {
        const auto& role = source_roles[index];
        if (role.container_element_ordinal) {
            auto [found, inserted] = element_bindings.try_emplace(
                role.declaration);
            if (inserted) {
                found->second =
                    lowerer.hir_concurrent_container_element_signals(
                        role.declaration);
            }
            const auto ordinal
                = *role.container_element_ordinal;
            if (ordinal < found->second.size()
                && found->second[ordinal].ordinal == ordinal) {
                const auto& element = found->second[ordinal];
                target_roles.push_back({
                    role.declaration,
                    element.signal,
                    ordinal,
                    element.container_type,
                    element.read_only,
                    element.readable,
                    element.writable,
                });
            } else {
                compatible = false;
            }
        } else {
            if (role.container_type) {
                compatible = false;
                break;
            }
            const auto actual = lowerer.hir_concurrent_port_signal(
                role.declaration);
            if (!actual) {
                compatible = false;
                break;
            }
            const auto read_only
                = lowerer.hir_concurrent_signal_read_only(*actual);
            target_roles.push_back({
                role.declaration,
                *actual,
                std::nullopt,
                std::nullopt,
                read_only,
                true,
                !read_only,
            });
        }
    }
    if (!compatible) {
        return std::nullopt;
    }
    return target_roles;
}

} // namespace

std::optional<std::map<SignalId, SignalId>>
make_systemverilog_template_signal_remap(
    const std::span<const SystemVerilogTemplateSignalRole> source_roles,
    const std::span<const SystemVerilogTemplateSignalRole> target_roles,
    const std::span<const SignalInfo> signal_info,
    const std::span<const Signal> signals)
{
    using RoleKey = std::pair<std::uint32_t,
                              std::optional<std::uint32_t>>;
    if (source_roles.empty() || source_roles.size() != target_roles.size()) {
        return std::nullopt;
    }

    std::map<RoleKey, const SystemVerilogTemplateSignalRole*> targets;
    for (const auto& target : target_roles) {
        if (target.container_element_ordinal.has_value()
                != target.container_type.has_value()
            || !targets.emplace(
                    RoleKey { target.declaration.value(),
                        target.container_element_ordinal },
                    &target).second) {
            return std::nullopt;
        }
    }

    std::map<SignalId, SignalId> remap;
    std::set<SignalId> source_signals;
    std::set<SignalId> target_signals;
    std::set<RoleKey> source_keys;
    for (const auto& source : source_roles) {
        const RoleKey key {
            source.declaration.value(),
            source.container_element_ordinal,
        };
        const auto target = targets.find(key);
        if (!source_keys.insert(key).second
            || target == targets.end()
            || source.container_element_ordinal.has_value()
                != source.container_type.has_value()
            || source.container_element_ordinal
                != target->second->container_element_ordinal
            || source.container_type != target->second->container_type
            || source.read_only != target->second->read_only
            || source.readable != target->second->readable
            || source.writable != target->second->writable
            || source.signal >= signal_info.size()
            || source.signal >= signals.size()
            || target->second->signal >= signal_info.size()
            || target->second->signal >= signals.size()
            || !same_concurrent_signal_layout(
                signal_info[source.signal],
                signal_info[target->second->signal],
                signals[source.signal],
                signals[target->second->signal],
                source.read_only && !source.writable
                    && target->second->read_only
                    && !target->second->writable)
            || !source_signals.insert(source.signal).second
            || !target_signals.insert(target->second->signal).second) {
            return std::nullopt;
        }
        remap.emplace(source.signal, target->second->signal);
    }
    if (source_keys.size() != targets.size()) {
        return std::nullopt;
    }
    return remap;
}

template<class ProcessInstance>
bool remap_systemverilog_template_process(
    ProcessInstance& process,
    const std::map<SignalId, SignalId>& signal_remap,
    const std::string_view from_hierarchy,
    const std::string_view to_hierarchy,
    const std::span<const SignalInfo> signal_info)
{
    if (process.switch_source || process.switch_target
        || process.switch_control) {
        return false;
    }

    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        // A template's operations all passed remap_concurrent_operation when
        // it was cached; only signal operands and debug scopes change.
        const auto& stored = std::as_const(process.operations)[index];
        const bool remaps = visit_operation([](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            return std::is_same_v<Type, DebugPoint>
                || std::is_same_v<Type, ReadSignal>
                || std::is_same_v<Type, WriteBlocking>
                || std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteBlockingSlice>
                || std::is_same_v<Type, WriteUpdateSlice>
                || std::is_same_v<Type, WriteUpdateDynamicPartSlice>
                || std::is_same_v<Type, WriteProjected>
                || std::is_same_v<Type, WriteProjectedSlice>;
        }, stored);
        if (!remaps) {
            continue;
        }
        // A debug point without an instruction override is remapped from
        // its canonical (body) scope: an earlier point of the same scope may
        // already have remapped it, which expanded() would apply.
        const bool canonical_point = operation_holds<DebugPoint>(stored)
            && !runtime::simir::operation_list_detail::ShareAccess::
                has_instruction_override(process.operations, index);
        auto operation = canonical_point ? Operation { stored }
                                         : process.operations.expanded(index);
        bool changed { };
        if (!remap_concurrent_operation(
                operation, signal_remap, from_hierarchy, to_hierarchy,
                &changed)) {
            return false;
        }
        if (!changed) {
            continue;
        }
        const auto* const point = operation_get_if<DebugPoint>(&operation);
        const auto* const canonical = operation_get_if<DebugPoint>(&stored);
        if (canonical_point && point != nullptr
            && point->kind == canonical->kind
            && point->source == canonical->source) {
            // Only the scope differs: remap the body's scope instead of
            // overriding the operation.
            runtime::simir::operation_list_detail::ShareAccess::
                remap_debug_scope(process.operations, canonical->scope,
                    point->scope);
            continue;
        }
        process.operations.replace(index, std::move(operation));
    }

    auto sensitivities = process.static_sensitivity;
    for (auto& sensitivity : sensitivities) {
        const auto found = signal_remap.find(sensitivity.signal);
        if (found == signal_remap.end()
            || found->second >= signal_info.size()) {
            return false;
        }
        sensitivity.signal = found->second;
    }
    normalize_sensitivities(sensitivities);
    std::ranges::sort(sensitivities, {},
        [&](const Sensitivity& sensitivity) {
            return std::tuple {
                std::string_view {
                    signal_info[sensitivity.signal].name },
                static_cast<std::uint8_t>(sensitivity.edge),
                sensitivity.offset,
                sensitivity.width,
            };
        });
    if (std::ranges::adjacent_find(sensitivities)
        != sensitivities.end()) {
        return false;
    }

    auto drivers = process.driver_regions;
    for (auto& driver : drivers) {
        const auto found = signal_remap.find(driver.signal);
        if (found == signal_remap.end()) {
            return false;
        }
        driver.signal = found->second;
    }

    process.static_sensitivity = std::move(sensitivities);
    process.driver_regions = std::move(drivers);
    return true;
}

bool remap_systemverilog_template_process(
    Process& process,
    const std::map<SignalId, SignalId>& signal_remap,
    const std::string_view from_hierarchy,
    const std::string_view to_hierarchy,
    const std::span<const SignalInfo> signal_info)
{
    return remap_systemverilog_template_process<Process>(
        process, signal_remap, from_hierarchy, to_hierarchy, signal_info);
}

std::uint32_t HierarchyBuilder::overlay_class(
    const semantic::SpecializedHirUnit& specialized)
{
    auto& cache = specialization_cache(specialized);
    if (cache.overlay_class != SpecializationCache::unassigned) {
        return cache.overlay_class;
    }
    const auto& overlay = specialized.specialization();
    // An overlay with an incomplete actual matches nothing, itself included.
    if (!same_systemverilog_template_overlay(overlay, overlay)) {
        cache.overlay_class = no_overlay_class;
        return no_overlay_class;
    }
    for (std::uint32_t index = 0U; index < overlay_classes_.size(); ++index) {
        if (same_systemverilog_template_overlay(overlay_classes_[index], overlay)) {
            cache.overlay_class = index;
            return index;
        }
    }
    cache.overlay_class = static_cast<std::uint32_t>(overlay_classes_.size());
    overlay_classes_.push_back(overlay);
    return cache.overlay_class;
}

hierarchy_sv_generate_detail::CollectionResult
HierarchyBuilder::collect_generate_occurrences(const semantic::sv::Unit& unit,
    const std::string& path,
    const semantic::SpecializedHirUnit& specialized)
{
    if (unit.generates.empty()
        || !overlay_determines_specialization(specialized)) {
        return hierarchy_sv_generate_detail::collect_occurrences(
            unit, path, specialized);
    }
    const auto relocate = [&](std::vector<
                                  hierarchy_sv_generate_detail::Occurrence>
                                  occurrences) {
        for (auto& occurrence : occurrences) {
            if (occurrence.path.empty()) {
                occurrence.path = path;
            } else if (!path.empty()) {
                occurrence.path = path + "." + occurrence.path;
            }
        }
        return occurrences;
    };
    const auto overlay = overlay_class(specialized);
    auto& templates = generate_occurrence_templates_[unit.id];
    for (const auto& cached : templates) {
        if (overlay != no_overlay_class && cached.overlay_class == overlay) {
            return { relocate(cached.occurrences), std::nullopt };
        }
    }
    auto collected = hierarchy_sv_generate_detail::collect_occurrences(
        unit, {}, specialized);
    if (collected.diagnostic) {
        return hierarchy_sv_generate_detail::collect_occurrences(
            unit, path, specialized);
    }
    templates.push_back({ overlay, collected.occurrences });
    collected.occurrences = relocate(std::move(collected.occurrences));
    return collected;
}

std::optional<HierarchyBuilder::VhdlProcessOccurrence>
HierarchyBuilder::lower_cached_vhdl_occurrence(
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& owner,
    const semantic::SpecializedHirUnit& specialized,
    Lowerer& lowerer,
    const semantic::StatementId statement,
    const std::string_view path,
    const std::size_t order,
    const std::optional<semantic::ProcessId> process_source,
    const std::string_view generate_relative_discriminator,
    const std::span<const semantic::DeclarationId> additional_declarations)
{
    record_lowering_census(process_source
        ? vhdl_process_template_occurrences_
        : vhdl_concurrent_template_occurrences_);
    const auto lower_occurrence = [&] {
        if (process_source) {
            record_lowering_census(vhdl_process_lower_requests_);
            return lowerer.lower_hir_process(
                *process_source, frontend::Language::Vhdl2008, path);
        }
        return lowerer.lower_hir_concurrent_statement(
            statement, frontend::Language::Vhdl2008, path, order);
    };
    const auto invocation_before =
        lowerer.next_hir_callable_invocation_identity();
    const auto lower_ordinary = [&] {
        record_lowering_census(process_source
            ? vhdl_process_template_rejections_
            : concurrent_template_rejections_);
        return lower_occurrence();
    };
    const auto source = process_source
        ? std::nullopt : specialized.find_statement(statement);
    const bool concurrent_source_supported = !(
        !source || source->vhdl == nullptr
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
                || source->vhdl->waveform.front().disconnect)));
    const auto process = process_source
        ? specialized.find_process(*process_source) : std::nullopt;
    const auto selected_generates = specialized.selected_generates();
    const bool has_source = process_source
        ? process && process->vhdl != nullptr
        : source && source->vhdl != nullptr;
    if (!has_source) {
        return lower_ordinary();
    }
    // Scalar entries also match other instances whose scalar generics have
    // equal values; exact entries need an equal overlay, which determines
    // the lowering completely, so they admit any occurrence.
    bool scalar = true;
    if ((!process_source && !concurrent_source_supported)
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
        scalar = false;
    }
    if (scalar && !process_source) {
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
            scalar = false;
        } else {
            const auto& call_name = *value->vhdl->referenced_name;
            const auto call_id = call_name.selected
                ? call_name.selected
                : call_name.overloads.size() == 1U
                    ? std::optional { call_name.overloads.front() }
                    : std::nullopt;
            const auto callable = call_id
                ? specialized.find_declaration(*call_id) : std::nullopt;
            scalar = callable && callable->vhdl != nullptr
                && callable->vhdl->callable
                && callable->vhdl->callable->function
                && callable->vhdl->callable->pure;
        }
    }
    for (const auto& actual :
        specialized.specialization().actual_identities) {
        if (!scalar) {
            break;
        }
        const auto formal = specialized.find_declaration(
            actual.declaration);
        scalar = !(!formal || formal->vhdl == nullptr
            || formal->vhdl->form
                != semantic::vhdl::DeclarationForm::generic_constant
            || !formal->vhdl->subtype
            || (formal->vhdl->subtype->domain
                    != semantic::vhdl::ValueDomain::integer
                && formal->vhdl->subtype->domain
                    != semantic::vhdl::ValueDomain::boolean)
            || !actual.identity.starts_with("vhdlconst-v1;")
            || actual.vhdl_type || actual.systemverilog_type
            || actual.vhdl_packed_value);
    }
    // Scalar entries are shared across overlays, so a generated occurrence
    // needs its generate position; an exact overlay already fixes the
    // selected generate branches.
    if (!selected_generates.empty() && generate_relative_discriminator.empty()) {
        scalar = false;
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
    const auto template_key = std::tuple {
        owner.id.value(), statement.value(),
        process_source ? process_source->value() + 1U : 0U,
        std::string { generate_relative_discriminator }
    };
    const auto indexed = concurrent_process_template_index_.find(template_key);
    const auto candidates = indexed == concurrent_process_template_index_.end()
        ? std::span<const std::size_t> { }
        : std::span<const std::size_t> { indexed->second };
    for (const auto candidate : candidates) {
        const auto& cached = concurrent_process_templates_[candidate];
        if (cached.unit != owner.id || cached.statement != statement
            || cached.process_source != process_source
            || cached.generate_relative_discriminator
                != generate_relative_discriminator
            || !std::ranges::equal(
                cached.selected_generates, selected_generates)
            || (cached.exact
                    ? cached.overlay != specialized.specialization()
                    : !scalar
                        || !same_concurrent_scalar_actuals(
                            cached.overlay, specialized.specialization()))
            || cached.language_standard != owner.standard
            || cached.compatibility_profile
                != owner.compatibility_profile
            || cached.common == nullptr
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
        const auto remap_replay = [&]<typename ProcessRecord>(
                                      ProcessRecord replay,
                                      const std::string& cached_name)
            -> std::optional<ProcessRecord> {
            replay.id = static_cast<ProcessId>(design_.process_count());
            const auto old_name_prefix = cached.hierarchy + ".";
            const bool unnamed_process = process_source
                && cached_name == cached.hierarchy;
            if (!unnamed_process
                && !replay.name.starts_with(old_name_prefix)) {
                return std::nullopt;
            }
            if (unnamed_process) {
                replay.name = std::string { path };
            } else {
                const auto suffix = process_source
                    ? cached_name.substr(old_name_prefix.size())
                    : source->vhdl->label.empty()
                    ? "concurrent_" + std::to_string(order)
                    : source->vhdl->label;
                replay.name = std::string { path } + "." + suffix;
            }
            for (std::size_t index = 0;
                 index < replay.operations.size(); ++index) {
                auto operation = replay.operations.expanded(index);
                bool changed { };
                if (!remap_concurrent_operation(
                        operation, signal_remap,
                        cached_name, replay.name, &changed)) {
                    return std::nullopt;
                }
                if (changed) {
                    replay.operations.replace(index, std::move(operation));
                }
            }
            for (auto& sensitivity : replay.static_sensitivity) {
                const auto found = signal_remap.find(sensitivity.signal);
                if (found == signal_remap.end()) {
                    return std::nullopt;
                }
                sensitivity.signal = found->second;
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
                return std::nullopt;
            }
            for (auto& driver : replay.driver_regions) {
                const auto found = signal_remap.find(driver.signal);
                if (found == signal_remap.end()) {
                    return std::nullopt;
                }
                driver.signal = found->second;
            }
            return replay;
        };
        auto replay_instance = remap_replay(
            cached.instance, cached.instance.name);
        if (!replay_instance) {
            continue;
        }
        if (!lowerer.advance_hir_callable_invocation_identity(
                invocation_before,
                cached.callable_invocation_after)) {
            continue;
        }
        record_lowering_census(process_source
            ? vhdl_process_template_hits_ : concurrent_template_hits_);
        return VhdlProcessOccurrence {
            cached.common, std::move(*replay_instance)
        };
    }

    record_lowering_census(process_source
        ? vhdl_process_template_misses_ : concurrent_template_misses_);
    const auto diagnostics_before = diagnostics_.size();
    const bool measure = lowering_census_enabled_;
    const auto started = measure
        ? diagnostic::thread_cpu_now() : std::nullopt;
    auto lowered = lower_occurrence();
    if (process_source && lowered && lowered->name == "<process>") {
        lowered->name = std::string { path };
    }
    const auto invocation_after =
        lowerer.next_hir_callable_invocation_identity();
    if (const auto elapsed = diagnostic::thread_cpu_elapsed(
            started, measure ? diagnostic::thread_cpu_now() : std::nullopt)) {
        auto& lower_cpu_ns = process_source
            ? vhdl_process_template_lower_cpu_ns_
            : concurrent_template_lower_cpu_ns_;
        lower_cpu_ns += static_cast<std::uint64_t>(elapsed->count());
    }
    if (!lowered || diagnostics_.size() != diagnostics_before
        || lowerer.has_generated_processes()
        || lowered->string_register_count != 0U
        || lowered->container_register_count != 0U
        || !lowered->static_trigger_regions.empty()
        || invocation_after < invocation_before
        || (process_source
            && (lowered->static_sensitivity.empty()
                || !lowered->debug_string_locals.empty()
                || !lowered->debug_container_locals.empty()
                || lowered->switch_source || lowered->switch_target
                || lowered->switch_control))) {
        return lowered;
    }
    // Scalar entries keep the original restrictions; an occurrence they
    // exclude is stored as an exact entry instead.
    const auto scalar_admissible = [&] {
        if (process_source && !lowered->debug_locals.empty()) {
            return false;
        }
        if (lowered->driver_regions.empty()) {
            return false;
        }
        if (!process_source) {
            return lowered->driver_regions.size() == 1U
                && lowered->driver_regions.front().whole;
        }
        const auto signal = lowered->driver_regions.front().signal;
        return std::ranges::all_of(
                   lowered->driver_regions,
                   [&](const Process::DriverRegion& region) {
                       return region.signal == signal;
                   })
            && std::ranges::any_of(
                lowered->driver_regions,
                [](const Process::DriverRegion& region) {
                    return region.whole;
                });
    };
    std::map<SignalId, SignalId> formal_signals;
    std::vector<std::pair<semantic::DeclarationId, SignalId>> formals;
    std::vector<bool> read_only_roles;
    // The signals an entry rebinds, and whether every operation, sensitivity
    // and driver of the lowered process refers only to them.
    const auto collect_formals = [&](const bool exact) {
        formal_signals.clear();
        formals.clear();
        read_only_roles.clear();
        std::set<semantic::DeclarationId> appended_declarations;
        const auto append_signal_binding = [&](
            const semantic::DeclarationId declaration_id) {
            const auto declaration = specialized.find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                return false;
            }
            const auto form = declaration->vhdl->form;
            if (form != semantic::vhdl::DeclarationForm::port
                && ((!exact && generate_relative_discriminator.empty())
                    || form != semantic::vhdl::DeclarationForm::signal)) {
                return true;
            }
            const auto signal = lowerer.hir_concurrent_port_signal(
                declaration_id);
            if (!signal || !profile(*signal)
                || !formal_signals.emplace(*signal, *signal).second) {
                return false;
            }
            formals.emplace_back(declaration_id, *signal);
            read_only_roles.push_back(
                lowerer.hir_concurrent_signal_read_only(*signal));
            return true;
        };
        for (const auto declaration_id : entity.declarations) {
            const auto declaration = specialized.find_declaration(
                declaration_id);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::port
                && appended_declarations.insert(declaration_id).second
                && !append_signal_binding(declaration_id)) {
                return false;
            }
        }
        for (const auto declaration_id : additional_declarations) {
            if (appended_declarations.insert(declaration_id).second
                && !append_signal_binding(declaration_id)) {
                return false;
            }
        }
        // An exact replay also rebinds the owning unit's own signals.
        for (const auto declaration_id : exact
                 ? std::span<const semantic::DeclarationId> {
                       owner.declarations }
                 : std::span<const semantic::DeclarationId> { }) {
            if (appended_declarations.insert(declaration_id).second
                && !append_signal_binding(declaration_id)) {
                return false;
            }
        }
        if (formals.empty()) {
            return false;
        }
        for (std::size_t index = 0; index < lowered->operations.size();
             ++index) {
            auto operation = lowered->operations.expanded(index);
            if (process_source && !exact
                && (operation_holds<Call>(operation)
                    || operation_holds<CallableFramePush>(operation)
                    || operation_holds<CallableFramePop>(operation))) {
                return false;
            }
            if (!remap_concurrent_operation(
                    operation, formal_signals,
                    lowered->name, lowered->name)) {
                return false;
            }
        }
        return std::ranges::all_of(lowered->static_sensitivity,
                   [&](const Sensitivity& sensitivity) {
                       return formal_signals.contains(sensitivity.signal);
                   })
            && std::ranges::all_of(lowered->driver_regions,
                [&](const Process::DriverRegion& driver) {
                    return formal_signals.contains(driver.signal);
                });
    };
    bool exact = false;
    if (!(scalar && scalar_admissible() && collect_formals(false))) {
        if (!collect_formals(true)) {
            return lowered;
        }
        exact = true;
    }
    lowered->language_standard = owner.standard;
    lowered->compatibility_profile = owner.compatibility_profile;
    auto common = design_.intern_process_template(
        ProcessProgramView { *lowered });
    ProcessInstanceProgram instance {
        ProcessProgramView { *lowered }
    };
    concurrent_process_template_index_[template_key].push_back(
        concurrent_process_templates_.size());
    concurrent_process_templates_.push_back(ConcurrentProcessTemplate {
        owner.id, statement, specialized.specialization(),
        owner.standard, owner.compatibility_profile,
        std::string { path }, common, instance,
        invocation_before, invocation_after,
        std::move(formals),
        std::move(read_only_roles), process_source,
        std::string { generate_relative_discriminator },
        std::vector<semantic::DeclarationId>(
            selected_generates.begin(), selected_generates.end()),
        exact
    });
    return VhdlProcessOccurrence {
        std::move(common), std::move(instance)
    };
}

std::optional<HierarchyBuilder::SystemVerilogConcurrentProcessOccurrence>
HierarchyBuilder::lower_cached_systemverilog_concurrent_statement(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    Lowerer& lowerer,
    const semantic::StatementId statement,
    const frontend::Language source_language,
    const std::span<const semantic::DeclarationId> signal_declarations,
    const std::string_view path,
    const std::size_t order,
    const std::optional<std::uint32_t> program_owner)
{
    record_lowering_census(systemverilog_concurrent_occurrences_);
    const auto retain_full_process = [](
                                         std::optional<Process> process)
        -> std::optional<SystemVerilogConcurrentProcessOccurrence> {
        if (!process) {
            return std::nullopt;
        }
        return SystemVerilogConcurrentProcessOccurrence {
            std::move(*process)
        };
    };
    static const bool debug_reject = std::getenv("FSIM_DEBUG_LOWERING_REJECT") != nullptr;
    const auto why = [&](const char* reason) {
        if (debug_reject) {
            std::cerr << "fsim-lowering-reject: " << reason << ' ' << path << '\n';
        }
    };
    const auto lower_ordinary = [&] {
        record_lowering_census(
            systemverilog_concurrent_template_rejections_);
        return retain_full_process(
            lowerer.lower_hir_concurrent_statement(
                statement, source_language, path, order));
    };
    const auto source = specialized.find_statement(statement);
    if (!source || source->systemverilog == nullptr) {
        why("no_source");
        return lower_ordinary();
    }
    const auto& assignment = *source->systemverilog;
    if (assignment.kind != semantic::sv::StatementKind::assignment
        || assignment.assignment_kind
            != semantic::sv::AssignmentKind::continuous
        || !assignment.target || !assignment.value || assignment.delay
        || assignment.drive_zero || assignment.drive_one
        || assignment.verilog_switch_driver
        || assignment.verilog_switch_bidirectional
        || assignment.verilog_switch_resistive
        || assignment.verilog_switch_source
        || assignment.verilog_switch_control
        || assignment.assignment_control
            != semantic::sv::AssignmentControl::none
        || assignment.assignment_control_repeated
        || assignment.update_kind != semantic::sv::UpdateKind::none
        || !assignment.update_operator.empty()
        || assignment.clocking_cycle_delay
        || assignment.clocking_cycle_count
        || !assignment.statements.empty()
        || !assignment.else_statements.empty()) {
        why(assignment.kind != semantic::sv::StatementKind::assignment ? "not_assignment"
            : assignment.assignment_kind != semantic::sv::AssignmentKind::continuous ? "not_continuous"
            : "shape");
        return lower_ordinary();
    }

    using TemplateKey = SystemVerilogConcurrentProcessTemplateKey;
    const auto overlay = overlay_class(specialized);
    const TemplateKey key { unit.id, statement, source_language, overlay };
    const auto cached_templates = overlay == no_overlay_class
        ? systemverilog_concurrent_process_templates_.end()
        : systemverilog_concurrent_process_templates_.find(key);
    bool matching_overlay { };
    if (cached_templates
        != systemverilog_concurrent_process_templates_.end()) {
        for (const auto& cached : cached_templates->second) {
            if (overlay == no_overlay_class || cached.overlay_class != overlay
                || cached.language_standard != unit.standard
                || cached.compatibility_profile
                    != unit.compatibility_profile) {
                why(overlay == no_overlay_class ? "no_overlay" : "overlay_differs");
                continue;
            }
            matching_overlay = true;
            const auto target_roles = resolve_systemverilog_template_roles(
                lowerer, cached.signal_roles);
            if (!target_roles) {
                why("roles");
                continue;
            }
            auto signal_remap = make_systemverilog_template_signal_remap(
                cached.signal_roles,
                *target_roles,
                design_.signal_info_,
                design_.signals_);
            if (!signal_remap) {
                why("remap");
                continue;
            }

            const auto old_name_prefix = cached.hierarchy + ".";
            const auto suffix = assignment.label.empty()
                ? "concurrent_" + std::to_string(order)
                : assignment.label;
            const auto replay_name = std::string { path } + "." + suffix;
            if (!cached.common) {
                continue;
            }
            ProcessInstanceProgram replay = cached.instance;
            replay.id = static_cast<ProcessId>(design_.process_count());
            if (!replay.name.starts_with(old_name_prefix)) {
                continue;
            }
            replay.name = replay_name;
            if (!remap_systemverilog_template_process(
                    replay, *signal_remap, cached.instance.name,
                    replay.name, design_.signal_info_)) {
                continue;
            }
            replay.reactive = program_owner.has_value();
            replay.program_owner = program_owner;
            record_lowering_census(
                systemverilog_concurrent_template_hits_);
            record_lowering_census(
                systemverilog_concurrent_templates_replayed_);
            return SystemVerilogConcurrentProcessOccurrence {
                cached.common, std::move(replay)
            };
        }
    }
    if (!matching_overlay) {
        record_lowering_census(systemverilog_concurrent_template_misses_);
    } else {
        record_lowering_census(
            systemverilog_concurrent_template_rejections_);
    }

    const auto diagnostics_before = diagnostics_.size();
    const auto invocation_before
        = lowerer.next_hir_callable_invocation_identity();
    const bool measure = lowering_census_enabled_;
    const auto started = measure
        ? diagnostic::thread_cpu_now() : std::nullopt;
    auto lowered = lowerer.lower_hir_concurrent_statement(
        statement, source_language, path, order);
    const auto invocation_after
        = lowerer.next_hir_callable_invocation_identity();
    if (const auto elapsed = diagnostic::thread_cpu_elapsed(
            started, measure ? diagnostic::thread_cpu_now() : std::nullopt)) {
        concurrent_template_lower_cpu_ns_ += static_cast<std::uint64_t>(
            elapsed->count());
    }
    if (!lowered) {
        record_lowering_census(systemverilog_concurrent_template_rejections_);
        return retain_full_process(std::move(lowered));
    }

    const auto expected_domain
        = (source_language == frontend::Language::SystemVerilog2017
            || source_language == frontend::Language::Verilog2005)
        ? ProcessSchedulingDomain::systemverilog
        : ProcessSchedulingDomain::generic;
    if (diagnostics_.size() != diagnostics_before
        || invocation_after != invocation_before
        || lowerer.has_generated_processes()
        || lowered->scheduling_domain != expected_domain
        || lowered->string_register_count != 0U
        || lowered->container_register_count != 0U
        || !lowered->debug_locals.empty()
        || !lowered->debug_string_locals.empty()
        || !lowered->debug_container_locals.empty()
        || !lowered->static_trigger_regions.empty()
        || lowered->driver_regions.size() != 1U
        || (!lowered->driver_regions.front().whole
            && lowered->driver_regions.front().width == 0U)
        || lowered->switch_source || lowered->switch_target
        || lowered->switch_control) {
        record_lowering_census(systemverilog_concurrent_template_rejections_);
        return retain_full_process(std::move(lowered));
    }

    const auto profile = [&](const SignalId signal) {
        return signal < design_.signals_.size()
            && signal < design_.signal_info_.size();
    };
    std::set<SignalId> used_signals;
    const auto collect_operation_signals = [&](const Operation& operation) {
        visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, ReadSignal>
                || std::is_same_v<Type, WriteBlocking>
                || std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteBlockingSlice>
                || std::is_same_v<Type, WriteUpdateSlice>
                || std::is_same_v<Type, WriteUpdateDynamicPartSlice>
                || std::is_same_v<Type, WriteProjected>
                || std::is_same_v<Type, WriteProjectedSlice>) {
                used_signals.insert(value.signal);
            }
        }, operation);
    };
    for (std::size_t index = 0; index < lowered->operations.size(); ++index) {
        auto operation = lowered->operations.expanded(index);
        collect_operation_signals(operation);
    }
    for (const auto& sensitivity : lowered->static_sensitivity) {
        used_signals.insert(sensitivity.signal);
    }
    for (const auto& driver : lowered->driver_regions) {
        used_signals.insert(driver.signal);
    }
    std::map<SignalId, SignalId> formal_signals;
    std::vector<SystemVerilogTemplateSignalRole> signal_roles;
    for (const auto declaration_id : signal_declarations) {
        const auto declaration = specialized.find_declaration(
            declaration_id);
        if (!declaration || declaration->systemverilog == nullptr) {
            continue;
        }
        const auto signal = lowerer.hir_concurrent_port_signal(
            declaration_id);
        if (signal) {
            if (!used_signals.contains(*signal)) {
                continue;
            }
            if (!profile(*signal)
                || !formal_signals.emplace(*signal, *signal).second) {
                record_lowering_census(
                    systemverilog_concurrent_template_rejections_);
                return retain_full_process(std::move(lowered));
            }
            const auto read_only
                = lowerer.hir_concurrent_signal_read_only(*signal);
            signal_roles.push_back({
                declaration_id,
                *signal,
                std::nullopt,
                std::nullopt,
                read_only,
                true,
                !read_only,
            });
            continue;
        }
        for (const auto& element :
            lowerer.hir_concurrent_container_element_signals(
                declaration_id)) {
            if (!used_signals.contains(element.signal)) {
                continue;
            }
            if (!profile(element.signal)
                || !formal_signals.emplace(
                        element.signal, element.signal).second) {
                record_lowering_census(
                    systemverilog_concurrent_template_rejections_);
                return retain_full_process(std::move(lowered));
            }
            signal_roles.push_back({
                declaration_id,
                element.signal,
                element.ordinal,
                element.container_type,
                element.read_only,
                element.readable,
                element.writable,
            });
        }
    }
    if (signal_roles.empty()) {
        record_lowering_census(systemverilog_concurrent_template_rejections_);
        return retain_full_process(std::move(lowered));
    }
    for (std::size_t index = 0; index < lowered->operations.size();
         ++index) {
        auto operation = lowered->operations.expanded(index);
        if (operation_holds<Call>(operation)
            || operation_holds<CallableFramePush>(operation)
            || operation_holds<CallableFramePop>(operation)
            || !remap_concurrent_operation(
                operation, formal_signals,
                lowered->name, lowered->name)) {
            record_lowering_census(
                systemverilog_concurrent_template_rejections_);
            return retain_full_process(std::move(lowered));
        }
    }
    for (const auto& sensitivity : lowered->static_sensitivity) {
        if (!formal_signals.contains(sensitivity.signal)) {
            record_lowering_census(
                systemverilog_concurrent_template_rejections_);
            return retain_full_process(std::move(lowered));
        }
    }
    for (const auto& driver : lowered->driver_regions) {
        if (!formal_signals.contains(driver.signal)) {
            record_lowering_census(
                systemverilog_concurrent_template_rejections_);
            return retain_full_process(std::move(lowered));
        }
    }

    lowered->language_standard = unit.standard;
    lowered->compatibility_profile = unit.compatibility_profile;
    record_lowering_census(systemverilog_concurrent_templates_lowered_);
    auto common = design_.intern_process_template(
        ProcessProgramView { *lowered });
    ProcessInstanceProgram instance {
        ProcessProgramView { *lowered }
    };
    systemverilog_concurrent_process_templates_[key].push_back(
        SystemVerilogConcurrentProcessTemplate {
            specialized.specialization(), unit.standard,
            unit.compatibility_profile, std::string { path },
            common, instance, std::move(signal_roles)
        });
    systemverilog_concurrent_process_templates_[key].back().overlay_class
        = overlay;
    return SystemVerilogConcurrentProcessOccurrence {
        std::move(common), std::move(instance)
    };
}

std::optional<std::vector<SystemVerilogTemplateSignalRole>>
HierarchyBuilder::systemverilog_process_signal_roles(
    const Process& process,
    const Lowerer& lowerer,
    const semantic::SpecializedHirUnit& specialized,
    const std::span<const semantic::DeclarationId> signal_declarations) const
{
    std::set<SignalId> used_signals;
    const auto collect_operation_signals =
        [&](const Operation& operation) {
            visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, ReadSignal>
                    || std::is_same_v<Type, WriteBlocking>
                    || std::is_same_v<Type, WriteUpdate>
                    || std::is_same_v<Type, WriteBlockingSlice>
                    || std::is_same_v<Type, WriteUpdateSlice>
                    || std::is_same_v<Type,
                        WriteUpdateDynamicPartSlice>
                    || std::is_same_v<Type, WriteProjected>
                    || std::is_same_v<Type, WriteProjectedSlice>) {
                    used_signals.insert(value.signal);
                }
            }, operation);
        };
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        collect_operation_signals(operation);
    }
    for (const auto& sensitivity : process.static_sensitivity) {
        used_signals.insert(sensitivity.signal);
    }
    for (const auto& driver : process.driver_regions) {
        used_signals.insert(driver.signal);
    }
    if (used_signals.empty()) {
        return std::nullopt;
    }

    std::set<SignalId> bound_signals;
    std::vector<SystemVerilogTemplateSignalRole> roles;
    for (const auto declaration_id : signal_declarations) {
        const auto declaration
            = specialized.find_declaration(declaration_id);
        if (!declaration || declaration->systemverilog == nullptr) {
            continue;
        }
        const auto signal
            = lowerer.hir_concurrent_port_signal(declaration_id);
        if (!signal) {
            for (const auto& element :
                lowerer.hir_concurrent_container_element_signals(
                    declaration_id)) {
                if (!used_signals.contains(element.signal)) {
                    continue;
                }
                if (element.signal >= design_.signals_.size()
                    || element.signal >= design_.signal_info_.size()
                    || !bound_signals.insert(element.signal).second) {
                    return std::nullopt;
                }
                roles.push_back({
                    declaration_id,
                    element.signal,
                    element.ordinal,
                    element.container_type,
                    element.read_only,
                    element.readable,
                    element.writable,
                });
            }
            continue;
        }
        if (!used_signals.contains(*signal)) {
            continue;
        }
        if (*signal >= design_.signals_.size()
            || *signal >= design_.signal_info_.size()
            || !bound_signals.insert(*signal).second) {
            return std::nullopt;
        }
        const auto read_only
            = lowerer.hir_concurrent_signal_read_only(*signal);
        roles.push_back({
            declaration_id,
            *signal,
            std::nullopt,
            std::nullopt,
            read_only,
            true,
            !read_only,
        });
    }
    if (roles.empty() || bound_signals.size() != used_signals.size()) {
        return std::nullopt;
    }
    return roles;
}

std::optional<HierarchyBuilder::SystemVerilogGeneratedProcessIdentity>
HierarchyBuilder::systemverilog_generated_process_identity(
    const semantic::SpecializedHirOverlay& overlay,
    const std::string_view module_hierarchy,
    const std::string_view occurrence_path) const
{
    std::string_view relative_path = occurrence_path;
    if (!module_hierarchy.empty()) {
        if (!occurrence_path.starts_with(module_hierarchy)
            || occurrence_path.size() <= module_hierarchy.size()
            || occurrence_path[module_hierarchy.size()] != '.') {
            return std::nullopt;
        }
        relative_path.remove_prefix(module_hierarchy.size() + 1U);
    }
    if (relative_path.empty()) {
        return std::nullopt;
    }

    SystemVerilogGeneratedProcessIdentity identity;
    identity.relative_path = relative_path;
    identity.constant_bindings.reserve(
        overlay.hierarchy_identities.size());
    for (const auto& binding : overlay.hierarchy_identities) {
        if (binding.name.empty() || binding.identity.empty()) {
            return std::nullopt;
        }
        identity.constant_bindings.emplace_back(
            binding.name, binding.identity);
    }
    return identity;
}

std::optional<HierarchyBuilder::SystemVerilogProcessReplay>
HierarchyBuilder::replay_systemverilog_process_template(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    const semantic::sv::Process& source,
    Lowerer& lowerer,
    const std::span<const semantic::DeclarationId>,
    const frontend::Language source_language,
    const bool generated_occurrence,
    const std::string_view module_hierarchy,
    const std::string_view path,
    const std::optional<std::uint32_t> program_owner)
{
    if (source_language != frontend::Language::SystemVerilog2017
        || unit.kind != semantic::sv::UnitKind::module
        || coverage_ != nullptr
        || program_owner.has_value()
        || (source.kind != semantic::sv::ProcessKind::always_comb
            && source.kind != semantic::sv::ProcessKind::always_ff
            && source.kind != semantic::sv::ProcessKind::always)
        || source.concurrent_assertion) {
        return std::nullopt;
    }

    SystemVerilogGeneratedProcessIdentity generated_identity;
    if (generated_occurrence) {
        const auto identity = systemverilog_generated_process_identity(
            specialized.specialization(), module_hierarchy, path);
        if (!identity) {
            return std::nullopt;
        }
        generated_identity = *identity;
    }
    const SystemVerilogProcessTemplateKey key {
        unit.id, source.id, source_language, generated_occurrence,
        std::move(generated_identity)
    };
    const auto found = systemverilog_process_templates_.find(key);
    if (found == systemverilog_process_templates_.end()) {
        return std::nullopt;
    }

    bool matching_overlay { };
    const auto callable_invocation_before
        = lowerer.next_hir_callable_invocation_identity();
    const auto overlay = overlay_class(specialized);
    for (const auto& cached : found->second) {
        if (overlay == no_overlay_class || cached.overlay_class != overlay) {
            continue;
        }
        matching_overlay = true;
        if (!same_systemverilog_process_source(cached.source, source)
            || cached.language_standard != unit.standard
            || cached.compatibility_profile
                != unit.compatibility_profile
            || !cached.common
            || !cached.common->matches(
                ProcessProgramView { *cached.common, cached.program })
            || (source.kind == semantic::sv::ProcessKind::always
                && cached.program.static_sensitivity.empty())) {
            continue;
        }
        const bool has_callable_invocations
            = cached.callable_invocation_after
                != cached.callable_invocation_before;
        if (has_callable_invocations
            && callable_invocation_before
                != cached.callable_invocation_before) {
            continue;
        }

        const auto target_roles = resolve_systemverilog_template_roles(
            lowerer, cached.signal_roles);
        if (!target_roles) {
            continue;
        }
        const auto signal_remap = make_systemverilog_template_signal_remap(
            cached.signal_roles,
            *target_roles,
            design_.signal_info_,
            design_.signals_);
        if (!signal_remap) {
            continue;
        }

        ProcessInstanceProgram replay = cached.program;
        replay.id = static_cast<ProcessId>(design_.process_count());
        const auto cached_name = std::string_view { cached.program.name };
        if (!cached_name.starts_with(cached.hierarchy)
            || (cached_name.size() > cached.hierarchy.size()
                && cached_name[cached.hierarchy.size()] != '.')) {
            continue;
        }
        if (!generated_occurrence
            && (source.name.empty() || source.name == "<process>")) {
            // The lowerer derives anonymous names from the new runtime
            // ProcessId; copying the cached suffix would retain the
            // prototype's stale `.process_<id>` suffix.
            replay.name = std::string { path } + ".process_"
                + std::to_string(replay.id);
        } else {
            replay.name = std::string { path }
                + std::string {
                    cached_name.substr(cached.hierarchy.size()) };
        }
        if (!remap_systemverilog_template_process(
                replay, *signal_remap, cached.hierarchy, path,
                design_.signal_info_)) {
            continue;
        }
        replay.observed = source.concurrent_assertion;
        replay.reactive = !source.concurrent_assertion
            && program_owner.has_value();
        replay.program_owner = program_owner;
        if (has_callable_invocations
            && !lowerer.advance_hir_callable_invocation_identity(
                cached.callable_invocation_before,
                cached.callable_invocation_after)) {
            continue;
        }
        record_lowering_census(generated_occurrence
                ? systemverilog_generated_process_template_replays_
                : systemverilog_ordinary_process_template_replays_);
        return SystemVerilogProcessReplay {
            cached.common, std::move(replay)
        };
    }
    if (matching_overlay) {
        record_lowering_census(generated_occurrence
                ? systemverilog_generated_process_template_rejections_
                : systemverilog_ordinary_process_template_rejections_);
    }
    return std::nullopt;
}

bool HierarchyBuilder::remember_systemverilog_process_template(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    const semantic::sv::Process& source,
    const Lowerer& lowerer,
    const std::span<const semantic::DeclarationId> signal_declarations,
    const frontend::Language source_language,
    const bool generated_occurrence,
    const std::string_view module_hierarchy,
    const std::string_view hierarchy,
    const std::uint32_t callable_invocation_before,
    const std::uint32_t callable_invocation_after,
    const Process& process)
{
    // Plain `always` is cacheable only when lowering proves a static
    // sensitivity. Timed empty-sensitivity loops remain per occurrence.
    if (source_language != frontend::Language::SystemVerilog2017
        || unit.kind != semantic::sv::UnitKind::module
        || coverage_ != nullptr
        || process.program_owner.has_value()
        || (source.kind != semantic::sv::ProcessKind::always_comb
            && source.kind != semantic::sv::ProcessKind::always_ff
            && source.kind != semantic::sv::ProcessKind::always)
        || source.concurrent_assertion
        || (source.kind == semantic::sv::ProcessKind::always
            && process.static_sensitivity.empty())
        || process.scheduling_domain
            != ProcessSchedulingDomain::systemverilog
        || process.string_register_count != 0U
        || process.container_register_count != 0U
        || (!process.debug_locals.empty()
            && callable_invocation_after == callable_invocation_before)
        || !process.debug_string_locals.empty()
        || !process.debug_container_locals.empty()
        || process.switch_source || process.switch_target
        || process.switch_control
        || !process.operations.signal_remap().empty()
        || !process.operations.instance_operation_overrides().empty()
        || callable_invocation_after < callable_invocation_before) {
        return false;
    }

    SystemVerilogGeneratedProcessIdentity generated_identity;
    if (generated_occurrence) {
        const auto identity = systemverilog_generated_process_identity(
            specialized.specialization(), module_hierarchy, hierarchy);
        if (!identity) {
            return false;
        }
        generated_identity = *identity;
    }

    const auto signal_roles = systemverilog_process_signal_roles(
        process, lowerer, specialized, signal_declarations);
    if (!signal_roles) {
        return false;
    }
    std::map<SignalId, SignalId> identity_signals;
    for (const auto& role : *signal_roles) {
        if (!identity_signals.emplace(role.signal, role.signal).second) {
            return false;
        }
    }
    Process identity_check = process;
    if (!remap_systemverilog_template_process(
            identity_check, identity_signals, hierarchy, hierarchy,
            design_.signal_info_)) {
        return false;
    }

    const SystemVerilogProcessTemplateKey key {
        unit.id, source.id, source_language, generated_occurrence,
        std::move(generated_identity)
    };
    auto& templates = systemverilog_process_templates_[key];
    const auto overlay = overlay_class(specialized);
    if (overlay != no_overlay_class
        && std::ranges::any_of(templates, [&](const auto& cached) {
               return cached.overlay_class == overlay;
           })) {
        return false;
    }
    auto common = design_.intern_process_template(
        ProcessProgramView { process });
    ProcessInstanceProgram instance { ProcessProgramView { process } };
    templates.push_back(SystemVerilogProcessTemplate {
        specialized.specialization(),
        source,
        unit.standard,
        unit.compatibility_profile,
        std::string { hierarchy },
        std::move(common),
        std::move(instance),
        *signal_roles,
        callable_invocation_before,
        callable_invocation_after,
    });
    templates.back().overlay_class = overlay;
    record_lowering_census(generated_occurrence
            ? systemverilog_generated_process_templates_lowered_
            : systemverilog_ordinary_process_templates_lowered_);
    return true;
}

void HierarchyBuilder::canonicalize_process_operations(Process& process)
{
    if (coverage_ != nullptr) {
        return;
    }
    if (!process_operations_shareable(process)) {
        return;
    }

    ProcessOperationGroupingKey key;
    key.operation_count = process.operations.size();
    key.register_count = process.register_count;
    key.string_register_count = process.string_register_count;
    key.container_register_count = process.container_register_count;
    const auto register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.register_value_kinds);
    key.register_value_kinds.assign(
        register_value_kinds.begin(), register_value_kinds.end());
    key.operation_kinds.reserve(process.operations.size());
    for (const auto& operation : std::as_const(process.operations)) {
        key.operation_kinds.emplace_back(
            operation_group_index(operation),
            operation_alternative_index(operation));
    }
    key.sensitivity_edges.reserve(process.static_sensitivity.size());
    for (const auto& sensitivity : process.static_sensitivity) {
        key.sensitivity_edges.push_back(sensitivity.edge);
    }
    const auto trigger_regions
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.static_trigger_regions);
    key.trigger_regions.reserve(trigger_regions.size());
    for (const auto& region : trigger_regions) {
        key.trigger_regions.emplace_back(
            region.begin, region.end, region.mask);
    }
    key.language_standard = process.language_standard;
    key.compatibility_profile = process.compatibility_profile;

    auto& representatives = process_operation_representatives_[key];
    std::erase_if(
        representatives,
        [&](const ProcessId representative) {
            return representative >= design_.process_count();
        });
    for (const auto representative : representatives) {
        const auto representative_process
            = design_.process_view(representative);
        if (process_program_detail::share_operations(
                representative_process,
                process,
                design_.signals_,
                &operation_scratch_)) {
            return;
        }
    }
    representatives.push_back(process.id);
}

void HierarchyBuilder::canonicalize_process_operations(
    const std::shared_ptr<const ProcessProgramTemplate>& common,
    ProcessInstanceProgram& instance)
{
    if (coverage_ != nullptr || common == nullptr
        || !process_program_detail::operation_list_shareable(
            instance.operations)) {
        return;
    }

    const ProcessProgramView process { *common, instance };
    ProcessOperationGroupingKey key;
    key.operation_count = process.operations().size();
    key.register_count = process.register_count();
    key.string_register_count = process.string_register_count();
    key.container_register_count = process.container_register_count();
    const auto register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.register_value_kinds());
    key.register_value_kinds.assign(
        register_value_kinds.begin(), register_value_kinds.end());
    key.operation_kinds.reserve(process.operations().size());
    for (const auto& operation : process.operations()) {
        key.operation_kinds.emplace_back(
            operation_group_index(operation),
            operation_alternative_index(operation));
    }
    key.sensitivity_edges.reserve(process.static_sensitivity().size());
    for (const auto& sensitivity : process.static_sensitivity()) {
        key.sensitivity_edges.push_back(sensitivity.edge);
    }
    const auto trigger_regions
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process.static_trigger_regions());
    key.trigger_regions.reserve(trigger_regions.size());
    for (const auto& region : trigger_regions) {
        key.trigger_regions.emplace_back(
            region.begin, region.end, region.mask);
    }
    key.language_standard = process.language_standard();
    key.compatibility_profile = process.compatibility_profile();

    auto& representatives = process_operation_representatives_[key];
    std::erase_if(
        representatives,
        [&](const ProcessId representative) {
            return representative >= design_.process_count();
        });
    const auto share = [&](const ProcessId representative) {
        return process_program_detail::share_operations(
            design_.process_view(representative), *common, instance,
            design_.signals_, &operation_scratch_);
    };
    const auto remembered = representative_of_template_.find(common.get());
    if (remembered != representative_of_template_.end()
        && remembered->second < design_.process_count()
        && share(remembered->second)) {
        return;
    }
    for (const auto representative : representatives) {
        if (remembered != representative_of_template_.end()
            && representative == remembered->second) {
            continue;
        }
        if (share(representative)) {
            representative_of_template_[common.get()] = representative;
            return;
        }
    }
    representatives.push_back(instance.id);
    representative_of_template_[common.get()] = instance.id;
}

HierarchyBuilder::HierarchyBuilder(
    const semantic::ValidatedCompiledDesign compiled,
    ElaboratedDesign& design,
    std::vector<Diagnostic>& diagnostics,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries,
    const CoverageHirContext* coverage)
    : validated_compiled_(compiled),
      compiled_(&compiled.design()),
      design_(design),
      coverage_(coverage),
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
    // FSIM_PROFILE_LOWERING=0 keeps phase profiles free of census timing.
    const char* lowering_profile = std::getenv("FSIM_PROFILE_LOWERING");
    lowering_census_enabled_ = lowering_profile != nullptr
        ? std::string_view { lowering_profile } != "0"
        : std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    const char* merge_setting = std::getenv("FSIM_MERGE_CONSTANT_DRIVERS");
    merge_constant_drivers_ = merge_setting == nullptr
        || std::string_view { merge_setting } != "0";
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
