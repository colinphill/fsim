// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <iterator>
#include <map>
#include <type_traits>
#include <unordered_set>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

frontend::SourceSpan compiled_resolution_source_span(
    const semantic::CompiledDesign& compiled,
    const semantic::SourceSpanId source)
{
    frontend::SourceSpan result;
    const auto& spans = compiled.semantics.source_spans();
    if (!source.valid() || source.value() >= spans.size()) {
        return result;
    }
    const auto& span = spans[source.value()];
    result.source_name = span.logical_name;
    result.begin = {
        static_cast<std::size_t>(span.begin.offset),
        span.begin.line,
        span.begin.column,
    };
    result.end = {
        static_cast<std::size_t>(span.end.offset),
        span.end.line,
        span.end.column,
    };
    const auto& files = compiled.semantics.source_files();
    if (span.file.valid() && span.file.value() < files.size()) {
        result.physical_source_name = files[span.file.value()].physical_name;
    }
    return result;
}

const semantic::sv::Declaration* compiled_systemverilog_declaration(
    const semantic::CompiledDesign& compiled,
    const semantic::DeclarationId id)
{
    const auto found = compiled.find_declaration(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

const semantic::sv::TypeDefinition* compiled_systemverilog_type(
    const semantic::CompiledDesign& compiled,
    const semantic::TypeId id)
{
    const auto found = compiled.find_type(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

const semantic::sv::Statement* compiled_systemverilog_statement(
    const semantic::CompiledDesign& compiled,
    const semantic::StatementId id)
{
    const auto found = compiled.find_statement(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

const semantic::sv::Expression* compiled_systemverilog_expression(
    const semantic::CompiledDesign& compiled,
    const semantic::ExpressionId id)
{
    const auto found = compiled.find_expression(id);
    return found && found->systemverilog != nullptr
        ? found->systemverilog
        : nullptr;
}

bool compiled_resolver_returns_first(
    const semantic::CompiledDesign& compiled,
    const semantic::sv::Declaration& function)
{
    if (!function.callable || function.callable->formals.size() != 1U
        || function.statements.size() != 1U) {
        return false;
    }
    const auto* formal = compiled_systemverilog_declaration(
        compiled, function.callable->formals.front());
    const auto* statement = compiled_systemverilog_statement(
        compiled, function.statements.front());
    if (formal == nullptr || statement == nullptr
        || statement->kind
            != semantic::sv::StatementKind::return_statement
        || !statement->value) {
        return false;
    }
    const auto* value = compiled_systemverilog_expression(
        compiled, *statement->value);
    if (value == nullptr
        || value->kind != semantic::sv::ExpressionKind::index
        || value->operands.size() != 2U) {
        return false;
    }
    const auto* drivers = compiled_systemverilog_expression(
        compiled, value->operands.front());
    const auto* index = compiled_systemverilog_expression(
        compiled, value->operands.back());
    return drivers != nullptr && index != nullptr
        && drivers->kind == semantic::sv::ExpressionKind::name
        && drivers->text == formal->name
        && index->kind == semantic::sv::ExpressionKind::integer_literal
        && index->text == "0";
}

} // namespace

void HierarchyBuilder::finish()
{
    validate_process_drivers();
    std::stable_sort(
        design_.systemc_objects_.begin(),
        design_.systemc_objects_.end(),
        [](const SystemCNamedObjectInfo& left,
            const SystemCNamedObjectInfo& right) {
            return left.native_handle < right.native_handle;
        });
    for (const auto& [path, binding] : bindings_) {
        (void)binding;
        if (!used_bindings_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-011",
                "binding instance path '" + path
                    + "' was not found in the elaborated hierarchy",
                { });
        }
    }
    for (const auto& [path, instance] : systemc_instances_) {
        (void)instance;
        if (!used_systemc_instances_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-033",
                "constructed SystemC instance path '" + path
                    + "' was not reached from the elaborated hierarchy",
                { });
        }
    }
    // Root-global predeclaration and authoritative root instantiation both
    // specialize the same unit now that no prepared AST root is retained.
    // Preserve the first diagnostic (including constant-function $error
    // failures) while coalescing the identical second report.
    std::vector<Diagnostic> unique_diagnostics;
    unique_diagnostics.reserve(diagnostics_.size());
    for (auto& diagnostic : diagnostics_) {
        const auto duplicate = std::ranges::any_of(
            unique_diagnostics, [&](const Diagnostic& retained) {
                return retained.code == diagnostic.code
                    && retained.message == diagnostic.message
                    && retained.span == diagnostic.span;
            });
        if (!duplicate) {
            unique_diagnostics.push_back(std::move(diagnostic));
        }
    }
    diagnostics_ = std::move(unique_diagnostics);
    design_.freeze_hierarchy_paths();
}

ResolutionKind HierarchyBuilder::native_resolution(
    const SignalInfo& signal)
{
    const auto& net_type = signal.systemverilog_net_type.empty()
        ? signal.type_name
        : signal.systemverilog_net_type;
    if (signal.type_name == "std_logic"
        || signal.type_name == "std_logic_vector") {
        return ResolutionKind::std_logic;
    }
    if (net_type == "wire"
        || net_type == "tri"
        || net_type == "tri0"
        || net_type == "tri1"
        || net_type == "trireg"
        || net_type == "supply0"
        || net_type == "supply1") {
        return ResolutionKind::sv_wire;
    }
    if (net_type == "wand"
        || net_type == "triand") {
        return ResolutionKind::sv_wand;
    }
    if (net_type == "wor"
        || net_type == "trior") {
        return ResolutionKind::sv_wor;
    }
    // A nonempty name can also be an unresolved user nettype or an
    // accidentally retained variable spelling. Only the explicit built-in
    // net kinds above have native wire resolution; user nettypes are handled
    // by explicit_resolution() after their resolver has been linked.
    return ResolutionKind::none;
}

std::optional<ResolutionKind>
HierarchyBuilder::explicit_resolution(
    const SignalId signal)
{
    const auto found = resolver_by_signal_.find(signal);
    if (found == resolver_by_signal_.end()) {
        const auto& info = design_.signal_info_.at(signal);
        const auto nettype = info.systemverilog_net_type.empty()
            ? info.type_name
            : info.systemverilog_net_type;
        const auto user_nettype = systemverilog_resolution_kinds_.find(
            nettype);
        if (user_nettype != systemverilog_resolution_kinds_.end()) {
            return user_nettype->second;
        }
        return std::nullopt;
    }
    if (found->second == "std_logic") {
        return ResolutionKind::std_logic;
    }
    if (found->second == "sv_wire") {
        return ResolutionKind::sv_wire;
    }
    if (const auto user = vhdl_resolution_kinds_.find(found->second);
        user != vhdl_resolution_kinds_.end()) {
        return user->second;
    }
    if (const auto user = systemverilog_resolution_kinds_.find(found->second);
        user != systemverilog_resolution_kinds_.end()) {
        return user->second;
    }
    report(
        "FSIM-ELAB-BIND-050",
        "unknown resolver '" + found->second
            + "'; expected \"std_logic\" or \"sv_wire\"",
        { });
    return ResolutionKind::none;
}

void HierarchyBuilder::register_systemverilog_resolution_functions(
    const semantic::sv::Unit& unit)
{
    if (compiled_ == nullptr) {
        return;
    }
    if (std::ranges::find(
            systemverilog_resolution_unit_registrations_, unit.id)
        != systemverilog_resolution_unit_registrations_.end()) {
        return;
    }
    const auto diagnostics_before = diagnostics_.size();
    for (const auto declaration_id : unit.declarations) {
        const auto* declaration = compiled_systemverilog_declaration(
            *compiled_, declaration_id);
        if (declaration == nullptr
            || declaration->form
                != semantic::sv::DeclarationForm::nettype_declaration
            || !declaration->declared_type) {
            continue;
        }
        const auto* type = compiled_systemverilog_type(
            *compiled_, *declaration->declared_type);
        if (type == nullptr || type->resolution_function.empty()) {
            continue;
        }
        const auto& resolver = type->resolution_function;
        std::vector<const semantic::sv::Declaration*> matches;
        const semantic::CompiledDeclarationPredicate executable_function
            = [](const semantic::CompiledDeclarationView& candidate) {
                  return candidate.systemverilog != nullptr
                      && candidate.systemverilog->form
                          == semantic::sv::DeclarationForm::function
                      && !candidate.systemverilog->statements.empty();
              };
        const auto resolution = semantic::CompiledDesignResolver {
            *compiled_, unit.id }
                                    .resolve_systemverilog(resolver,
                                        unit.scope, executable_function,
                                        false);
        for (const auto candidate_id : resolution.candidates) {
            if (const auto* candidate = compiled_systemverilog_declaration(
                    *compiled_, candidate_id)) {
                matches.push_back(candidate);
            }
        }
        const auto source = compiled_resolution_source_span(
            *compiled_, type->source);
        if (matches.size() != 1U) {
            report(
                matches.empty()
                    ? "FSIM-ELAB-SVNETTYPE-001"
                    : "FSIM-ELAB-SVNETTYPE-002",
                matches.empty()
                    ? "SystemVerilog nettype resolution function '"
                        + resolver
                        + "' is not visible with an executable body"
                    : "SystemVerilog nettype resolution function '"
                        + resolver + "' is ambiguous",
                source);
            continue;
        }
        const auto& function = *matches.front();
        const auto* formal = function.callable
                && function.callable->formals.size() == 1U
            ? compiled_systemverilog_declaration(
                  *compiled_, function.callable->formals.front())
            : nullptr;
        const bool profile_matches = function.callable
            && function.callable->function
            && formal != nullptr && formal->type
            && formal->type->container_form
                == semantic::sv::TypeForm::dynamic_array
            && type->base.executable_width
            && function.callable->return_type.executable_width
            && *type->base.executable_width
                == *function.callable->return_type.executable_width
            && type->base.four_state
                == function.callable->return_type.four_state;
        if (!profile_matches) {
            report(
                "FSIM-ELAB-SVNETTYPE-003",
                "SystemVerilog nettype resolution function '" + resolver
                    + "' must take one dynamic array of the net base type "
                      "and return that base type",
                compiled_resolution_source_span(
                    *compiled_, function.source));
            continue;
        }
        if (!compiled_resolver_returns_first(*compiled_, function)) {
            report(
                "FSIM-ELAB-SVNETTYPE-004",
                "the executable SystemVerilog nettype resolver '" + resolver
                    + "' is outside the retained deterministic resolution "
                      "forms",
                compiled_resolution_source_span(
                    *compiled_, function.source));
            continue;
        }
        const auto [entry, inserted] = systemverilog_resolution_kinds_.emplace(
            resolver, ResolutionKind::sv_user_first);
        if (!inserted && entry->second != ResolutionKind::sv_user_first) {
            report(
                "FSIM-ELAB-SVNETTYPE-002",
                "SystemVerilog nettype resolution function '" + resolver
                    + "' has conflicting visible bodies",
                source);
        }
        const auto [nettype, nettype_inserted]
            = systemverilog_resolution_kinds_.emplace(
                declaration->name, ResolutionKind::sv_user_first);
        if (!nettype_inserted
            && nettype->second != ResolutionKind::sv_user_first) {
            report(
                "FSIM-ELAB-SVNETTYPE-002",
                "SystemVerilog nettype '" + declaration->name
                    + "' has conflicting visible resolution functions",
                source);
        }
    }
    if (diagnostics_.size() == diagnostics_before) {
        systemverilog_resolution_unit_registrations_.push_back(unit.id);
    }
}

void HierarchyBuilder::set_resolution(
    const SignalId signal,
    const ResolutionKind resolution)
{
    design_.signal_info_.at(signal).resolution = resolution;
    design_.signals_.at(signal).resolution = resolution;
}

void HierarchyBuilder::validate_process_drivers()
{
    using DriverRegion = Process::DriverRegion;
    struct ProcessDriver {
        std::vector<DriverRegion> regions;
        bool continuous { };
        bool event_controlled { };
    };
    std::unordered_map<ContainerObjectId, SignalId> writable_container_signals;
    std::unordered_map<SignalId, bool> variable_container_signals;
    for (const auto& alias : design_.container_signal_aliases_) {
        if (alias.writable) {
            writable_container_signals.insert_or_assign(
                alias.object, alias.signal);
            if (alias.signal < design_.signal_info_.size()
                && design_.signal_info_[alias.signal]
                       .systemverilog_net_type.empty()) {
                variable_container_signals.insert_or_assign(
                    alias.signal, true);
            }
        }
    }
    const auto regions_overlap = [](
                                     const DriverRegion& left,
                                     const DriverRegion& right) {
        if (left.whole || right.whole) {
            return true;
        }
        const auto left_end = static_cast<std::uint64_t>(left.offset)
            + left.width;
        const auto right_end = static_cast<std::uint64_t>(right.offset)
            + right.width;
        return left.offset < right_end && right.offset < left_end;
    };
    const auto process_leaf = [](const Process& process) {
        return std::string_view { process.name }.substr(
            process.name.find_last_of('.') + 1U);
    };
    const auto is_continuous_process = [&](const Process& process) {
        const auto leaf = process_leaf(process);
        return leaf.starts_with("concurrent_")
            || leaf.starts_with("continuous_fused_");
    };
    std::unordered_set<SignalId> continuous_signals;
    for (const auto& process : design_.processes_) {
        if (process.name.find("$declaration_initializer_")
                != std::string::npos
            || !is_continuous_process(process)) {
            continue;
        }
        for (const auto& region : process.driver_regions) {
            continuous_signals.insert(region.signal);
        }
        for (const auto& operation : process.operations) {
            visit_operation(
                [&](const auto& value) {
                    using OperationType
                        = std::decay_t<decltype(value)>;
                    if constexpr (
                        std::is_same_v<
                            OperationType, WriteContainerObject>
                        || std::is_same_v<
                            OperationType, WriteContainerObjectElement>) {
                        const auto alias
                            = writable_container_signals.find(value.object);
                        if (alias != writable_container_signals.end()) {
                            continuous_signals.insert(alias->second);
                        }
                    }
                },
                operation);
        }
    }
    std::unordered_map<SignalId, std::vector<ProcessDriver>> drivers;
    for (const auto& process : design_.processes_) {
        if (process.name.find("$declaration_initializer_")
            != std::string::npos) {
            continue;
        }
        std::map<SignalId, std::vector<DriverRegion>> process_outputs;
        for (const auto& region : process.driver_regions) {
            process_outputs[region.signal].push_back(region);
        }
        const auto record_container_object_write =
            [&](const ContainerObjectId object) {
                const auto alias = writable_container_signals.find(object);
                if (alias == writable_container_signals.end()) {
                    return;
                }
                if (variable_container_signals.contains(alias->second)
                    && !continuous_signals.contains(alias->second)) {
                    // Procedural-only variable arrays retain their legacy
                    // multiple-writer semantics. Audit the conservative
                    // whole-array claim only when a continuous process also
                    // drives the alias-backed signal.
                    return;
                }
                auto& regions = process_outputs[alias->second];
                if (std::ranges::none_of(
                        regions,
                        [](const DriverRegion& region) {
                            return region.whole;
                        })) {
                    regions.push_back(DriverRegion {
                        alias->second, 0U, 0U, true });
                }
            };
        for (const auto& operation : process.operations) {
            visit_operation(
                [&](const auto& value) {
                    using OperationType
                        = std::decay_t<decltype(value)>;
                    if constexpr (
                        std::is_same_v<
                            OperationType, WriteContainerObject>
                        || std::is_same_v<
                            OperationType, WriteContainerObjectElement>) {
                        record_container_object_write(value.object);
                    }
                },
                operation);
        }
        for (auto& [signal, regions] : process_outputs) {
            const bool continuous = is_continuous_process(process);
            const bool event_controlled
                = !process.static_sensitivity.empty();
            if (process_leaf(process).starts_with("continuous_fused_")) {
                // Fusion combines independently elaborated continuous
                // assignments into one process. Keep their regions separate
                // here so an overlapping pair remains a multiple-driver
                // error after fusion.
                for (const auto& region : regions) {
                    drivers[signal].push_back(ProcessDriver {
                        { region }, continuous, event_controlled });
                }
            } else {
                drivers[signal].push_back(ProcessDriver {
                    std::move(regions), continuous, event_controlled });
            }
        }
    }
    for (SignalId signal = 0;
        signal < design_.signal_info_.size();
        ++signal) {
        const auto selected = explicit_resolution(signal);
        set_resolution(
            signal,
            selected.value_or(
                native_resolution(
                    design_.signal_info_.at(signal))));
    }
    for (const auto& [signal, process_drivers] : drivers) {
        const auto& info = design_.signal_info_.at(signal);
        const auto boundary_drivers = boundary_driver_paths_.find(signal);
        if (process_drivers.size() <= 1
            || (boundary_drivers != boundary_driver_paths_.end()
                && boundary_drivers->second.size() > 1U)
            || vhdl_1993_shared_signals_.contains(signal)
            || info.resolution != ResolutionKind::none
            || (info.type_name == "reg"
                && !variable_container_signals.contains(signal))
            || (info.type_name == "integer"
                && !variable_container_signals.contains(signal))
            || info.name.ends_with(".$container_storage")) {
            continue;
        }
        bool overlap = false;
        for (std::size_t left = 0;
            left < process_drivers.size() && !overlap; ++left) {
            for (std::size_t right = left + 1;
                right < process_drivers.size() && !overlap; ++right) {
                overlap = std::ranges::any_of(
                    process_drivers[left].regions,
                    [&](const auto& left_region) {
                        return std::ranges::any_of(
                            process_drivers[right].regions,
                            [&](const auto& right_region) {
                                return regions_overlap(
                                    left_region, right_region);
                            });
                    });
            }
        }
        if (!overlap
            && (!info.vhdl_mode_view_bindings.empty()
                || info.vhdl_array
                || std::ranges::all_of(
                    process_drivers,
                    [](const auto& driver) {
                        return driver.continuous;
                    }))) {
            continue;
        }
        if (process_drivers.size() == 2
            && process_drivers.front().event_controlled
                != process_drivers.back().event_controlled
            && !process_drivers.front().continuous
            && !process_drivers.back().continuous) {
            continue;
        }
        report(
            "FSIM-ELAB-DRV-001",
            "unresolved variable '" + info.name
                + "' has multiple process drivers",
            { });
    }
}

const Binding* HierarchyBuilder::binding_for(const std::string& path)
{
    const auto found = bindings_.find(path);
    if (found == bindings_.end()) {
        return nullptr;
    }
    used_bindings_.insert(path);
    return found->second;
}

std::optional<UnitResolutionCandidate>
HierarchyBuilder::compiled_instance_target(
    const std::string_view parent_library,
    const std::string_view name,
    const std::string& path,
    const frontend::SourceSpan source,
    const Binding* const binding,
    std::optional<semantic::CompiledUnitView> linked_target,
    const bool linked_target_authoritative)
{
    if (compiled_ == nullptr) {
        return std::nullopt;
    }
    if (binding != nullptr && binding->target) {
        const auto target = parse_target(*binding->target);
        if (!target) {
            report(
                "FSIM-ELAB-BIND-013",
                "malformed binding target '" + *binding->target + "'",
                source);
            return std::nullopt;
        }
        if (target->language == "systemc") {
            return UnitResolutionCandidate {
                binding->target, *binding->target, std::nullopt, nullptr };
        }
        if (target->language == "vhdl" && !target->architecture) {
            report(
                "FSIM-ELAB-BIND-016",
                "an explicit VHDL binding target must name an "
                "architecture, for example vhdl:work.entity(rtl)",
                source);
            return std::nullopt;
        }
        auto selected = choose_bound_unit(*compiled_, *target);
        if (!selected) {
            report(
                "FSIM-ELAB-BIND-015",
                "binding target '" + *binding->target
                    + "' was not found",
                source);
            return std::nullopt;
        }
        UnitResolutionCandidate result;
        result.identity = unit_identity(*selected);
        result.compiled_unit = std::move(selected);
        return result;
    }
    const bool linked_systemverilog_unit = linked_target
        && linked_target->systemverilog != nullptr
        && linked_target->vhdl == nullptr
        && (linked_target->systemverilog->kind
                == semantic::sv::UnitKind::module
            || linked_target->systemverilog->kind
                == semantic::sv::UnitKind::interface
            || linked_target->systemverilog->kind
                == semantic::sv::UnitKind::program)
        && !linked_target->systemverilog->external;
    const bool linked_vhdl_unit = linked_target
        && linked_target->vhdl != nullptr
        && linked_target->systemverilog == nullptr
        && linked_target->vhdl->kind
            == semantic::vhdl::UnitKind::architecture;
    if (linked_target_authoritative
        && (linked_systemverilog_unit || linked_vhdl_unit)) {
        UnitResolutionCandidate result;
        result.identity = unit_identity(*linked_target);
        result.compiled_unit = std::move(linked_target);
        return result;
    }

    const auto scope = effective_search_scope(
        parent_library, search_libraries_);
    std::vector<UnitResolutionCandidate> candidates;
    std::vector<std::string> unavailable_libraries;
    for (std::size_t index = 0U; index < scope.size(); ++index) {
        const auto& library = scope[index];
        if (!has_logical_library(
                *compiled_, systemc_candidates_, systemc_libraries_,
                library)) {
            if (index != 0U) {
                unavailable_libraries.push_back(library);
            }
            continue;
        }
        auto resolved = resolve_unit_candidates(
            *compiled_, library, name);
        candidates.insert(
            candidates.end(),
            std::make_move_iterator(resolved.begin()),
            std::make_move_iterator(resolved.end()));
        for (const auto& factory : systemc_candidates_) {
            if (factory.library == library && factory.name == name) {
                candidates.push_back({
                    factory.target,
                    "systemc:" + factory.library + "." + factory.name,
                    std::nullopt,
                    nullptr,
                });
            }
        }
    }
    std::stable_sort(
        candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) {
            return left.identity < right.identity;
        });
    std::string formatted_scope;
    for (const auto& entry : scope) {
        if (!formatted_scope.empty()) {
            formatted_scope += ", ";
        }
        formatted_scope += entry;
    }
    if (!unavailable_libraries.empty()) {
        std::string unavailable;
        for (const auto& entry : unavailable_libraries) {
            if (!unavailable.empty()) {
                unavailable += ", ";
            }
            unavailable += entry;
        }
        report(
            "FSIM-ELAB-BIND-059",
            "instance '" + path
                + "' queried unavailable logical library/libraries ["
                + unavailable + "] while resolving unit '"
                + std::string { name } + "' in search scope ["
                + formatted_scope + "]",
            source);
        return std::nullopt;
    }
    if (candidates.empty()) {
        report(
            "FSIM-ELAB-BIND-012",
            "instance '" + path + "' names unit '"
                + std::string { name }
                + "', which was not found across VHDL, Verilog, "
                  "SystemVerilog, or SystemC in search scope ["
                + formatted_scope + "]; candidates: <none>",
            source);
        return std::nullopt;
    }
    if (candidates.size() != 1U) {
        report(
            "FSIM-ELAB-BIND-017",
            "instance '" + path + "' names ambiguous unit '"
                + std::string { name } + "' in search scope ["
                + formatted_scope + "]; candidates: "
                + format_resolution_candidates(candidates),
            source);
        return std::nullopt;
    }
    return candidates.front();
}

} // namespace fsim::elaboration
