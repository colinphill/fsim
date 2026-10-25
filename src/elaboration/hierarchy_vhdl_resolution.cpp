// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::elaboration {
namespace {

} // namespace

std::optional<CompiledVhdlResolutionDiagnostic>
HierarchyBuilder::register_compiled_vhdl_resolution(
    const SignalId signal,
    const semantic::vhdl::SubtypeIndication& subtype,
    const semantic::SourceSpanId declaration_source,
    const CompiledVhdlResolutionBinding& binding,
    const semantic::SpecializedHirUnit& specialization)
{
    resolver_by_signal_.insert_or_assign(signal, binding.designator);
    const auto source = binding.source.valid()
        ? binding.source
        : declaration_source;

    if (binding.kind == ResolutionKind::vhdl_user_function) {
        vhdl_function_resolvers_.insert_or_assign(signal,
            PendingVhdlResolver { binding.body, source,
                std::make_shared<const semantic::SpecializedHirUnit>(
                    specialization),
                { } });
    }
    if (binding.kind) {
        const auto [existing, inserted]
            = vhdl_resolution_kinds_.emplace(
                binding.designator, *binding.kind);
        if (!inserted && existing->second != *binding.kind) {
            return CompiledVhdlResolutionDiagnostic {
                "FSIM-ELAB-VHRESOLVE-003",
                "VHDL resolution function designator '"
                    + binding.designator
                    + "' denotes conflicting visible bodies",
                source,
            };
        }
        return std::nullopt;
    }

    switch (binding.issue) {
    case CompiledVhdlResolutionIssue::missing_body:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-001",
            "VHDL resolution function '" + binding.designator
                + "' is not visible with an executable body",
            source,
        };
    case CompiledVhdlResolutionIssue::invalid_profile:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-002",
            "VHDL resolution function '" + binding.designator
                + "' must be pure with one array-of-base-type "
                  "input and a base-type result",
            source,
        };
    case CompiledVhdlResolutionIssue::ambiguous_profile:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-003",
            "VHDL resolution function '" + binding.designator
                + "' is ambiguous for subtype '"
                + subtype.type_mark.spelling + "'",
            source,
        };
    case CompiledVhdlResolutionIssue::unsupported_body:
        return CompiledVhdlResolutionDiagnostic {
            "FSIM-ELAB-VHRESOLVE-004",
            "bounded VHDL resolution function '"
                + binding.designator
                + "' must return one scalar OR or AND expression",
            source,
        };
    case CompiledVhdlResolutionIssue::none:
        return std::nullopt;
    }

    return std::nullopt;
}

void HierarchyBuilder::register_compiled_vhdl_record_resolution(
    const SignalId signal,
    std::vector<VhdlResolverSegment> segments,
    const semantic::SourceSpanId source,
    const semantic::SpecializedHirUnit& specialization)
{
    static const std::string designator { "@vhdl-record-resolution" };
    resolver_by_signal_.insert_or_assign(signal, designator);
    vhdl_resolution_kinds_.insert_or_assign(
        designator, ResolutionKind::vhdl_user_function);
    const auto body = segments.front().function;
    vhdl_function_resolvers_.insert_or_assign(signal,
        PendingVhdlResolver { body, source,
            std::make_shared<const semantic::SpecializedHirUnit>(
                specialization),
            std::move(segments) });
}

void HierarchyBuilder::lower_compiled_vhdl_resolvers(
    const std::unordered_map<SignalId, std::vector<ProcessId>>& drivers)
{
    for (const auto& [signal, pending] : vhdl_function_resolvers_) {
        if (signal >= design_.signals_.size()
            || design_.signals_[signal].resolution
                != ResolutionKind::vhdl_user_function) {
            continue;
        }
        const auto found = drivers.find(signal);
        if (found == drivers.end() || found->second.empty()) {
            // Without drivers the resolution function is never called.
            set_resolution(signal, ResolutionKind::none);
            continue;
        }
        const SignalBindings signals;
        const ReadOnlySignalBindings read_only_signals;
        const StringObjectBindings string_objects;
        const ReadOnlyStringBindings read_only_strings;
        const ContainerObjectBindings container_objects;
        const ReadOnlyContainerBindings read_only_containers;
        Lowerer lowerer {
            design_,
            signals,
            read_only_signals,
            string_objects,
            read_only_strings,
            container_objects,
            read_only_containers,
            diagnostics_,
        };
        lowerer.set_specialized_hir_unit(pending.specialization.get());
        lowerer.set_vhdl_lowering_index(
            &vhdl_lowering_index_);
        const auto& name = design_.signal_info_.at(signal).name;
        const auto separator = name.find_last_of('.');
        const auto hierarchy = separator == std::string::npos
            ? std::string { }
            : name.substr(0U, separator);
        // Each driver of a record of resolved elements drives its own
        // parts of the unresolved ones.
        std::vector<VhdlDriverRegions> driver_regions;
        if (!pending.segments.empty()) {
            for (const auto process : found->second) {
                auto& regions = driver_regions.emplace_back();
                for (const auto& region :
                    design_.process_view(process).driver_regions()) {
                    if (region.signal == signal && !region.whole) {
                        regions.emplace_back(region.offset, region.width);
                    }
                }
            }
        }
        auto program = lowerer.lower_hir_vhdl_resolver_process(
            signal, pending.body, found->second.size(), hierarchy,
            pending.segments, driver_regions);
        if (!program) {
            report("FSIM-ELAB-VHRESOLVE-004",
                "the VHDL resolution function of '" + name
                    + "' could not be lowered as a resolver program",
                compiled_source_span(*compiled_, pending.source));
            continue;
        }
        const auto process = program->process.id;
        design_.append_process_record(std::move(program->process));
        VhdlSignalResolver resolver {
            process, program->input, program->output, found->second };
        // A guarded signal resolves its connected drivers only; a bus with
        // none calls the function with a null array (14.7.3.2).
        if (const auto guarded = vhdl_guarded_signal_kinds_.find(signal);
            guarded != vhdl_guarded_signal_kinds_.end()) {
            resolver.guarded_kind = guarded->second;
            for (std::size_t count { }; count < found->second.size();
                ++count) {
                if (count == 0U && guarded->second == 2U) {
                    resolver.variants.push_back(resolver.variants.empty()
                            ? VhdlResolverVariant { process, program->input,
                                  program->output }
                            : resolver.variants.back());
                    continue;
                }
                const auto diagnostics_before = diagnostics_.size();
                auto variant = lowerer.lower_hir_vhdl_resolver_process(
                    signal, pending.body, count, hierarchy,
                    pending.segments, driver_regions);
                if (count == 0U && diagnostics_.size() != diagnostics_before) {
                    variant.reset();
                }
                if (!variant && count == 0U) {
                    diagnostics_.resize(diagnostics_before);
                    // A null array argument is not yet lowered; the bus
                    // then keeps its value (marked by an invalid process).
                    resolver.variants.push_back(VhdlResolverVariant {
                        std::numeric_limits<ProcessId>::max(), 0U, 0U });
                    continue;
                }
                if (!variant) {
                    break;
                }
                const auto variant_process = variant->process.id;
                design_.append_process_record(std::move(variant->process));
                resolver.variants.push_back(VhdlResolverVariant {
                    variant_process, variant->input, variant->output });
            }
        }
        design_.signals_[signal].vhdl_resolver = std::move(resolver);
    }
}

namespace {

// A static name of a subelement or slice of a signal (IEEE 1076-2008 8.1):
// the formal is associated with that part of the actual signal itself.
bool vhdl_static_signal_name(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId actual, const std::size_t depth = 0U)
{
    const auto expression = specialization.find_expression(actual);
    if (!expression || expression->vhdl == nullptr || depth > 16U) {
        return false;
    }
    const auto& source = *expression->vhdl;
    using Kind = semantic::vhdl::ExpressionKind;
    // Record subelements only: an element or slice of an array actual keeps
    // its adapter, which closed VHDL kernels schedule as a member process.
    if (source.kind == Kind::name) {
        return source.text.find('.') != std::string::npos;
    }
    if ((source.kind != Kind::index && source.kind != Kind::slice)
        || source.operands.size() < 2U || depth != 0U) {
        return false;
    }
    for (std::size_t index = 1U; index < source.operands.size(); ++index) {
        const auto bound = specialization.find_expression(
            source.operands[index]);
        if (bound && bound->vhdl != nullptr
            && bound->vhdl->kind == Kind::binary
            && (bound->vhdl->text == "to" || bound->vhdl->text == "downto")
            && bound->vhdl->operands.size() == 2U) {
            if (!specialization.evaluate_integral_expression(
                    bound->vhdl->operands[0])
                || !specialization.evaluate_integral_expression(
                    bound->vhdl->operands[1])) {
                return false;
            }
            continue;
        }
        if (!specialization.evaluate_integral_expression(
                source.operands[index])) {
            return false;
        }
    }
    return vhdl_static_signal_name(
        specialization, source.operands.front(), depth + 1U);
}

bool vhdl_conversion_actual(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId actual)
{
    if (vhdl_static_signal_name(specialization, actual)) {
        return true;
    }
    const auto expression = specialization.find_expression(actual);
    // Subelement associations or an aggregate of signal names and literals
    // (`p(0) => '1', p(1) => x`) give the formal the actuals' value in the
    // same cycle and at initialization (IEEE 1076-2008 6.5.7.1, 14.7.5.2).
    // Nested formal parts (`p.b.x => s`) nest their subelement aggregates.
    const auto conversion_aggregate = [&](const auto& self,
                                          const semantic::vhdl::Expression& aggregate,
                                          const std::size_t depth) -> bool {
        return depth <= 16U
            && aggregate.kind == semantic::vhdl::ExpressionKind::aggregate
            && !aggregate.associations.empty()
            && std::ranges::all_of(aggregate.associations,
            [&](const semantic::vhdl::AggregateAssociation& association) {
                const auto value = specialization.find_expression(
                    association.value);
                if (!value || value->vhdl == nullptr) {
                    return false;
                }
                const auto kind = value->vhdl->kind;
                if (kind == semantic::vhdl::ExpressionKind::aggregate
                    && value->vhdl->text == "@vhdl-formal-subelements") {
                    return self(self, *value->vhdl, depth + 1U);
                }
                // An inertial part (`r.y => inertial t`) is an implicit
                // signal updated a delta after its signal (IEEE 1076-2008
                // 6.5.7.1, 14.7.3.3); the program reads that implicit signal
                // while the other parts follow their actuals in the same
                // cycle.
                if (kind == semantic::vhdl::ExpressionKind::call
                    && value->vhdl->text == "@vhdl-inertial"
                    && value->vhdl->operands.size() == 1U) {
                    const auto operand = specialization.find_expression(
                        value->vhdl->operands.front());
                    // Only a whole signal is delayed: a part of one keeps
                    // its adapter.
                    return operand && operand->vhdl != nullptr
                        && operand->vhdl->kind
                            == semantic::vhdl::ExpressionKind::name
                        && operand->vhdl->text.find_first_of(".(")
                            == std::string::npos;
                }
                // A type conversion or conversion function of a signal name
                // (`p(3 downto 2) => std_ulogic_vector(s)`).
                if (kind == semantic::vhdl::ExpressionKind::call
                    && value->vhdl->operands.size() == 1U
                    && !value->vhdl->text.starts_with('@')) {
                    const auto operand = specialization.find_expression(
                        value->vhdl->operands.front());
                    return operand && operand->vhdl != nullptr
                        && operand->vhdl->kind
                            == semantic::vhdl::ExpressionKind::name;
                }
                return kind == semantic::vhdl::ExpressionKind::logic_literal
                    || kind == semantic::vhdl::ExpressionKind::integer_literal
                    || kind == semantic::vhdl::ExpressionKind::string_literal
                    || kind == semantic::vhdl::ExpressionKind::boolean_literal
                    || vhdl_static_signal_name(
                        specialization, association.value)
                    || (kind == semantic::vhdl::ExpressionKind::name
                        && value->vhdl->text.find('.') == std::string::npos);
            });
    };
    if (expression && expression->vhdl != nullptr
        && conversion_aggregate(conversion_aggregate, *expression->vhdl, 0U)) {
        return true;
    }
    // A conversion function may be an operator symbol written as a call
    // (`"not"(s)`); the operator form `not s` is an expression actual.
    if (!expression || expression->vhdl == nullptr
        || (expression->vhdl->kind != semantic::vhdl::ExpressionKind::call
            && !(expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::unary
                && expression->vhdl->operator_symbol_call))
        || expression->vhdl->operands.size() != 1U
        || expression->vhdl->text.starts_with('@')
        || expression->vhdl->text.starts_with(
            semantic::vhdl::qualified_expression_prefix)) {
        return false;
    }
    const auto operand = specialization.find_expression(
        expression->vhdl->operands.front());
    return operand && operand->vhdl != nullptr
        && operand->vhdl->kind == semantic::vhdl::ExpressionKind::name;
}

} // namespace

void HierarchyBuilder::compose_vhdl_output_routes()
{
    // A part of a formal placed in a part of an actual.
    struct Route {
        SignalId formal { };
        std::uint32_t formal_offset { };
        std::uint32_t offset { };
        std::uint32_t width { };
    };
    struct Piece {
        SignalId signal { };
        std::uint32_t offset { };
        std::uint32_t width { };
    };
    const auto signal_width = [&](const SignalId signal) {
        return static_cast<std::uint64_t>(
            design_.signal_info_[signal].width);
    };
    const auto vhdl_standard = [](const std::string_view standard) {
        return standard == "1987" || standard == "1993"
            || standard == "2000" || standard == "2002"
            || standard == "2008" || standard == "2019";
    };
    // An output adapter (`port_result_N`) that only copies parts of its
    // formals into parts of its actuals; any other operation keeps it.
    std::map<std::size_t, std::vector<std::pair<SignalId, Route>>> adapters;
    std::unordered_map<SignalId, std::vector<std::size_t>> writers;
    std::unordered_set<SignalId> input_targets;
    for (std::size_t index { }; index < design_.process_count(); ++index) {
        const auto process = design_.process_view(index);
        for (const auto& region : process.driver_regions()) {
            auto& list = writers[region.signal];
            if (list.empty() || list.back() != index) {
                list.push_back(index);
            }
        }
        const std::string_view name { process.name() };
        const auto leaf = name.substr(name.find_last_of('.') + 1U);
        if (leaf.starts_with("port_actual_")) {
            for (const auto& region : process.driver_regions()) {
                input_targets.insert(region.signal);
            }
        }
        if (!leaf.starts_with("port_result_")
            || !vhdl_standard(process.language_standard())
            || process.scheduling_domain()
                != ProcessSchedulingDomain::generic) {
            continue;
        }
        std::unordered_map<RegisterId, Piece> pieces;
        std::vector<std::pair<SignalId, Route>> routes;
        bool eligible = true;
        const auto place = [&](const SignalId signal, const RegisterId source,
                               const std::uint64_t offset) {
            const auto piece = pieces.find(source);
            if (piece == pieces.end() || signal >= design_.signal_info_.size()
                || offset + piece->second.width > signal_width(signal)) {
                return false;
            }
            routes.emplace_back(signal, Route { piece->second.signal,
                piece->second.offset, static_cast<std::uint32_t>(offset),
                piece->second.width });
            return true;
        };
        for (const auto& operation : process.operations()) {
            if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
                if (read->kind != SignalReadKind::current || read->clock
                    || read->gate || read->signal >= design_.signal_info_.size()
                    || signal_width(read->signal)
                        > std::numeric_limits<std::uint32_t>::max()) {
                    eligible = false;
                    break;
                }
                pieces.insert_or_assign(read->destination, Piece { read->signal,
                    0U, static_cast<std::uint32_t>(
                        signal_width(read->signal)) });
            } else if (const auto* extract
                = operation_get_if<Extract>(&operation)) {
                const auto piece = pieces.find(extract->source);
                if (piece == pieces.end()
                    || static_cast<std::uint64_t>(extract->offset)
                            + extract->width
                        > piece->second.width) {
                    eligible = false;
                    break;
                }
                pieces.insert_or_assign(extract->destination,
                    Piece { piece->second.signal,
                        piece->second.offset + extract->offset,
                        extract->width });
            } else if (const auto* copy
                = operation_get_if<CopyRegister>(&operation)) {
                const auto piece = pieces.find(copy->source);
                if (piece == pieces.end()) {
                    eligible = false;
                    break;
                }
                const auto value = piece->second;
                pieces.insert_or_assign(copy->destination, value);
            } else if (const auto* write
                = operation_get_if<WriteUpdate>(&operation)) {
                const auto piece = pieces.find(write->source);
                if (piece == pieces.end()
                    || write->signal >= design_.signal_info_.size()
                    || piece->second.width != signal_width(write->signal)
                    || !place(write->signal, write->source, 0U)) {
                    eligible = false;
                    break;
                }
            } else if (const auto* slice
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                if (!place(slice->signal, slice->source, slice->offset)) {
                    eligible = false;
                    break;
                }
            } else if (!operation_holds<WaitSensitivity>(operation)
                && !operation_holds<Jump>(operation)
                && !operation_holds<Halt>(operation)
                && !operation_holds<DebugPoint>(operation)) {
                eligible = false;
                break;
            }
        }
        if (eligible && !routes.empty()) {
            adapters.emplace(index, std::move(routes));
        }
    }
    if (adapters.empty()) {
        return;
    }
    // Each destination must be unresolved, written by route adapters only,
    // and covered exactly once by its routes.
    std::map<SignalId, std::vector<Route>> destinations;
    for (const auto& [index, routes] : adapters) {
        for (const auto& [signal, route] : routes) {
            destinations[signal].push_back(route);
        }
    }
    std::unordered_set<SignalId> rejected;
    for (auto& [signal, routes] : destinations) {
        const auto& info = design_.signal_info_[signal];
        const auto& runtime_signal = design_.signals_[signal];
        bool eligible = !runtime_signal.vhdl_resolver
            && !resolver_by_signal_.contains(signal)
            && !vhdl_function_resolvers_.contains(signal)
            && !vhdl_guarded_signal_kinds_.contains(signal)
            && !vhdl_unprotected_shared_signals_.contains(signal)
            && !input_targets.contains(signal)
            && info.systemverilog_net_type.empty()
            && !explicit_resolution(signal)
            && native_resolution(info) == ResolutionKind::none
            && signal_width(signal) != 0U;
        std::ranges::sort(routes, {}, &Route::offset);
        std::uint64_t covered { };
        for (const auto& route : routes) {
            const auto& formal = design_.signals_[route.formal];
            // An inout formal is also driven from its actual; a converted
            // formal settles through its own program.
            if (route.offset != covered || route.formal == signal
                || formal.vhdl_resolver || input_targets.contains(route.formal)
                || formal.value_kind != runtime_signal.value_kind) {
                eligible = false;
                break;
            }
            covered += route.width;
        }
        if (!eligible || covered != signal_width(signal)) {
            rejected.insert(signal);
        }
    }
    // An adapter is replaced only when every destination it writes is; a
    // destination only when all of its writers are replaced adapters.
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [signal, routes] : destinations) {
            if (rejected.contains(signal)) {
                continue;
            }
            const auto found = writers.find(signal);
            const bool foreign = found != writers.end()
                && std::ranges::any_of(found->second,
                    [&](const std::size_t writer) {
                        const auto adapter = adapters.find(writer);
                        return adapter == adapters.end()
                            || std::ranges::any_of(adapter->second,
                                [&](const auto& route) {
                                    return rejected.contains(route.first);
                                });
                    });
            if (foreign) {
                rejected.insert(signal);
                changed = true;
            }
        }
    }
    for (const auto& [index, routes] : adapters) {
        if (std::ranges::any_of(routes, [&](const auto& route) {
                return rejected.contains(route.first);
            })) {
            continue;
        }
        const auto adapter = design_.process_view(index);
        Process stub;
        stub.id = adapter.id();
        stub.name = adapter.name();
        stub.language_standard = adapter.language_standard();
        stub.compatibility_profile = adapter.compatibility_profile();
        stub.initialize = false;
        stub.operations.emplace_back(Halt { });
        design_.rebuild_process_record(index, std::move(stub));
    }
    // A program reading each route's formal into its place in `signal`: the
    // composed value is left in `output`.
    struct RouteProgram {
        ProcessId id { };
        RegisterId input { };
        RegisterId output { };
        std::vector<SignalId> sources;
    };
    const auto route_program = [&](const SignalId signal,
                                   const std::vector<Route>& routes,
                                   const std::string_view role) {
        const auto process_index = design_.process_count();
        Process program;
        program.id = static_cast<ProcessId>(process_index);
        if (static_cast<std::size_t>(program.id) != process_index) {
            throw std::length_error { "too many SimIR processes" };
        }
        const auto& name = design_.signal_info_[signal].name;
        const auto separator = name.find_last_of('.');
        program.name = (separator == std::string::npos
                ? std::string { }
                : name.substr(0U, separator + 1U))
            + std::string { role } + name.substr(separator + 1U);
        const auto kind = design_.signals_[signal].value_kind;
        const auto allocate = [&](const ValueKind value_kind) {
            program.register_value_kinds.push_back(value_kind);
            return static_cast<RegisterId>(program.register_count++);
        };
        RouteProgram result;
        result.id = program.id;
        // The resolver input is unused: the program has no drivers.
        result.input = allocate(ValueKind::logic4);
        std::unordered_map<SignalId, RegisterId> formals;
        std::vector<RegisterId> parts;
        for (const auto& route : routes | std::views::reverse) {
            auto [formal, inserted] = formals.try_emplace(route.formal);
            if (inserted) {
                formal->second = allocate(kind);
                program.operations.emplace_back(
                    ReadSignal { formal->second, route.formal });
                result.sources.push_back(route.formal);
            }
            if (route.formal_offset == 0U
                && route.width == signal_width(route.formal)) {
                parts.push_back(formal->second);
                continue;
            }
            const auto part = allocate(kind);
            program.operations.emplace_back(Extract { part, formal->second,
                route.formal_offset, route.width });
            parts.push_back(part);
        }
        result.output = allocate(kind);
        if (parts.size() == 1U) {
            program.operations.emplace_back(
                CopyRegister { result.output, parts[0] });
        } else {
            program.operations.emplace_back(Concatenate { result.output,
                std::move(parts),
                static_cast<std::uint32_t>(signal_width(signal)) });
        }
        program.operations.emplace_back(Halt { });
        program.initialize = false;
        design_.append_process_record(std::move(program));
        return result;
    };
    for (const auto& [signal, routes] : destinations) {
        if (rejected.contains(signal)) {
            continue;
        }
        auto program = route_program(signal, routes, "composer_");
        VhdlSignalResolver resolver {
            program.id, program.input, program.output, { } };
        resolver.sources = std::move(program.sources);
        design_.signals_[signal].vhdl_resolver = std::move(resolver);
    }
    // An adapter that only copies its formals over the whole of resolved
    // actuals is a source of each actual like a formal conversion (IEEE
    // 1076-2019 14.7.3.2): its driving value follows the formals in the
    // same simulation cycle.
    for (auto& [index, routes] : adapters) {
        std::map<SignalId, std::vector<Route>> placed_by_destination;
        for (const auto& [destination, route] : routes) {
            placed_by_destination[destination].push_back(route);
        }
        const auto adapter = design_.process_view(index);
        if (std::ranges::any_of(adapter.driver_regions(),
                [&](const auto& region) {
                    return !placed_by_destination.contains(region.signal);
                })) {
            continue;
        }
        bool eligible = true;
        for (auto& [destination, placed] : placed_by_destination) {
            const auto& info = design_.signal_info_[destination];
            const auto& runtime_signal = design_.signals_[destination];
            const bool resolved = runtime_signal.resolution
                    != ResolutionKind::none
                || resolver_by_signal_.contains(destination)
                || explicit_resolution(destination)
                        .value_or(native_resolution(info))
                    != ResolutionKind::none;
            if (!rejected.contains(destination) || !resolved
                || runtime_signal.vhdl_resolver
                || vhdl_guarded_signal_kinds_.contains(destination)
                || vhdl_unprotected_shared_signals_.contains(destination)
                || input_targets.contains(destination)
                || !info.systemverilog_net_type.empty()) {
                eligible = false;
                break;
            }
            std::ranges::sort(placed, {}, &Route::offset);
            std::uint64_t covered { };
            for (const auto& route : placed) {
                const auto& formal = design_.signals_[route.formal];
                if (route.offset != covered || route.formal == destination
                    || formal.vhdl_resolver
                    || input_targets.contains(route.formal)
                    || formal.value_kind != runtime_signal.value_kind) {
                    eligible = false;
                    break;
                }
                covered += route.width;
            }
            if (!eligible || covered != signal_width(destination)) {
                eligible = false;
                break;
            }
        }
        if (!eligible) {
            continue;
        }
        // The adapter halts and keeps its drivers, which the programs set.
        const auto stub_id = adapter.id();
        Process stub;
        stub.id = stub_id;
        stub.name = adapter.name();
        stub.language_standard = adapter.language_standard();
        stub.compatibility_profile = adapter.compatibility_profile();
        stub.initialize = false;
        for (const auto& [destination, placed] : placed_by_destination) {
            stub.driver_regions.push_back(
                Process::DriverRegion { destination, 0U, 0U, true });
        }
        stub.operations.emplace_back(Halt { });
        design_.rebuild_process_record(index, std::move(stub));
        for (const auto& [destination, placed] : placed_by_destination) {
            auto program = route_program(destination, placed, "port_driver_");
            design_.signals_[destination].vhdl_conversion_drivers.push_back(
                VhdlConversionDriver { stub_id, program.id, program.output,
                    std::move(program.sources) });
        }
    }
}

bool HierarchyBuilder::vhdl_output_port_read(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::DeclarationId port)
{
    namespace vhdl = semantic::vhdl;
    const auto output = [](const vhdl::Declaration& declaration) {
        return declaration.form == vhdl::DeclarationForm::port
            && (declaration.direction == vhdl::Direction::output
                || declaration.direction == vhdl::Direction::buffer);
    };
    if (!vhdl_read_output_ports_) {
        const auto& hir = specialization.design().vhdl_hir;
        std::unordered_set<std::uint32_t> outputs;
        for (const auto& declaration : hir.declarations()) {
            if (output(declaration)) {
                outputs.insert(declaration.id.value());
            }
        }
        // The names a value is written through rather than read: assignment
        // targets and the actuals of out and buffer formals, with the
        // prefixes they select from (IEEE 1076-2019 10.5.2.1, 6.5.7.1).
        std::unordered_set<std::uint32_t> written;
        const auto mark = [&](const auto& self,
                              const semantic::ExpressionId id,
                              const std::size_t depth) -> void {
            const auto expression = specialization.find_expression(id);
            if (!expression || expression->vhdl == nullptr || depth > 64U) {
                return;
            }
            written.insert(id.value());
            const auto& source = *expression->vhdl;
            if ((source.kind == vhdl::ExpressionKind::index
                    || source.kind == vhdl::ExpressionKind::slice
                    || (source.kind == vhdl::ExpressionKind::call
                        && source.text.starts_with("@vhdl-member:")))
                && !source.operands.empty()) {
                self(self, source.operands.front(), depth + 1U);
            } else if (source.kind == vhdl::ExpressionKind::aggregate) {
                for (const auto& association : source.associations) {
                    self(self, association.value, depth + 1U);
                }
            }
        };
        for (const auto& statement : hir.statements()) {
            if (statement.target) {
                mark(mark, *statement.target, 0U);
            }
        }
        const auto mark_port_map
            = [&](const std::vector<vhdl::Association>& port_map) {
                  for (const auto& association : port_map) {
                      if (!association.expression || !association.formal
                          || !association.formal->selected) {
                          continue;
                      }
                      const auto formal = specialization.find_declaration(
                          *association.formal->selected);
                      if (formal && formal->vhdl != nullptr
                          && output(*formal->vhdl)) {
                          mark(mark, *association.expression, 0U);
                      }
                  }
              };
        for (const auto& instance : hir.instances()) {
            mark_port_map(instance.port_map);
        }
        const auto blocks = [&](const auto& self,
                                const std::vector<vhdl::GenerateRegion>&
                                    generates) -> void {
            for (const auto& generate : generates) {
                mark_port_map(generate.port_map);
                self(self, generate.nested);
            }
        };
        for (const auto& unit : specialization.design().vhdl_units()) {
            blocks(blocks, unit.generates);
        }
        auto& read = vhdl_read_output_ports_.emplace();
        for (const auto& expression : hir.expressions()) {
            if (expression.referenced_name
                && expression.referenced_name->selected
                && outputs.contains(
                    expression.referenced_name->selected->value())
                && !written.contains(expression.id.value())) {
                read.insert(expression.referenced_name->selected->value());
            }
        }
    }
    return vhdl_read_output_ports_->contains(port.value());
}

SignalId HierarchyBuilder::add_vhdl_formal_driving_mirror(
    const SignalId formal)
{
    // The formal keeps its drivers; its resolver program gives its effective
    // value from the actual, and the runtime copies its drivers' value to a
    // mirror signal in the same cycle (IEEE 1076-2008 14.7.3.2).
    const auto mirror = static_cast<SignalId>(design_.signals_.size());
    if (static_cast<std::size_t>(mirror) != design_.signals_.size()) {
        throw std::length_error { "too many SimIR signals" };
    }
    auto info = design_.signal_info_[formal];
    info.id = mirror;
    info.name += "$driving";
    info.is_port = false;
    auto signal = design_.signals_[formal];
    signal.name = info.name;
    signal.resolution = ResolutionKind::none;
    signal.vhdl_resolver.reset();
    signal.vhdl_conversion_drivers.clear();
    design_.signal_by_name_.insert_or_assign(info.name, mirror);
    design_.signal_info_.push_back(std::move(info));
    design_.signals_.push_back(std::move(signal));
    design_.signals_[formal].vhdl_resolver->driving_mirror = mirror;
    set_resolution(formal, ResolutionKind::vhdl_user_function);
    vhdl_formal_driving_mirrors_.insert_or_assign(formal, mirror);
    return mirror;
}

void HierarchyBuilder::read_vhdl_formal_driving_values(
    Process& process, std::vector<SignalId>* const sources)
{
    // The out side of an inout formal reads the formal's driving value, not
    // its effective value (IEEE 1076-2008 14.7.3.2).
    if (vhdl_formal_driving_mirrors_.empty()) {
        return;
    }
    const auto retarget = [&](SignalId& signal) {
        const auto mirror = vhdl_formal_driving_mirrors_.find(signal);
        if (mirror == vhdl_formal_driving_mirrors_.end()) {
            return false;
        }
        signal = mirror->second;
        return true;
    };
    for (std::size_t index { }; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
        bool changed { };
        if (auto* const read = operation_get_if<ReadSignal>(&operation)) {
            changed = retarget(read->signal);
        } else if (auto* const wait = operation_get_if<WaitOn>(&operation)) {
            for (auto& signal : wait->signals) {
                changed = retarget(signal) || changed;
            }
        }
        if (changed) {
            process.operations.replace(index, std::move(operation));
        }
    }
    for (auto& sensitivity : process.static_sensitivity) {
        (void)retarget(sensitivity.signal);
    }
    if (sources != nullptr) {
        for (auto& source : *sources) {
            (void)retarget(source);
        }
    }
}

bool HierarchyBuilder::lower_compiled_vhdl_formal_conversion(
    Lowerer& lowerer,
    const semantic::StatementId statement,
    const std::string_view path,
    const semantic::vhdl::Unit& owner)
{
    auto conversion = lowerer.lower_hir_vhdl_formal_conversion(
        statement, path);
    // A resolved actual combines its port's converted value with its other
    // sources through the resolution function; it keeps its driver. Its
    // resolution is assigned after elaboration, so its declared resolver
    // tells (IEEE 1076-2008 14.7.3.2).
    if (!conversion) {
        return false;
    }
    const auto destination = conversion->destination;
    if (design_.signals_[destination].vhdl_resolver) {
        return false;
    }
    const bool resolved = design_.signals_[destination].resolution
            != ResolutionKind::none
        || resolver_by_signal_.contains(destination)
        || explicit_resolution(destination)
                .value_or(native_resolution(
                    design_.signal_info_[destination]))
            != ResolutionKind::none;
    // Only an unresolved inout formal splits its effective value from its
    // driving value; a resolved one keeps its continuous assignments.
    if (conversion->effective && resolved) {
        return false;
    }
    // A subelement of an unresolved actual takes the converted value as its
    // driver; a resolved one keeps its assignment.
    if (conversion->slice && resolved) {
        return false;
    }
    auto& program = conversion->conversion;
    auto& process = program.program.process;
    process.language_standard = owner.standard;
    process.compatibility_profile = owner.compatibility_profile;
    if (!conversion->effective) {
        read_vhdl_formal_driving_values(process, &program.sources);
    }
    canonicalize_process_operations(process);
    const auto process_id = process.id;
    design_.append_process_record(std::move(process));
    if (conversion->effective) {
        VhdlSignalResolver resolver {
            process_id, program.program.input, program.program.output, { } };
        resolver.sources = std::move(program.sources);
        design_.signals_[destination].vhdl_resolver = std::move(resolver);
        add_vhdl_formal_driving_mirror(destination);
        return true;
    }
    if (resolved || conversion->slice) {
        // The converted value is one driver of the resolved actual, or of a
        // subelement of the actual: a halted stub owns that driver, and the
        // runtime assigns it the program's result in the cycle a source
        // changes.
        const auto stub_index = design_.process_count();
        Process stub;
        stub.id = static_cast<ProcessId>(stub_index);
        if (static_cast<std::size_t>(stub.id) != stub_index) {
            throw std::length_error { "too many SimIR processes" };
        }
        const auto& name = design_.signal_info_[destination].name;
        stub.name = name.substr(0U, name.find_last_of('.') + 1U)
            + "conversion_driver_" + std::to_string(stub_index);
        stub.language_standard = owner.standard;
        stub.compatibility_profile = owner.compatibility_profile;
        stub.initialize = false;
        stub.driver_regions.push_back(conversion->slice
                ? Process::DriverRegion { destination,
                      conversion->slice->first, conversion->slice->second,
                      false }
                : Process::DriverRegion { destination, 0U, 0U, true });
        stub.operations.emplace_back(Halt { });
        design_.append_process_record(std::move(stub));
        VhdlConversionDriver driver { static_cast<ProcessId>(stub_index),
            process_id, program.program.output, std::move(program.sources) };
        if (conversion->slice) {
            driver.offset = conversion->slice->first;
        }
        design_.signals_[destination].vhdl_conversion_drivers.push_back(
            std::move(driver));
        return true;
    }
    VhdlSignalResolver resolver {
        process_id, program.program.input, program.program.output, { } };
    resolver.sources = std::move(program.sources);
    design_.signals_[conversion->destination].vhdl_resolver
        = std::move(resolver);
    return true;
}

bool HierarchyBuilder::lower_compiled_vhdl_input_conversion(
    Lowerer& lowerer,
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId actual,
    const SignalId destination,
    const std::string_view path,
    const std::optional<semantic::vhdl::SubtypeIndication>& context,
    const semantic::vhdl::Unit& owner)
{
    if (!vhdl_conversion_actual(specialization, actual)
        || destination >= design_.signals_.size()
        || design_.signals_[destination].vhdl_resolver) {
        return false;
    }
    auto program = lowerer.lower_hir_vhdl_conversion_program(
        actual, destination, path, context);
    if (!program) {
        return false;
    }
    auto& process = program->program.process;
    process.language_standard = owner.standard;
    process.compatibility_profile = owner.compatibility_profile;
    canonicalize_process_operations(process);
    const auto process_id = process.id;
    design_.append_process_record(std::move(process));
    VhdlSignalResolver resolver {
        process_id, program->program.input, program->program.output, { } };
    resolver.sources = std::move(program->sources);
    design_.signals_[destination].vhdl_resolver = std::move(resolver);
    return true;
}

} // namespace fsim::elaboration
