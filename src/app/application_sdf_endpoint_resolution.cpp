// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_endpoint_resolution.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfConstructKind;
    using frontend::SdfIrNode;
    using frontend::SourceSpan;

    struct EndpointSpec {
        std::vector<std::string> segments;
        std::optional<SdfEndpointSelect> select;
        std::string edge_identity;
    };

    struct Candidate {
        std::string path;
        runtime::simir::SignalId signal { };
        std::size_t width { };
        bool port { };
        frontend::PortDirection direction { frontend::PortDirection::Unknown };
        SdfEndpointObjectKind kind { SdfEndpointObjectKind::HdlNet };
    };

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] bool ascii_equal(
        const std::string_view left, const std::string_view right,
        const SdfHierarchyCasePolicy policy)
    {
        if (policy == SdfHierarchyCasePolicy::Sensitive)
            return left == right;
        return std::ranges::equal(left, right, [](const char lhs, const char rhs) {
            return std::tolower(static_cast<unsigned char>(lhs))
                == std::tolower(static_cast<unsigned char>(rhs));
        });
    }

    [[nodiscard]] std::optional<std::vector<std::string>> decode_fields(
        const std::string_view atom, const std::string_view prefix)
    {
        if (!atom.starts_with(prefix))
            return std::nullopt;
        std::vector<std::string> fields;
        std::size_t cursor = prefix.size();
        while (cursor < atom.size()) {
            const auto colon = atom.find(':', cursor);
            if (colon == std::string_view::npos || colon == cursor)
                return std::nullopt;
            std::size_t length = 0U;
            const auto parsed = std::from_chars(
                atom.data() + cursor, atom.data() + colon, length);
            if (parsed.ec != std::errc { } || parsed.ptr != atom.data() + colon
                || length > atom.size() - colon - 1U) {
                return std::nullopt;
            }
            cursor = colon + 1U;
            fields.emplace_back(atom.substr(cursor, length));
            cursor += length;
        }
        if (fields.empty())
            return std::nullopt;
        return fields;
    }

    [[nodiscard]] std::optional<std::int64_t> parse_index(
        const std::string_view text)
    {
        std::int64_t value = 0;
        const auto parsed
            = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || parsed.ec != std::errc { }
            || parsed.ptr != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] bool extract_select(
        std::string& segment, std::optional<SdfEndpointSelect>& select)
    {
        if (segment.empty() || segment.back() != ']')
            return true;
        const auto open = segment.rfind('[');
        if (open == std::string::npos || open == 0U)
            return false;
        const std::string_view body { segment.data() + open + 1U,
            segment.size() - open - 2U };
        const auto colon = body.find(':');
        const auto left = parse_index(body.substr(0U, colon));
        const auto right = colon == std::string_view::npos
            ? left
            : parse_index(body.substr(colon + 1U));
        if (!left || !right)
            return false;
        const auto distance = *left >= *right
            ? static_cast<std::uint64_t>(*left - *right)
            : static_cast<std::uint64_t>(*right - *left);
        if (distance >= std::numeric_limits<std::size_t>::max())
            return false;
        select = SdfEndpointSelect { *left, *right,
            static_cast<std::size_t>(distance + 1U) };
        segment.resize(open);
        return !segment.empty();
    }

    [[nodiscard]] std::optional<EndpointSpec> decode_endpoint_atom(
        const std::string_view atom)
    {
        auto fields = decode_fields(atom, "identifier");
        if (!fields)
            fields = decode_fields(atom, "name");
        if (!fields)
            return std::nullopt;
        EndpointSpec result;
        result.segments = std::move(*fields);
        if (!extract_select(result.segments.back(), result.select))
            return std::nullopt;
        return result;
    }

    [[nodiscard]] std::vector<EndpointSpec> decoded_specs(
        const SdfIrNode& node)
    {
        std::vector<EndpointSpec> result;
        for (const auto& atom : node.canonical_atoms) {
            if (auto decoded = decode_endpoint_atom(atom))
                result.push_back(std::move(*decoded));
        }
        return result;
    }

    [[nodiscard]] bool endpoint_kind(const SdfConstructKind kind) noexcept
    {
        static constexpr auto kinds = std::to_array<SdfConstructKind>({
            SdfConstructKind::Iopath,
            SdfConstructKind::Interconnect,
            SdfConstructKind::NetDelay,
            SdfConstructKind::Port,
            SdfConstructKind::Device,
            SdfConstructKind::PathPulse,
            SdfConstructKind::PathPulsePercent,
            SdfConstructKind::Mipd,
            SdfConstructKind::Setup,
            SdfConstructKind::Hold,
            SdfConstructKind::SetupHold,
            SdfConstructKind::Recovery,
            SdfConstructKind::Removal,
            SdfConstructKind::RecRem,
            SdfConstructKind::Skew,
            SdfConstructKind::BidirectSkew,
            SdfConstructKind::Width,
            SdfConstructKind::Period,
            SdfConstructKind::NoChange,
            SdfConstructKind::PathConstraint,
            SdfConstructKind::PeriodConstraint,
            SdfConstructKind::SkewConstraint,
            SdfConstructKind::Arrival,
            SdfConstructKind::Departure,
            SdfConstructKind::Slack,
            SdfConstructKind::Waveform,
        });
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] std::string joined_path(
        const std::vector<std::string>& segments)
    {
        std::string result;
        for (const auto& segment : segments) {
            if (!result.empty())
                result.push_back('.');
            result += segment;
        }
        return result;
    }

    void add_signal_candidates(std::vector<Candidate>& candidates,
        const elaboration::ElaboratedDesign& elaborated)
    {
        candidates.reserve(elaborated.signals().size()
            + elaborated.systemc_objects().size());
        for (const auto& signal : elaborated.signals()) {
            candidates.push_back(Candidate { signal.name, signal.id,
                signal.width, signal.is_port, signal.direction,
                signal.is_port ? SdfEndpointObjectKind::HdlPort
                               : SdfEndpointObjectKind::HdlNet });
        }
        for (const auto& object : elaborated.systemc_objects()) {
            if (!object.signal
                || (object.kind != elaboration::SystemCNamedObjectKind::port
                    && object.kind
                        != elaboration::SystemCNamedObjectKind::signal
                    && object.kind
                        != elaboration::SystemCNamedObjectKind::export_object)) {
                continue;
            }
            const auto info = std::ranges::find(
                elaborated.signals(), *object.signal,
                &elaboration::SignalInfo::id);
            if (info == elaborated.signals().end())
                continue;
            const auto kind = object.kind
                    == elaboration::SystemCNamedObjectKind::port
                ? SdfEndpointObjectKind::SystemCPort
                : SdfEndpointObjectKind::SystemCSignal;
            const auto duplicate = std::ranges::find_if(candidates,
                [&](const Candidate& candidate) {
                    return candidate.path == object.name
                        && candidate.signal == *object.signal;
                });
            if (duplicate != candidates.end()) {
                duplicate->kind = kind;
                duplicate->port
                    = object.kind == elaboration::SystemCNamedObjectKind::port;
            } else {
                candidates.push_back(Candidate { object.name, *object.signal,
                    info->width,
                    object.kind == elaboration::SystemCNamedObjectKind::port,
                    info->direction, kind });
            }
        }
        std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
            if (left.path != right.path)
                return left.path < right.path;
            return left.signal < right.signal;
        });
    }

    [[nodiscard]] bool has_instance(const SdfResolvedInstance& target,
        const elaboration::ElaboratedDesign& elaborated)
    {
        if (target.kind == SdfResolvedInstanceKind::Hdl) {
            return std::ranges::any_of(elaborated.specializations(),
                [&](const auto& specialization) {
                    const bool language_matches
                        = (target.language == SdfScopeRootLanguage::Vhdl
                              && specialization.language
                                  == frontend::Language::Vhdl2008)
                        || (target.language == SdfScopeRootLanguage::Verilog
                            && specialization.language
                                == frontend::Language::Verilog2005)
                        || (target.language
                                == SdfScopeRootLanguage::SystemVerilog
                            && specialization.language
                                == frontend::Language::SystemVerilog2017);
                    return specialization.id == target.declaration_id
                        && specialization.instance == target.instance_path
                        && specialization.unit == target.unit_identity
                        && language_matches;
                });
        }
        return std::ranges::any_of(elaborated.systemc_instances(),
            [&](const auto& instance) {
                return instance.id == target.declaration_id
                    && instance.instance == target.instance_path
                    && instance.target == target.unit_identity;
            });
    }

    [[nodiscard]] std::vector<const Candidate*> match_candidates(
        const EndpointSpec& spec, const SdfResolvedInstance& target,
        const std::vector<Candidate>& candidates)
    {
        const auto local = joined_path(spec.segments);
        std::vector<std::string> alternatives;
        alternatives.push_back(target.instance_path + '.' + local);
        if (!ascii_equal(local, target.instance_path, target.case_policy))
            alternatives.push_back(local);
        std::vector<const Candidate*> result;
        for (const auto& candidate : candidates) {
            if (std::ranges::any_of(alternatives,
                    [&](const std::string& path) {
                        return ascii_equal(
                            candidate.path, path, target.case_policy);
                    })) {
                if (!std::ranges::any_of(result,
                        [&](const Candidate* prior) {
                            return prior->signal == candidate.signal
                                && prior->path == candidate.path;
                        })) {
                    result.push_back(&candidate);
                }
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<EndpointSpec> default_device_specs(
        const SdfResolvedInstance& target,
        const std::vector<Candidate>& candidates)
    {
        std::vector<EndpointSpec> result;
        const auto prefix = target.instance_path + '.';
        for (const auto& candidate : candidates) {
            if (!candidate.path.starts_with(prefix) || !candidate.port
                || candidate.direction != frontend::PortDirection::Output) {
                continue;
            }
            const auto local = candidate.path.substr(prefix.size());
            if (!std::ranges::any_of(result,
                    [&](const EndpointSpec& prior) {
                        return prior.segments.size() == 1U
                            && prior.segments.front() == local;
                    })) {
                result.push_back(EndpointSpec { { local }, { }, { } });
            }
        }
        return result;
    }

    [[nodiscard]] std::string candidate_summary(
        const SdfResolvedInstance& target,
        const std::vector<Candidate>& candidates,
        const SdfEndpointResolutionLimits& limits)
    {
        std::string result;
        std::size_t emitted = 0U;
        const auto prefix = target.instance_path + '.';
        for (const auto& candidate : candidates) {
            if (emitted >= limits.max_reported_candidates)
                break;
            if (!candidate.path.starts_with(prefix))
                continue;
            if (!result.empty())
                result += ", ";
            result += candidate.path;
            ++emitted;
        }
        return result.empty() ? "<none>" : result;
    }

    [[nodiscard]] SdfEndpointRole initial_role(
        const SdfConstructKind kind, const std::size_t index,
        const Candidate& candidate)
    {
        static constexpr auto path_kinds = std::to_array<SdfConstructKind>({
            SdfConstructKind::Iopath,
            SdfConstructKind::PathPulse,
            SdfConstructKind::PathPulsePercent,
        });
        if (std::ranges::find(path_kinds, kind) != path_kinds.end()) {
            return index == 0U ? SdfEndpointRole::Input
                               : SdfEndpointRole::Output;
        }
        if (kind == SdfConstructKind::Interconnect) {
            return index == 0U ? SdfEndpointRole::InterconnectSource
                               : SdfEndpointRole::InterconnectDestination;
        }
        if (kind == SdfConstructKind::NetDelay
            || kind == SdfConstructKind::Mipd) {
            return SdfEndpointRole::Net;
        }
        if (kind == SdfConstructKind::Device)
            return SdfEndpointRole::Device;
        if (kind == SdfConstructKind::Port) {
            return candidate.direction == frontend::PortDirection::Output
                ? SdfEndpointRole::Output
                : SdfEndpointRole::Input;
        }
        if (kind == SdfConstructKind::Width
            || kind == SdfConstructKind::Period) {
            return SdfEndpointRole::TimingReference;
        }
        return index == 0U ? SdfEndpointRole::TimingData
                           : SdfEndpointRole::TimingReference;
    }

    void bind_conversion(SdfResolvedEndpoint& endpoint,
        const elaboration::ElaboratedDesign& elaborated,
        const SdfHierarchyCasePolicy policy)
    {
        for (const auto& conversion : elaborated.boundary_conversions()) {
            if (!ascii_equal(conversion.path, endpoint.object_path, policy))
                continue;
            if (conversion.formal_signal == endpoint.signal) {
                endpoint.conversion = conversion.kind;
                endpoint.conversion_peer = conversion.actual_signal;
                return;
            }
            if (conversion.actual_signal == endpoint.signal) {
                endpoint.conversion = conversion.kind;
                endpoint.conversion_peer = conversion.formal_signal;
                return;
            }
        }
    }

    [[nodiscard]] std::vector<const SdfIrNode*> children_of(
        const SdfIrNode& node,
        const std::vector<std::vector<const SdfIrNode*>>& children)
    {
        if (node.id >= children.size())
            return { };
        return children[static_cast<std::size_t>(node.id)];
    }

    void append_decoded_specs(
        std::vector<EndpointSpec>& destination, const SdfIrNode& node)
    {
        auto decoded = decoded_specs(node);
        destination.insert(destination.end(),
            std::make_move_iterator(decoded.begin()),
            std::make_move_iterator(decoded.end()));
    }

    void append_edge_specs(
        std::vector<EndpointSpec>& specs, const SdfIrNode& edge)
    {
        auto decoded = decoded_specs(edge);
        for (auto& spec : decoded) {
            spec.edge_identity = edge.canonical_identity;
            specs.push_back(std::move(spec));
        }
    }

    void append_conditional_specs(std::vector<EndpointSpec>& specs,
        std::vector<EndpointSpec>& condition_specs,
        const SdfIrNode& conditional,
        const std::vector<std::vector<const SdfIrNode*>>& children)
    {
        auto decoded = decoded_specs(conditional);
        if (!decoded.empty()) {
            specs.push_back(std::move(decoded.back()));
            decoded.pop_back();
            condition_specs.insert(condition_specs.end(),
                std::make_move_iterator(decoded.begin()),
                std::make_move_iterator(decoded.end()));
        }
        for (const auto* expression : children_of(conditional, children)) {
            if (expression->kind == SdfConstructKind::ConditionExpression)
                append_decoded_specs(condition_specs, *expression);
        }
    }

    void append_condition_specs(std::vector<EndpointSpec>& condition_specs,
        const SdfIrNode& condition,
        const std::vector<std::vector<const SdfIrNode*>>& children)
    {
        append_decoded_specs(condition_specs, condition);
        for (const auto* expression : children_of(condition, children))
            append_decoded_specs(condition_specs, *expression);
    }

    void append_child_endpoint_specs(std::vector<EndpointSpec>& specs,
        std::vector<EndpointSpec>& condition_specs, const SdfIrNode& node,
        const std::vector<std::vector<const SdfIrNode*>>& children)
    {
        for (const auto* child : children_of(node, children)) {
            if (child->kind == SdfConstructKind::Edge) {
                append_edge_specs(specs, *child);
            } else if (child->kind == SdfConstructKind::Conditional) {
                append_conditional_specs(
                    specs, condition_specs, *child, children);
            } else if (child->kind == SdfConstructKind::StampCondition
                || child->kind == SdfConstructKind::CheckCondition
                || child->kind == SdfConstructKind::ConditionExpression) {
                append_condition_specs(condition_specs, *child, children);
            }
        }
    }

    [[nodiscard]] std::string ancestor_condition(
        const SdfIrNode& node, const frontend::SdfIr& ir,
        const std::vector<std::vector<const SdfIrNode*>>& children,
        std::vector<EndpointSpec>& condition_specs)
    {
        auto parent_id = node.parent_id;
        while (parent_id != 0U) {
            const auto* parent = ir.find_node(parent_id);
            if (!parent)
                break;
            if (parent->kind == SdfConstructKind::Conditional
                || parent->kind == SdfConstructKind::ConditionalElse) {
                auto direct = decoded_specs(*parent);
                condition_specs.insert(condition_specs.end(),
                    std::make_move_iterator(direct.begin()),
                    std::make_move_iterator(direct.end()));
                for (const auto* child : children_of(*parent, children)) {
                    if (child->kind != SdfConstructKind::ConditionExpression)
                        continue;
                    auto decoded = decoded_specs(*child);
                    condition_specs.insert(condition_specs.end(),
                        std::make_move_iterator(decoded.begin()),
                        std::make_move_iterator(decoded.end()));
                }
                return parent->canonical_identity;
            }
            parent_id = parent->parent_id;
        }
        return { };
    }

    [[nodiscard]] std::optional<runtime::simir::ModuleTimingCheckKind>
    timing_kind(const SdfConstructKind kind)
    {
        using Kind = runtime::simir::ModuleTimingCheckKind;
        static constexpr auto kinds
            = std::to_array<std::pair<SdfConstructKind, Kind>>({
                { SdfConstructKind::Setup, Kind::setup },
                { SdfConstructKind::Hold, Kind::hold },
                { SdfConstructKind::SetupHold, Kind::setuphold },
                { SdfConstructKind::Recovery, Kind::recovery },
                { SdfConstructKind::Removal, Kind::removal },
                { SdfConstructKind::RecRem, Kind::recrem },
                { SdfConstructKind::Skew, Kind::skew },
                { SdfConstructKind::BidirectSkew, Kind::fullskew },
                { SdfConstructKind::Width, Kind::width },
                { SdfConstructKind::Period, Kind::period },
                { SdfConstructKind::NoChange, Kind::nochange },
            });
        const auto found = std::ranges::find(kinds, kind, &decltype(kinds)::value_type::first);
        return found == kinds.end() ? std::nullopt
                                    : std::optional { found->second };
    }

    [[nodiscard]] std::optional<std::uint64_t> packed_index_offset(
        const std::int64_t index, const elaboration::SignalInfo& signal)
    {
        if (!signal.packed_range) {
            return index == 0 && signal.width == 1U
                ? std::optional<std::uint64_t> { 0U }
                : std::nullopt;
        }
        const auto& range = *signal.packed_range;
        const auto low = std::min(range.left, range.right);
        const auto high = std::max(range.left, range.right);
        if (index < low || index > high)
            return std::nullopt;
        return range.descending
            ? static_cast<std::uint64_t>(index)
                - static_cast<std::uint64_t>(range.right)
            : static_cast<std::uint64_t>(range.right)
                - static_cast<std::uint64_t>(index);
    }

    template <typename Terminal>
    [[nodiscard]] bool terminal_matches(
        const SdfResolvedEndpoint& endpoint,
        const Terminal& terminal,
        const elaboration::ElaboratedDesign& elaborated)
    {
        if (endpoint.signal != terminal.signal)
            return false;
        const auto signal = std::ranges::find(elaborated.signals(),
            endpoint.signal, &elaboration::SignalInfo::id);
        if (signal == elaborated.signals().end())
            return false;
        if (!endpoint.select) {
            return terminal.offset == 0U && terminal.width == signal->width;
        }
        const auto left = packed_index_offset(endpoint.select->left, *signal);
        const auto right = packed_index_offset(endpoint.select->right, *signal);
        return left && right
            && terminal.offset == std::min(*left, *right)
            && terminal.width == endpoint.select->width;
    }

    [[nodiscard]] std::optional<std::string_view> edge_token(
        const std::string_view identity)
    {
        constexpr std::string_view prefix = "edge{";
        if (!identity.starts_with(prefix) || !identity.ends_with('}')
            || identity.size() <= prefix.size() + 1U) {
            return std::nullopt;
        }
        return identity.substr(
            prefix.size(), identity.size() - prefix.size() - 1U);
    }

    [[nodiscard]] bool path_edge_matches(
        const SdfResolvedNodeEndpoints& resolved,
        const elaboration::VerilogSpecifyPathInfo& path)
    {
        const auto qualified = std::ranges::find_if(resolved.endpoints,
            [](const SdfResolvedEndpoint& endpoint) {
                return endpoint.role != SdfEndpointRole::Condition
                    && !endpoint.edge_identity.empty();
            });
        if (qualified == resolved.endpoints.end())
            return path.source_edge == frontend::VerilogSpecifyEdge::None;
        const auto token = edge_token(qualified->edge_identity);
        if (!token)
            return false;
        if (*token == "posedge")
            return path.source_edge == frontend::VerilogSpecifyEdge::Posedge;
        if (*token == "negedge")
            return path.source_edge == frontend::VerilogSpecifyEdge::Negedge;
        return path.source_edge == frontend::VerilogSpecifyEdge::Edge;
    }

    [[nodiscard]] bool path_condition_matches(
        const SdfResolvedNodeEndpoints& resolved,
        const elaboration::VerilogSpecifyPathInfo& path)
    {
        if (resolved.condition_identity.empty())
            return !path.conditional && !path.ifnone;
        const std::string else_prefix
            = std::string {
                  frontend::to_string(SdfConstructKind::ConditionalElse) }
            + "{";
        if (resolved.condition_identity.starts_with(else_prefix))
            return path.ifnone;
        return path.conditional && !path.ifnone;
    }

    [[nodiscard]] bool event_edge_matches(
        const SdfResolvedEndpoint& endpoint,
        const runtime::simir::ModuleTimingEvent& event)
    {
        if (endpoint.edge_identity.empty())
            return event.edge == runtime::simir::ModulePathEdge::none;
        const auto token = edge_token(endpoint.edge_identity);
        if (!token)
            return false;
        if (*token == "posedge")
            return event.edge == runtime::simir::ModulePathEdge::posedge;
        if (*token == "negedge")
            return event.edge == runtime::simir::ModulePathEdge::negedge;
        return event.edge == runtime::simir::ModulePathEdge::edge
            && std::ranges::any_of(event.edge_descriptors,
                [&](const std::string_view descriptor) {
                    return ascii_equal(descriptor, *token,
                        SdfHierarchyCasePolicy::AsciiInsensitive);
                });
    }

    [[nodiscard]] bool event_matches(
        const SdfResolvedEndpoint& endpoint,
        const runtime::simir::ModuleTimingEvent& event,
        const elaboration::ElaboratedDesign& elaborated)
    {
        return terminal_matches(endpoint, event.terminal, elaborated)
            && event_edge_matches(endpoint, event);
    }

    struct PathEndpointBinding {
        std::size_t source { };
        std::size_t destination { };
        friend bool operator==(
            const PathEndpointBinding&, const PathEndpointBinding&) = default;
    };

    [[nodiscard]] std::optional<PathEndpointBinding> path_endpoint_binding(
        const SdfResolvedNodeEndpoints& resolved,
        const elaboration::VerilogSpecifyPathInfo& path,
        const elaboration::ElaboratedDesign& elaborated)
    {
        std::optional<PathEndpointBinding> binding;
        const bool edge_qualified = std::ranges::any_of(resolved.endpoints,
            [](const SdfResolvedEndpoint& endpoint) {
                return !endpoint.edge_identity.empty();
            });
        for (std::size_t source = 0U; source < resolved.endpoints.size();
             ++source) {
            const auto& source_endpoint = resolved.endpoints[source];
            if (source_endpoint.role == SdfEndpointRole::Condition
                || (edge_qualified && source_endpoint.edge_identity.empty())
                || (!edge_qualified
                    && source_endpoint.role != SdfEndpointRole::Input)
                || !std::ranges::any_of(path.sources,
                    [&](const auto& terminal) {
                        return terminal_matches(
                            source_endpoint, terminal, elaborated);
                    })) {
                continue;
            }
            for (std::size_t destination = 0U;
                 destination < resolved.endpoints.size(); ++destination) {
                const auto& destination_endpoint = resolved.endpoints[destination];
                if (source == destination
                    || destination_endpoint.role == SdfEndpointRole::Condition
                    || (edge_qualified
                        && !destination_endpoint.edge_identity.empty())
                    || (!edge_qualified
                        && destination_endpoint.role != SdfEndpointRole::Output)
                    || !std::ranges::any_of(path.destinations,
                        [&](const auto& terminal) {
                            return terminal_matches(
                                destination_endpoint, terminal, elaborated);
                        })) {
                    continue;
                }
                const PathEndpointBinding candidate { source, destination };
                if (binding && *binding != candidate)
                    return std::nullopt;
                binding = candidate;
            }
        }
        return binding;
    }

    struct TimingEndpointBinding {
        std::size_t reference { };
        std::optional<std::size_t> data;
        friend bool operator==(const TimingEndpointBinding&,
            const TimingEndpointBinding&) = default;
    };

    [[nodiscard]] std::optional<TimingEndpointBinding> timing_endpoint_binding(
        const SdfResolvedNodeEndpoints& resolved,
        const runtime::simir::ModuleTimingCheck& check,
        const elaboration::ElaboratedDesign& elaborated)
    {
        std::optional<TimingEndpointBinding> binding;
        const bool edge_qualified = std::ranges::any_of(resolved.endpoints,
            [](const SdfResolvedEndpoint& endpoint) {
                return !endpoint.edge_identity.empty();
            });
        for (std::size_t reference = 0U;
             reference < resolved.endpoints.size(); ++reference) {
            const auto& reference_endpoint = resolved.endpoints[reference];
            if (reference_endpoint.role == SdfEndpointRole::Condition
                || (!edge_qualified
                    && reference_endpoint.role
                        != SdfEndpointRole::TimingReference)
                || !event_matches(
                    reference_endpoint, check.reference, elaborated)) {
                continue;
            }
            if (!check.data) {
                const TimingEndpointBinding candidate { reference, std::nullopt };
                if (binding && *binding != candidate)
                    return std::nullopt;
                binding = candidate;
                continue;
            }
            for (std::size_t data = 0U; data < resolved.endpoints.size();
                 ++data) {
                const auto& data_endpoint = resolved.endpoints[data];
                if (reference == data
                    || data_endpoint.role == SdfEndpointRole::Condition
                    || (!edge_qualified
                        && data_endpoint.role != SdfEndpointRole::TimingData)
                    || !event_matches(data_endpoint, *check.data, elaborated)) {
                    continue;
                }
                const TimingEndpointBinding candidate { reference, data };
                if (binding && *binding != candidate)
                    return std::nullopt;
                binding = candidate;
            }
        }
        return binding;
    }

    [[nodiscard]] bool link_specify_path(SdfResolvedNodeEndpoints& resolved,
        const SdfResolvedInstance& target,
        const elaboration::ElaboratedDesign& elaborated,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        if (resolved.construct_kind != SdfConstructKind::Iopath
            && resolved.construct_kind != SdfConstructKind::PathPulse
            && resolved.construct_kind
                != SdfConstructKind::PathPulsePercent) {
            return true;
        }
        const elaboration::VerilogSpecifyPathInfo* match = nullptr;
        std::optional<PathEndpointBinding> match_binding;
        for (const auto& path : elaborated.verilog_specify_paths()) {
            if (!ascii_equal(
                    path.instance, target.instance_path, target.case_policy)
                || !path_edge_matches(resolved, path)
                || !path_condition_matches(resolved, path)) {
                continue;
            }
            const auto binding
                = path_endpoint_binding(resolved, path, elaborated);
            if (!binding)
                continue;
            if (match) {
                diagnose(diagnostics, "FSIM-SDF-ENDPOINT-006",
                    "SDF IOPATH matches multiple elaborated specify paths after exact selection, edge, and condition filtering",
                    span);
                return false;
            }
            match = &path;
            match_binding = binding;
        }
        if (!match)
            return true;
        resolved.specify_path = match->id;
        resolved.endpoints[match_binding->source].role = SdfEndpointRole::Input;
        resolved.endpoints[match_binding->destination].role
            = SdfEndpointRole::Output;
        return true;
    }

    [[nodiscard]] bool link_timing_check(SdfResolvedNodeEndpoints& resolved,
        const SdfResolvedInstance& target,
        const elaboration::ElaboratedDesign& elaborated,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        const auto expected = timing_kind(resolved.construct_kind);
        if (!expected)
            return true;
        const runtime::simir::ModuleTimingCheck* match = nullptr;
        std::optional<TimingEndpointBinding> match_binding;
        const auto identity_prefix
            = "sdf:timingcheck:" + target.instance_path + ":";
        for (const auto& check : elaborated.verilog_timing_checks()) {
            if (check.kind != *expected
                || !check.identity.starts_with(identity_prefix)) {
                continue;
            }
            const auto binding
                = timing_endpoint_binding(resolved, check, elaborated);
            if (!binding)
                continue;
            if (match) {
                diagnose(diagnostics, "FSIM-SDF-ENDPOINT-006",
                    "SDF timing check matches multiple elaborated checks after exact role, selection, and edge filtering",
                    span);
                return false;
            }
            match = &check;
            match_binding = binding;
        }
        if (!match)
            return true;
        resolved.timing_check = match->id;
        resolved.endpoints[match_binding->reference].role
            = SdfEndpointRole::TimingReference;
        if (match_binding->data) {
            resolved.endpoints[*match_binding->data].role
                = SdfEndpointRole::TimingData;
        }
        return true;
    }

    [[nodiscard]] bool resolve_primary_specs(
        SdfResolvedNodeEndpoints& resolved,
        const std::vector<EndpointSpec>& specs,
        const SdfResolvedInstance& target,
        const std::vector<Candidate>& candidates,
        const elaboration::ElaboratedDesign& elaborated,
        const SdfEndpointResolutionLimits& limits,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span,
        std::size_t& endpoint_count)
    {
        for (std::size_t index = 0U; index < specs.size(); ++index) {
            const auto matches = match_candidates(specs[index], target, candidates);
            if (matches.empty()) {
                diagnose(diagnostics, "FSIM-SDF-ENDPOINT-002",
                    "SDF endpoint '" + joined_path(specs[index].segments)
                        + "' has no match beneath " + target.instance_path
                        + "; candidates: "
                        + candidate_summary(target, candidates, limits),
                    span);
                return false;
            }
            if (matches.size() != 1U) {
                diagnose(diagnostics, "FSIM-SDF-ENDPOINT-003",
                    "SDF endpoint '" + joined_path(specs[index].segments)
                        + "' is ambiguous beneath " + target.instance_path,
                    span);
                return false;
            }
            if (endpoint_count >= limits.max_endpoints) {
                diagnose(diagnostics, "FSIM-SDF-ENDPOINT-005",
                    "SDF endpoint resolution exceeds the configured endpoint limit",
                    span);
                return false;
            }
            const auto& candidate = *matches.front();
            SdfResolvedEndpoint endpoint;
            endpoint.role = initial_role(resolved.construct_kind, index, candidate);
            endpoint.object_kind = candidate.kind;
            endpoint.instance_path = target.instance_path;
            endpoint.object_path = candidate.path;
            endpoint.signal = candidate.signal;
            endpoint.object_width = candidate.width;
            endpoint.select = specs[index].select;
            endpoint.language = target.language;
            endpoint.direction = candidate.direction;
            endpoint.edge_identity = specs[index].edge_identity;
            bind_conversion(endpoint, elaborated, target.case_policy);
            resolved.endpoints.push_back(std::move(endpoint));
            ++endpoint_count;
        }
        return true;
    }

    [[nodiscard]] bool resolve_condition_specs(
        SdfResolvedNodeEndpoints& resolved,
        const std::vector<EndpointSpec>& specs,
        const SdfResolvedInstance& target,
        const std::vector<Candidate>& candidates,
        const elaboration::ElaboratedDesign& elaborated,
        const SdfEndpointResolutionLimits& limits,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span,
        std::size_t& endpoint_count)
    {
        std::unordered_set<runtime::simir::SignalId> emitted;
        for (const auto& spec : specs) {
            const auto matches = match_candidates(spec, target, candidates);
            if (matches.size() != 1U || emitted.contains(matches.front()->signal)) {
                continue;
            }
            if (endpoint_count >= limits.max_endpoints) {
                diagnose(diagnostics, "FSIM-SDF-ENDPOINT-005",
                    "SDF endpoint resolution exceeds the configured endpoint limit",
                    span);
                return false;
            }
            const auto& candidate = *matches.front();
            SdfResolvedEndpoint endpoint;
            endpoint.role = SdfEndpointRole::Condition;
            endpoint.object_kind = candidate.kind;
            endpoint.instance_path = target.instance_path;
            endpoint.object_path = candidate.path;
            endpoint.signal = candidate.signal;
            endpoint.object_width = candidate.width;
            endpoint.select = spec.select;
            endpoint.language = target.language;
            endpoint.direction = candidate.direction;
            endpoint.condition_identity = resolved.condition_identity;
            bind_conversion(endpoint, elaborated, target.case_policy);
            emitted.emplace(endpoint.signal);
            resolved.endpoints.push_back(std::move(endpoint));
            ++endpoint_count;
        }
        return true;
    }

    using ChildIndex = std::vector<std::vector<const SdfIrNode*>>;
    using EndpointNodeIndex
        = std::unordered_map<std::uint64_t, std::vector<const SdfIrNode*>>;

    struct ResolutionContext {
        const frontend::SdfIr& ir;
        const std::vector<Candidate>& candidates;
        const elaboration::ElaboratedDesign& elaborated;
        const SdfEndpointResolutionLimits& limits;
        const ChildIndex& children;
        std::vector<SdfResolvedNodeEndpoints>& nodes;
        std::vector<Diagnostic>& diagnostics;
        std::size_t& endpoint_count;
    };

    [[nodiscard]] bool direct_condition_kind(
        const SdfConstructKind kind) noexcept
    {
        static constexpr auto kinds = std::to_array<SdfConstructKind>({
            SdfConstructKind::Conditional,
            SdfConstructKind::StampCondition,
            SdfConstructKind::CheckCondition,
        });
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] std::string node_condition_identity(const SdfIrNode& node,
        const frontend::SdfIr& ir, const ChildIndex& children,
        std::vector<EndpointSpec>& condition_specs)
    {
        std::string identity
            = ancestor_condition(node, ir, children, condition_specs);
        for (const auto* child : children_of(node, children)) {
            if (!direct_condition_kind(child->kind))
                continue;
            if (!identity.empty())
                identity.push_back('|');
            identity += child->canonical_identity;
        }
        return identity;
    }

    [[nodiscard]] bool resolve_node(ResolutionContext& context,
        const SdfIrNode& node, const SdfResolvedInstance& target)
    {
        auto specs = decoded_specs(node);
        std::vector<EndpointSpec> condition_specs;
        append_child_endpoint_specs(
            specs, condition_specs, node, context.children);
        auto condition_identity = node_condition_identity(
            node, context.ir, context.children, condition_specs);
        const bool global_path_pulse
            = node.kind == SdfConstructKind::PathPulsePercent
            && specs.empty();
        if (specs.empty() && node.kind == SdfConstructKind::Device)
            specs = default_device_specs(target, context.candidates);
        if (specs.empty() && !global_path_pulse) {
            diagnose(context.diagnostics, "FSIM-SDF-ENDPOINT-004",
                "SDF endpoint-bearing construct has no decodable endpoint",
                node.span);
            return false;
        }
        if (context.nodes.size() >= context.limits.max_nodes) {
            diagnose(context.diagnostics, "FSIM-SDF-ENDPOINT-005",
                "SDF endpoint resolution exceeds the configured mapping limit",
                node.span);
            return false;
        }
        SdfResolvedNodeEndpoints resolved;
        resolved.node_id = node.id;
        resolved.cell_id = node.cell_id;
        resolved.construct_kind = node.kind;
        resolved.target_instance_path = target.instance_path;
        resolved.condition_identity = std::move(condition_identity);
        if (!global_path_pulse) {
            if (!resolve_primary_specs(resolved, specs, target,
                    context.candidates, context.elaborated, context.limits,
                    context.diagnostics, node.span,
                    context.endpoint_count)) {
                return false;
            }
            if (!resolve_condition_specs(resolved, condition_specs, target,
                    context.candidates, context.elaborated, context.limits,
                    context.diagnostics, node.span,
                    context.endpoint_count)) {
                return false;
            }
        }
        if (!link_specify_path(resolved, target, context.elaborated,
                context.diagnostics, node.span)
            || !link_timing_check(resolved, target, context.elaborated,
                context.diagnostics, node.span)) {
            return false;
        }
        context.nodes.push_back(std::move(resolved));
        return true;
    }

    [[nodiscard]] bool resolve_target(ResolutionContext& context,
        const std::vector<const SdfIrNode*>& nodes,
        const SdfResolvedInstance& target, const SourceSpan& cell_span)
    {
        if (!has_instance(target, context.elaborated)) {
            diagnose(context.diagnostics, "FSIM-SDF-ENDPOINT-001",
                "SDF cell target is stale relative to the elaborated design",
                cell_span);
            return false;
        }
        for (const auto* node : nodes) {
            if (!resolve_node(context, *node, target))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool resolve_cell(ResolutionContext& context,
        const SdfResolvedCell& cell, const EndpointNodeIndex& nodes_by_cell)
    {
        static const std::vector<const SdfIrNode*> no_nodes;
        const auto found = nodes_by_cell.find(cell.cell_id);
        const auto& nodes
            = found == nodes_by_cell.end() ? no_nodes : found->second;
        const auto* ir_cell = context.ir.find_cell(cell.cell_id);
        const SourceSpan span = ir_cell ? ir_cell->span : SourceSpan { };
        for (const auto& target : cell.targets) {
            if (!resolve_target(context, nodes, target, span))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool build_endpoint_indexes(const frontend::SdfIr& ir,
        ChildIndex& children, EndpointNodeIndex& nodes_by_cell,
        std::vector<Diagnostic>& diagnostics)
    {
        children.resize(ir.nodes().size() + 1U);
        nodes_by_cell.reserve(ir.cells().size());
        for (const auto& node : ir.nodes()) {
            if (node.parent_id >= children.size()) {
                diagnose(diagnostics, "FSIM-SDF-ENDPOINT-001",
                    "SDF endpoint resolution encountered an invalid IR parent",
                    node.span);
                return false;
            }
            children[static_cast<std::size_t>(node.parent_id)].push_back(&node);
            if (endpoint_kind(node.kind))
                nodes_by_cell[node.cell_id].push_back(&node);
        }
        return true;
    }

    [[nodiscard]] std::optional<std::string> resolution_identity(
        const SdfCellResolution& cells,
        const std::vector<SdfResolvedNodeEndpoints>& nodes,
        const std::size_t limit)
    {
        std::string identity = "sdf-endpoint-resolution-v1";
        append_field(identity, cells.semantic_identity());
        for (const auto& node : nodes) {
            append_field(identity, std::to_string(node.node_id));
            append_field(identity, std::to_string(node.cell_id));
            append_field(identity,
                std::to_string(static_cast<unsigned>(node.construct_kind)));
            append_field(identity, node.target_instance_path);
            append_field(identity, node.condition_identity);
            append_field(identity, node.specify_path ? std::to_string(static_cast<std::uint64_t>(*node.specify_path)) : std::string { });
            append_field(identity, node.timing_check ? std::to_string(*node.timing_check) : std::string { });
            for (const auto& endpoint : node.endpoints) {
                append_field(identity,
                    std::to_string(static_cast<unsigned>(endpoint.role)));
                append_field(identity,
                    std::to_string(static_cast<unsigned>(endpoint.object_kind)));
                append_field(identity, endpoint.object_path);
                append_field(identity,
                    std::to_string(static_cast<std::uint64_t>(endpoint.signal)));
                append_field(identity, std::to_string(endpoint.object_width));
                append_field(identity, endpoint.select ? std::to_string(endpoint.select->left) + ":" + std::to_string(endpoint.select->right) : std::string { });
                append_field(identity, endpoint.edge_identity);
                append_field(identity, endpoint.condition_identity);
                append_field(identity, endpoint.conversion ? std::to_string(static_cast<unsigned>(*endpoint.conversion)) : std::string { });
                append_field(identity, endpoint.conversion_peer ? std::to_string(static_cast<std::uint64_t>(*endpoint.conversion_peer)) : std::string { });
            }
            if (identity.size() > limit)
                return std::nullopt;
        }
        return identity.size() <= limit
            ? std::optional<std::string> { std::move(identity) }
            : std::nullopt;
    }
} // namespace

SdfEndpointResolution::SdfEndpointResolution(
    std::shared_ptr<const SdfCellResolution> cells,
    std::vector<SdfResolvedNodeEndpoints> nodes, std::string semantic_identity)
    : cells_(std::move(cells))
    , nodes_(std::move(nodes))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfCellResolution>& SdfEndpointResolution::cells()
    const noexcept
{
    return cells_;
}

std::span<const SdfResolvedNodeEndpoints> SdfEndpointResolution::nodes() const
    noexcept
{
    return nodes_;
}

std::span<const SdfResolvedNodeEndpoints> SdfEndpointResolution::find_node(
    const std::uint64_t node_id) const noexcept
{
    const auto first = std::ranges::lower_bound(
        nodes_, node_id, { }, &SdfResolvedNodeEndpoints::node_id);
    const auto last = std::ranges::upper_bound(
        nodes_, node_id, { }, &SdfResolvedNodeEndpoints::node_id);
    return { first, last };
}

std::string_view SdfEndpointResolution::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfEndpointResolutionResult::ok() const noexcept
{
    return resolution && !frontend::has_errors(diagnostics);
}

SdfEndpointResolutionResult resolve_sdf_endpoints(
    std::shared_ptr<const SdfCellResolution> cells,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfEndpointResolutionLimits limits)
{
    SdfEndpointResolutionResult result;
    if (!cells || !cells->scope() || !cells->scope()->normalized_ir()
        || cells->semantic_identity().empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-ENDPOINT-001",
            "SDF endpoint resolution requires a complete cell resolution",
            { });
        return result;
    }
    const auto& ir = *cells->scope()->normalized_ir();
    if (cells->cells().size() != ir.cells().size()) {
        diagnose(result.diagnostics, "FSIM-SDF-ENDPOINT-001",
            "SDF endpoint resolution encountered an incomplete cell mapping",
            ir.cells().empty() ? SourceSpan { } : ir.cells().front().span);
        return result;
    }
    std::unordered_set<std::uint64_t> cell_ids;
    for (const auto& cell : cells->cells()) {
        const auto* ir_cell = ir.find_cell(cell.cell_id);
        if (!ir_cell || ir_cell->source_identity != cell.source_identity
            || !cell_ids.emplace(cell.cell_id).second) {
            diagnose(result.diagnostics, "FSIM-SDF-ENDPOINT-001",
                "SDF endpoint resolution encountered a stale or duplicate cell mapping",
                ir_cell ? ir_cell->span : SourceSpan { });
            return result;
        }
    }
    if (ir.nodes().size() > limits.max_nodes) {
        diagnose(result.diagnostics, "FSIM-SDF-ENDPOINT-005",
            "SDF endpoint resolution exceeds the configured node limit",
            ir.nodes().empty() ? SourceSpan { } : ir.nodes().front().span);
        return result;
    }

    std::vector<Candidate> candidates;
    add_signal_candidates(candidates, elaborated);
    if (candidates.size() > limits.max_candidates) {
        diagnose(result.diagnostics, "FSIM-SDF-ENDPOINT-005",
            "SDF endpoint resolution exceeds the configured candidate limit",
            ir.nodes().empty() ? SourceSpan { } : ir.nodes().front().span);
        return result;
    }

    ChildIndex children;
    EndpointNodeIndex nodes_by_cell;
    if (!build_endpoint_indexes(
            ir, children, nodes_by_cell, result.diagnostics)) {
        return result;
    }

    std::vector<SdfResolvedNodeEndpoints> resolved_nodes;
    std::size_t endpoint_count = 0U;
    ResolutionContext context { ir, candidates, elaborated, limits, children,
        resolved_nodes, result.diagnostics, endpoint_count };
    for (const auto& cell : cells->cells()) {
        if (!resolve_cell(context, cell, nodes_by_cell))
            return result;
    }
    std::ranges::sort(resolved_nodes,
        [](const SdfResolvedNodeEndpoints& left,
            const SdfResolvedNodeEndpoints& right) {
            if (left.node_id != right.node_id)
                return left.node_id < right.node_id;
            return left.target_instance_path < right.target_instance_path;
        });
    auto identity
        = resolution_identity(*cells, resolved_nodes, limits.max_identity_bytes);
    if (!identity) {
        diagnose(result.diagnostics, "FSIM-SDF-ENDPOINT-005",
            "SDF endpoint semantic identity exceeds the configured byte limit",
            ir.nodes().empty() ? SourceSpan { } : ir.nodes().front().span);
        return result;
    }
    result.resolution = std::make_shared<const SdfEndpointResolution>(
        std::move(cells), std::move(resolved_nodes), std::move(*identity));
    return result;
}

} // namespace fsim::app
