// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include "vhdl_array_boundary.hpp"

#include <cstdlib>
#include <numeric>
#include <unordered_map>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    template <typename Callback>
    class ScopeExit final {
    public:
        explicit ScopeExit(Callback callback)
            : callback_ { std::move(callback) }
        {
        }

        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;

        ~ScopeExit() { callback_(); }

    private:
        Callback callback_;
    };

    template <typename Callback>
    ScopeExit(Callback) -> ScopeExit<Callback>;

    bool belongs_to_hierarchy(
        const std::string_view candidate,
        const std::string_view path)
    {
        return candidate == path
            || (candidate.size() > path.size()
                && candidate.starts_with(path)
                && candidate[path.size()] == '.');
    }

    template <typename SignalMap>
    void adapt_vhdl_array_port_shapes(
        DesignUnit& unit,
        const frontend::Instance& instance,
        const SignalMap& parent_signals,
        const std::span<const SignalInfo> signal_info,
        std::vector<std::pair<std::string, std::string>>& identities)
    {
        if (unit.language != frontend::Language::Vhdl2008
            || unit.ports.empty()) {
            return;
        }
        std::vector<bool> connected(unit.ports.size());
        std::size_t positional = 0;
        for (const auto& connection : instance.connections) {
            std::size_t port_index = unit.ports.size();
            if (connection.port) {
                const auto found = std::ranges::find_if(
                    unit.ports,
                    [&](const auto& port) {
                        return port.name == *connection.port;
                    });
                if (found != unit.ports.end()) {
                    port_index = static_cast<std::size_t>(
                        std::distance(unit.ports.begin(), found));
                }
            } else {
                while (positional < unit.ports.size()
                    && connected[positional]) {
                    ++positional;
                }
                port_index = positional++;
            }
            if (port_index >= unit.ports.size()
                || connected[port_index]
                || connection.kind != frontend::PortActualKind::Expression
                || connection.value.kind
                    != frontend::ExpressionKind::Identifier) {
                continue;
            }
            connected[port_index] = true;
            auto& formal = unit.ports[port_index];
            if (!formal.type.vhdl_array) {
                continue;
            }
            const auto actual = parent_signals.find(connection.value.text);
            if (actual == parent_signals.end()
                || actual->second >= signal_info.size()) {
                continue;
            }
            const auto& info = signal_info[actual->second];
            const bool indefinite = !formal.type.vhdl_array->flat_width
                || std::ranges::any_of(
                    formal.type.vhdl_array->dimensions,
                    [](const auto& dimension) {
                        return dimension.unconstrained;
                    });
            if (!indefinite || !info.vhdl_array
                || formal.type.nominal_type != info.nominal_type) {
                continue;
            }
            formal.type.vhdl_array = *info.vhdl_array;
            formal.type.vhdl_array_constraints.clear();
            formal.type.packed_range = info.packed_range;
            formal.type.packed_range_expression.reset();
            formal.type.domain = info.source_domain;
            formal.type.is_signed = info.is_signed;
            identities.emplace_back(
                "__vhdl_port_shape." + formal.name,
                vhdl_array_shape_identity(formal.type));
        }
    }

    struct ResolvedVerilogDefparam {
        const frontend::VerilogDefparamDeclaration* declaration { };
        std::vector<std::string> segments;
        bool matched { };
    };

    struct ConcurrentComponentGroups {
        std::vector<std::vector<std::size_t>> groups;
        std::size_t dependency_components { };
    };

    [[maybe_unused]] ConcurrentComponentGroups concurrent_dependency_components(
        const std::span<const frontend::Statement> statements,
        const std::unordered_map<std::string, SignalId>& signals)
    {
        const auto one_component = [&] {
            ConcurrentComponentGroups result;
            result.groups.resize(1);
            result.groups.front().resize(statements.size());
            std::iota(
                result.groups.front().begin(),
                result.groups.front().end(),
                std::size_t { });
            result.dependency_components = 1U;
            return result;
        };
        const auto static_expression_key = [](
                                               const auto& self,
                                               const frontend::Expression& expression)
            -> std::optional<std::string> {
            if (expression.kind == frontend::ExpressionKind::IntegerLiteral
                || expression.kind == frontend::ExpressionKind::LogicLiteral
                || expression.kind == frontend::ExpressionKind::BooleanLiteral) {
                return expression.text;
            }
            if ((expression.kind == frontend::ExpressionKind::Unary
                    || expression.kind == frontend::ExpressionKind::Binary)
                && !expression.operands.empty()) {
                auto result = expression.text + "(";
                for (std::size_t index = 0;
                     index < expression.operands.size(); ++index) {
                    const auto operand = self(self, expression.operands[index]);
                    if (!operand) {
                        return std::nullopt;
                    }
                    if (index != 0U) {
                        result += ",";
                    }
                    result += *operand;
                }
                result += ")";
                return result;
            }
            return std::nullopt;
        };
        const auto expression_key = [&static_expression_key](
                                        const auto& self,
                                        const frontend::Expression& expression)
            -> std::optional<std::string> {
            if (expression.kind == frontend::ExpressionKind::Identifier) {
                return expression.text;
            }
            if ((expression.kind == frontend::ExpressionKind::Index
                    || expression.kind == frontend::ExpressionKind::Slice)
                && !expression.operands.empty()) {
                auto base = self(self, expression.operands.front());
                if (!base) {
                    return std::nullopt;
                }
                auto result = *base + "[";
                for (std::size_t index = 1U;
                     index < expression.operands.size(); ++index) {
                    const auto selector = static_expression_key(
                        static_expression_key, expression.operands[index]);
                    if (!selector) {
                        return std::nullopt;
                    }
                    if (index != 1U) {
                        result += ":";
                    }
                    result += *selector;
                }
                result += "]";
                return result;
            }
            return std::nullopt;
        };
        const auto contains_call = [](const auto& self,
                                      const frontend::Expression& expression)
            -> bool {
            return (expression.kind == frontend::ExpressionKind::Call
                    && expression.text != "?:")
                || std::ranges::any_of(
                    expression.operands,
                    [&](const auto& operand) {
                        return self(self, operand);
                    });
        };
        const auto collect_reads = [&expression_key](
                                       const auto& self,
                                       const frontend::Expression& expression,
                                       std::vector<std::string>& reads) -> void {
            if (expression.kind == frontend::ExpressionKind::Identifier) {
                reads.push_back(expression.text);
                return;
            }
            if (expression.kind == frontend::ExpressionKind::Index
                || expression.kind == frontend::ExpressionKind::Slice) {
                if (const auto key = expression_key(
                        expression_key, expression)) {
                    reads.push_back(*key);
                    return;
                }
            }
            for (const auto& operand : expression.operands) {
                self(self, operand, reads);
            }
        };
        const auto base = [](const std::string& key) {
            const auto selected = key.find('[');
            return key.substr(0, selected);
        };
        const auto overlaps = [&base](const std::string& left,
                                      const std::string& right) {
            if (left == right) {
                return true;
            }
            const auto selected_prefix = [](const std::string& prefix,
                                             const std::string& value) {
                return value.size() > prefix.size()
                    && value.starts_with(prefix)
                    && value[prefix.size()] == '[';
            };
            if (selected_prefix(left, right)
                || selected_prefix(right, left)) {
                return true;
            }
            if (base(left) != base(right)) {
                return false;
            }
            return left.find('[') == std::string::npos
                || right.find('[') == std::string::npos;
        };

        std::vector<std::string> targets;
        std::vector<std::vector<std::string>> reads(statements.size());
        targets.reserve(statements.size());
        for (std::size_t index = 0; index < statements.size(); ++index) {
            const auto& statement = statements[index];
            if (contains_call(contains_call, statement.value)) {
                return one_component();
            }
            const auto target = expression_key(
                expression_key, statement.target);
            if (!target) {
                return one_component();
            }
            targets.push_back(*target);
            collect_reads(collect_reads, statement.value, reads[index]);
            for (std::size_t operand = 1U;
                 operand < statement.target.operands.size(); ++operand) {
                if (contains_call(
                        contains_call, statement.target.operands[operand])) {
                    return one_component();
                }
                collect_reads(
                    collect_reads,
                    statement.target.operands[operand],
                    reads[index]);
            }
        }

        std::vector<std::size_t> parent(statements.size());
        std::iota(parent.begin(), parent.end(), std::size_t { });
        const auto find_root = [&](const auto& self, std::size_t node)
            -> std::size_t {
            if (parent[node] != node) {
                parent[node] = self(self, parent[node]);
            }
            return parent[node];
        };
        const auto connect = [&](const std::size_t left,
                                 const std::size_t right) {
            const auto left_root = find_root(find_root, left);
            const auto right_root = find_root(find_root, right);
            if (left_root != right_root) {
                parent[right_root] = left_root;
            }
        };
        for (std::size_t left = 0; left < statements.size(); ++left) {
            for (std::size_t right = left + 1U;
                 right < statements.size(); ++right) {
                const auto reads_target = [&](const std::size_t reader,
                                              const std::size_t writer) {
                    return std::ranges::any_of(
                        reads[reader], [&](const auto& read) {
                            return overlaps(read, targets[writer]);
                        });
                };
                if (base(targets[left]) == base(targets[right])
                    || reads_target(left, right)
                    || reads_target(right, left)) {
                    connect(left, right);
                }
            }
        }
        std::vector<std::vector<std::size_t>> components;
        std::unordered_map<std::size_t, std::size_t> component_by_root;
        for (std::size_t index = 0; index < statements.size(); ++index) {
            const auto root = find_root(find_root, index);
            const auto [found, inserted] = component_by_root.try_emplace(
                root, components.size());
            if (inserted) {
                components.emplace_back();
            }
            components[found->second].push_back(index);
        }

        // Dependency components must remain intact so that forwarding chains
        // are evaluated together. Independent components, however, should not
        // become separate scheduler processes when they wake on the same
        // external signals. Coalesce those components by the exact elaborated
        // signal identities in their external sensitivity sets. This retains
        // the useful partitioning while avoiding a process/resume explosion.
        ConcurrentComponentGroups result;
        result.dependency_components = components.size();
        std::unordered_map<std::string, std::size_t> group_by_sensitivity;
        for (const auto& component : components) {
            std::vector<std::string> component_targets;
            component_targets.reserve(component.size());
            for (const auto statement : component) {
                component_targets.push_back(targets[statement]);
            }

            std::vector<SignalId> sensitivity;
            for (const auto statement : component) {
                for (const auto& read : reads[statement]) {
                    if (std::ranges::any_of(
                            component_targets,
                            [&](const auto& target) {
                                return overlaps(read, target);
                            })) {
                        continue;
                    }
                    const auto found = signals.find(base(read));
                    if (found != signals.end()) {
                        sensitivity.push_back(found->second);
                    }
                }
            }
            std::ranges::sort(sensitivity);
            sensitivity.erase(
                std::ranges::unique(sensitivity).begin(), sensitivity.end());
            std::string signature;
            for (const auto signal : sensitivity) {
                signature += std::to_string(
                    static_cast<std::size_t>(signal));
                signature += ';';
            }
            const auto [found, inserted] = group_by_sensitivity.try_emplace(
                std::move(signature), result.groups.size());
            if (inserted) {
                result.groups.emplace_back();
            }
            auto& group = result.groups[found->second];
            group.insert(group.end(), component.begin(), component.end());
        }
        for (auto& group : result.groups) {
            std::ranges::sort(group);
        }
        std::ranges::sort(
            result.groups,
            { },
            [](const auto& group) { return group.front(); });
        return result;
    }

    std::optional<std::vector<std::string>> resolve_defparam_path(
        const frontend::VerilogDefparamDeclaration& declaration,
        const SystemVerilogConstantEnvironment& integral_environment,
        const ConstantEnvironment& integer_environment,
        std::string& error)
    {
        std::vector<std::string> result;
        result.reserve(declaration.path.size());
        for (const auto& segment : declaration.path) {
            auto canonical = segment.name;
            for (const auto& index_expression : segment.indices) {
                auto index = evaluate_systemverilog_constant_expression(
                    index_expression,
                    integral_environment,
                    integer_environment,
                    error);
                if (!index) {
                    error = "cannot evaluate hierarchy index: " + error;
                    return std::nullopt;
                }
                const auto integer = index->integer_value();
                if (!integer) {
                    error = "hierarchy index must be a known signed 64-bit integer";
                    return std::nullopt;
                }
                canonical += "[" + std::to_string(*integer) + "]";
            }
            result.push_back(std::move(canonical));
        }
        return result;
    }

    std::optional<std::size_t> defparam_instance_prefix(
        const std::span<const std::string> segments,
        const std::string_view instance_name)
    {
        std::string candidate;
        for (std::size_t index = 0; index + 1U < segments.size(); ++index) {
            if (!candidate.empty()) {
                candidate += '.';
            }
            candidate += segments[index];
            if (candidate == instance_name) {
                return index + 1U;
            }
            if (candidate.size() >= instance_name.size()
                && !instance_name.starts_with(candidate)) {
                break;
            }
        }
        return std::nullopt;
    }

    std::string defparam_path_text(
        const std::span<const std::string> segments)
    {
        std::string result;
        for (const auto& segment : segments) {
            if (!result.empty()) {
                result += '.';
            }
            result += segment;
        }
        return result;
    }

} // namespace

void HierarchyBuilder::canonicalize_process_operations(Process& process)
{
    if (!process_operations_shareable(process)) {
        return;
    }

    std::uint64_t bucket = UINT64_C(1469598103934665603);
    const auto mix = [&](const std::uint64_t value) {
        bucket ^= value;
        bucket *= UINT64_C(1099511628211);
    };
    mix(process.operations.size());
    mix(process.register_count);
    mix(process.string_register_count);
    mix(process.container_register_count);
    for (const auto kind : process.register_value_kinds) {
        mix(static_cast<std::uint64_t>(kind));
    }
    for (const auto& operation : process.operations) {
        mix(operation_group_index(operation));
        mix(operation_alternative_index(operation));
    }

    auto& representatives = process_operation_representatives_[bucket];
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

HierarchyBuilder::HierarchyCheckpoint
HierarchyBuilder::hierarchy_checkpoint(std::string path) const
{
    return {
        std::move(path),
        diagnostics_.size(),
        design_.signals_.size(),
        design_.boundary_conversions_.size(),
        design_.string_objects_.size(),
        design_.container_objects_.size(),
        design_.container_signal_aliases_.size(),
        design_.vhdl_protected_object_info_.size(),
        design_.processes_.size(),
        design_.specializations_.size(),
        design_.udp_tables_.size(),
        design_.verilog_specify_paths_.size(),
        design_.verilog_timing_checks_.size(),
        design_.systemc_instances_.size(),
        design_.systemc_processes_.size(),
        design_.systemc_objects_.size(),
        owned_systemc_instances_.size(),
        stack_.size(),
        boundary_resolver_insertions_.size(),
        vhdl_resolution_kind_insertions_.size(),
        systemverilog_resolution_kind_insertions_.size(),
        next_systemverilog_interface_handle_
    };
}

void HierarchyBuilder::rollback_hierarchy(
    const HierarchyCheckpoint& checkpoint)
{
    design_.signal_info_.resize(checkpoint.signals);
    design_.signals_.resize(checkpoint.signals);
    design_.boundary_conversions_.resize(checkpoint.boundary_conversions);
    design_.string_object_info_.resize(checkpoint.strings);
    design_.string_objects_.resize(checkpoint.strings);
    design_.container_object_info_.resize(checkpoint.containers);
    design_.container_objects_.resize(checkpoint.containers);
    design_.container_signal_aliases_.resize(checkpoint.container_aliases);
    design_.vhdl_protected_object_info_.resize(checkpoint.protected_objects);
    design_.processes_.resize(checkpoint.processes);
    design_.specializations_.resize(checkpoint.specializations);
    design_.udp_tables_.resize(checkpoint.udp_tables);
    design_.verilog_specify_paths_.resize(checkpoint.specify_paths);
    design_.verilog_timing_checks_.resize(checkpoint.timing_checks);
    design_.systemc_instances_.resize(checkpoint.systemc_instances);
    design_.systemc_processes_.resize(checkpoint.systemc_processes);
    design_.systemc_objects_.resize(checkpoint.systemc_objects);

    const auto erase_named_objects = [&](auto& names, const std::size_t size) {
        std::erase_if(names, [&](const auto& entry) {
            return static_cast<std::size_t>(entry.second) >= size
                || belongs_to_hierarchy(entry.first, checkpoint.path);
        });
    };
    erase_named_objects(design_.signal_by_name_, checkpoint.signals);
    erase_named_objects(design_.string_by_name_, checkpoint.strings);
    erase_named_objects(design_.container_by_name_, checkpoint.containers);

    const auto erase_paths = [&](auto& paths) {
        std::erase_if(paths, [&](const auto& entry) {
            return belongs_to_hierarchy(entry, checkpoint.path);
        });
    };
    erase_paths(instance_paths_);
    erase_paths(systemverilog_interface_port_paths_);
    erase_paths(systemverilog_read_only_interface_member_paths_);

    const auto erase_path_map = [&](auto& paths) {
        std::erase_if(paths, [&](const auto& entry) {
            return belongs_to_hierarchy(entry.first, checkpoint.path);
        });
    };
    erase_path_map(vhdl_configurations_by_path_);
    erase_path_map(systemverilog_interface_instances_);
    erase_path_map(systemverilog_interface_handles_);
    erase_path_map(systemverilog_interface_parameter_identities_);
    erase_path_map(systemverilog_interface_modport_views_);
    std::erase_if(udp_table_by_identity_, [&](const auto& entry) {
        return static_cast<std::size_t>(entry.second) >= checkpoint.udp_tables;
    });

    const auto rollback_driver_paths = [&](auto& drivers) {
        std::erase_if(drivers, [&](auto& entry) {
            std::erase_if(entry.second, [&](const auto& driver) {
                if constexpr (requires { driver.path; }) {
                    return belongs_to_hierarchy(driver.path, checkpoint.path);
                } else {
                    return belongs_to_hierarchy(driver, checkpoint.path);
                }
            });
            return entry.second.empty();
        });
    };
    rollback_driver_paths(boundary_driver_paths_);
    rollback_driver_paths(string_boundary_driver_paths_);
    rollback_driver_paths(container_boundary_driver_paths_);
    for (auto index = boundary_resolver_insertions_.size();
        index > checkpoint.boundary_resolver_insertions;
        --index) {
        resolver_by_signal_.erase(boundary_resolver_insertions_[index - 1]);
    }
    boundary_resolver_insertions_.resize(
        checkpoint.boundary_resolver_insertions);
    for (auto index = vhdl_resolution_kind_insertions_.size();
        index > checkpoint.vhdl_resolution_kind_insertions;
        --index) {
        vhdl_resolution_kinds_.erase(
            vhdl_resolution_kind_insertions_[index - 1]);
    }
    vhdl_resolution_kind_insertions_.resize(
        checkpoint.vhdl_resolution_kind_insertions);
    for (auto index = systemverilog_resolution_kind_insertions_.size();
        index > checkpoint.systemverilog_resolution_kind_insertions;
        --index) {
        systemverilog_resolution_kinds_.erase(
            systemverilog_resolution_kind_insertions_[index - 1]);
    }
    systemverilog_resolution_kind_insertions_.resize(
        checkpoint.systemverilog_resolution_kind_insertions);
    std::erase_if(resolver_by_signal_, [&](const auto& entry) {
        return static_cast<std::size_t>(entry.first) >= checkpoint.signals;
    });

    stack_.resize(checkpoint.stack_depth);
    next_systemverilog_interface_handle_ = checkpoint.next_interface_handle;
    std::erase_if(systemc_instances_, [&](const auto& entry) {
        for (auto index = checkpoint.owned_systemc_instances;
            index < owned_systemc_instances_.size();
            ++index) {
            if (entry.second == &owned_systemc_instances_[index]) {
                return true;
            }
        }
        return false;
    });
    owned_systemc_instances_.resize(checkpoint.owned_systemc_instances);
}

void HierarchyBuilder::instantiate(
    DesignUnit& unit,
    const std::string& path,
    SignalMap aliases,
    StringMap string_aliases,
    ContainerMap container_aliases,
    std::unordered_set<SignalId> read_only_signals,
    std::unordered_set<StringObjectId> read_only_strings,
    ConstantEnvironment parameter_environment,
    SystemVerilogConstantEnvironment
        parameter_integral_environment,
    std::vector<std::pair<std::string, std::string>>
        parameter_values,
    std::vector<std::pair<std::string, std::string>>
        parameter_identity_values,
    PackageEnvironment package_environment)
{
    const auto parent_types = unit.language == frontend::Language::Vhdl2008
        ? local_vhdl_type_environment(unit)
        : unit.language
            == frontend::Language::SystemVerilog2017
        ? local_systemverilog_type_environment(unit)
        : NamedTypeEnvironment { };
    ConstantDomainEnvironment parent_domains;
    for (const auto& parameter : unit.parameters) {
        if (parameter.kind
                != frontend::ParameterKind::Value
            || (parameter_environment.find(parameter.name)
                    == parameter_environment.end()
                && parameter_integral_environment.find(
                       parameter.name)
                    == parameter_integral_environment.end())) {
            continue;
        }
        parent_domains.insert_or_assign(
            parameter.name,
            ConstantTypeInfo {
                parameter.type.domain,
                unit.language
                        == frontend::Language::Vhdl2008
                    && !parameter.type
                        .enumeration_literals.empty(),
                parameter.type.nominal_type });
    }
    std::vector<ResolvedVerilogDefparam> resolved_defparams;
    resolved_defparams.reserve(unit.verilog_defparams.size());
    for (const auto& declaration : unit.verilog_defparams) {
        if (unit.language != frontend::Language::Verilog2005
            && unit.language
                != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-DEFPARAM-004",
                "defparam declarations cannot cross a non-Verilog "
                "hierarchy scope",
                declaration.span);
            continue;
        }
        std::string error;
        auto segments = resolve_defparam_path(
            declaration,
            parameter_integral_environment,
            parameter_environment,
            error);
        if (!segments || segments->size() < 2U) {
            report(
                "FSIM-ELAB-DEFPARAM-001",
                "cannot resolve defparam hierarchy path: " + error,
                declaration.span);
            continue;
        }
        if (segments->size() > 2U
            && segments->front() == unit.name) {
            segments->erase(segments->begin());
        }
        resolved_defparams.push_back(
            { &declaration, std::move(*segments), false });
    }
    if (!instance_paths_.insert(path).second) {
        report(
            "FSIM-ELAB-HIER-001",
            "duplicate instance path '" + path + "'",
            unit.span);
        return;
    }
    const auto identity = unit_identity(unit);
    if (std::find(stack_.begin(), stack_.end(), identity) != stack_.end()) {
        report(
            "FSIM-ELAB-HIER-002",
            "recursive instantiation of '" + identity + "' at '" + path
                + "'",
            unit.span);
        return;
    }
    stack_.push_back(identity);
    register_vhdl_resolution_functions(unit);
    register_systemverilog_resolution_functions(unit);

    SignalMap local = std::move(aliases);
    if (unit.language
        == frontend::Language::SystemVerilog2017) {
        for (const auto& [name, signal] : global_root_signals_) {
            local.try_emplace(name, signal);
        }
    }
    StringMap local_string_objects = std::move(string_aliases);
    ContainerMap local_container_objects = std::move(container_aliases);
    std::unordered_set<std::string>
        read_only_container_objects;
    std::unordered_map<
        std::string, const frontend::Type*>
        visible_types;
    std::unordered_map<
        std::string, const frontend::Type*>
        visible_type_marks;
    const auto builtin_scalar = [](
                                    const frontend::ValueDomain domain,
                                    const std::string_view spelling,
                                    const std::optional<frontend::IntegerRange> range = { }) {
        frontend::Type type;
        type.domain = domain;
        type.spelling = spelling;
        type.is_signed = domain == frontend::ValueDomain::Integer;
        type.integer_range = range;
        return type;
    };
    static const auto builtin_integer = builtin_scalar(
        frontend::ValueDomain::Integer,
        "integer",
        frontend::IntegerRange {
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max(), false });
    static const auto builtin_natural = builtin_scalar(
        frontend::ValueDomain::Integer,
        "natural",
        frontend::IntegerRange {
            0, std::numeric_limits<std::int32_t>::max(), false });
    static const auto builtin_positive = builtin_scalar(
        frontend::ValueDomain::Integer,
        "positive",
        frontend::IntegerRange {
            1, std::numeric_limits<std::int32_t>::max(), false });
    static const auto builtin_boolean = builtin_scalar(
        frontend::ValueDomain::Boolean, "boolean");
    static const auto builtin_bit = builtin_scalar(
        frontend::ValueDomain::Bit2, "bit");
    const auto builtin_systemverilog_scalar = [](
                                                  const std::string_view spelling,
                                                  const frontend::SystemVerilogScalarKind kind,
                                                  const frontend::ValueDomain domain,
                                                  const bool is_signed) {
        frontend::Type type;
        type.spelling = spelling;
        type.systemverilog_scalar = kind;
        type.domain = domain;
        type.is_signed = is_signed;
        return type;
    };
    static const auto builtin_shortreal = builtin_systemverilog_scalar(
        "shortreal",
        frontend::SystemVerilogScalarKind::ShortReal,
        frontend::ValueDomain::Bit2,
        true);
    static const auto builtin_real = builtin_systemverilog_scalar(
        "real",
        frontend::SystemVerilogScalarKind::Real,
        frontend::ValueDomain::Bit2,
        true);
    static const auto builtin_realtime = builtin_systemverilog_scalar(
        "realtime",
        frontend::SystemVerilogScalarKind::Realtime,
        frontend::ValueDomain::Bit2,
        true);
    static const auto builtin_time = builtin_systemverilog_scalar(
        "time",
        frontend::SystemVerilogScalarKind::Time,
        frontend::ValueDomain::Logic4,
        false);
    static const auto builtin_chandle = builtin_systemverilog_scalar(
        "chandle",
        frontend::SystemVerilogScalarKind::Chandle,
        frontend::ValueDomain::Bit2,
        false);
    if (unit.language == frontend::Language::Vhdl2008) {
        visible_type_marks.emplace("integer", &builtin_integer);
        visible_type_marks.emplace("natural", &builtin_natural);
        visible_type_marks.emplace("positive", &builtin_positive);
        visible_type_marks.emplace("boolean", &builtin_boolean);
        visible_type_marks.emplace("bit", &builtin_bit);
    } else if (
        unit.language
        == frontend::Language::SystemVerilog2017) {
        visible_type_marks.emplace("shortreal", &builtin_shortreal);
        visible_type_marks.emplace("real", &builtin_real);
        visible_type_marks.emplace("realtime", &builtin_realtime);
        visible_type_marks.emplace("time", &builtin_time);
        visible_type_marks.emplace("chandle", &builtin_chandle);
    }
    const auto expose_type_mark =
        [&](const std::string_view name,
            const frontend::Type& type) {
            if (unit.language
                    == frontend::Language::SystemVerilog2017
                || unit.language
                    == frontend::Language::Vhdl2008) {
                visible_type_marks.try_emplace(
                    std::string { name }, &type);
            }
        };
    for (const auto& alias : unit.type_aliases) {
        expose_type_mark(alias.name, alias.type);
    }
    for (const auto& parameter : unit.parameters) {
        if (unit.language
                == frontend::Language::SystemVerilog2017
            && parameter.kind
                == frontend::ParameterKind::Type) {
            expose_type_mark(
                parameter.name, parameter.type);
        } else if (unit.language
            != frontend::Language::SystemVerilog2017) {
            expose_type_mark(
                parameter.type.spelling, parameter.type);
        }
    }
    const auto* ports = unit_ports(parsed_, unit);
    if (ports == nullptr) {
        report(
            "FSIM-ELAB-002",
            "architecture '" + unit.name + "' has no matching entity",
            unit.span);
        stack_.pop_back();
        return;
    }
    SystemVerilogAliasPlan systemverilog_alias_plan;
    if (unit.language
        == frontend::Language::SystemVerilog2017) {
        systemverilog_alias_plan = apply_systemverilog_aliases(
            unit,
            *ports,
            parameter_integral_environment,
            parameter_environment,
            true,
            true);
    }
    const auto& systemverilog_alias_groups = systemverilog_alias_plan.whole_groups;
    std::unordered_map<std::string, std::size_t>
        systemverilog_alias_group_by_name;
    for (std::size_t group = 0;
        group < systemverilog_alias_groups.size(); ++group) {
        for (const auto& name :
            systemverilog_alias_groups[group]) {
            systemverilog_alias_group_by_name.emplace(
                name, group);
        }
    }
    std::vector<std::optional<SignalId>>
        systemverilog_alias_signals(
            systemverilog_alias_groups.size());
    const auto add_or_bind_signal =
        [&](const frontend::SignalDeclaration& declaration) {
            const auto group = systemverilog_alias_group_by_name.find(
                declaration.name);
            const auto existing = local.find(declaration.name);
            if (group == systemverilog_alias_group_by_name.end()) {
                return existing == local.end()
                    ? add_owned_signal(declaration, path, local)
                    : std::optional<SignalId> { existing->second };
            }
            auto& alias_signal = systemverilog_alias_signals[group->second];
            if (!alias_signal && existing != local.end()) {
                alias_signal = existing->second;
            }
            if (!alias_signal) {
                alias_signal = add_owned_signal(
                    declaration, path, local);
            } else {
                const auto full_name = path + "." + declaration.name;
                local.insert_or_assign(
                    declaration.name, *alias_signal);
                local.insert_or_assign(full_name, *alias_signal);
                design_.signal_by_name_.insert_or_assign(
                    full_name, *alias_signal);
                if (design_.roots_.size() == 1
                    && path == active_root_) {
                    design_.signal_by_name_.insert_or_assign(
                        declaration.name, *alias_signal);
                }
            }
            return alias_signal;
        };
    for (const auto& port : *ports) {
        expose_type_mark(port.type.spelling, port.type);
        visible_types.emplace(port.name, &port.type);
        visible_types.emplace(
            path + "." + port.name, &port.type);
        if (!port.interface_type.empty()
            || port.type.spelling == "interface") {
            continue;
        }
        if (port.type.domain
            == frontend::ValueDomain::String) {
            const auto object = add_owned_string_port(
                port, path, local_string_objects);
            if (object
                && port.direction
                    == frontend::PortDirection::Input) {
                read_only_strings.insert(*object);
            }
            continue;
        }
        if (port.type.systemverilog_container) {
            if (!local_container_objects.contains(
                    port.name)) {
                (void)add_owned_container_port(
                    port,
                    path,
                    local_container_objects,
                    parameter_environment);
            }
            if (port.direction
                == frontend::PortDirection::Input) {
                read_only_container_objects.insert(
                    port.name);
                read_only_container_objects.insert(
                    path + "." + port.name);
            }
            continue;
        }
        (void)add_or_bind_signal(port);
    }
    for (const auto& signal : unit.signals) {
        expose_type_mark(signal.type.spelling, signal.type);
        visible_types.emplace(signal.name, &signal.type);
        visible_types.emplace(
            path + "." + signal.name, &signal.type);
        auto initialized_signal = signal;
        if (unit.language == frontend::Language::Vhdl2008
            && initialized_signal.default_value) {
            std::string initializer_error;
            if (const auto initialized =
                    evaluate_systemverilog_constant_function_expression(
                        *initialized_signal.default_value,
                        parameter_integral_environment,
                        parameter_environment,
                        unit.functions,
                        initializer_error)) {
                auto expression = initialized->expression(
                    initialized_signal.default_value->span);
                if (!initialized_signal.type.nominal_type.empty()) {
                    expression.nominal_type =
                        initialized_signal.type.nominal_type;
                }
                initialized_signal.default_value = std::move(expression);
            }
        }
        (void)add_or_bind_signal(initialized_signal);
    }
    for (const auto& alias : unit.signal_aliases) {
        expose_type_mark(alias.type.spelling, alias.type);
        visible_types.emplace(alias.name, &alias.type);
        visible_types.emplace(path + "." + alias.name, &alias.type);
        const auto actual = local.find(alias.actual);
        if (actual == local.end()) {
            report(
                "FSIM-ELAB-VHBLOCK-003",
                "unknown signal target '" + alias.actual
                    + "' for object alias '" + alias.name + "'",
                alias.span);
            continue;
        }
        const auto& info = design_.signal_info_.at(actual->second);
        const auto width = alias.type.width();
        const auto same_packed_range = [&]() {
            if (alias.type.packed_range.has_value()
                != info.packed_range.has_value()) {
                return false;
            }
            if (!alias.type.packed_range) {
                return true;
            }
            return alias.type.packed_range->left
                == info.packed_range->left
                && alias.type.packed_range->right
                == info.packed_range->right
                && alias.type.packed_range->descending
                == info.packed_range->descending;
        };
        if (!width || *width != info.width
            || alias.type.domain != info.source_domain
            || alias.type.is_signed != info.is_signed
            || !same_packed_range()
            || (!alias.type.nominal_type.empty()
                && alias.type.nominal_type
                    != info.nominal_type)) {
            report(
                "FSIM-ELAB-VHBLOCK-003",
                "object alias '" + alias.name
                    + "' does not match signal target '"
                    + alias.actual + "' (alias width="
                    + (width ? std::to_string(*width) : "unknown")
                    + ", signal width="
                    + std::to_string(info.width)
                    + ", alias nominal='" + alias.type.nominal_type
                    + "', signal nominal='" + info.nominal_type
                    + "')",
                alias.span);
            continue;
        }
        const frontend::SignalDeclaration formal {
            alias.name,
            alias.type,
            alias.direction,
            true,
            alias.span
        };
        const auto diagnostics_before = diagnostics_.size();
        validate_boundary_type(
            formal, info, path, alias.span, false);
        if (diagnostics_.size() != diagnostics_before) {
            continue;
        }
        local.emplace(alias.name, actual->second);
        if (!path.empty()) {
            local.emplace(
                path + "." + alias.name, actual->second);
        }
        if (design_.roots_.size() == 1 && path == active_root_) {
            design_.signal_by_name_.emplace(
                alias.name, actual->second);
        }
        if (!path.empty()) {
            design_.signal_by_name_.emplace(
                path + "." + alias.name,
                actual->second);
        }
    }
    SystemVerilogStringEnvironment string_values;
    for (const auto& variable : unit.variables) {
        visible_types.emplace(
            variable.name, &variable.type);
        visible_types.emplace(
            path + "." + variable.name, &variable.type);
        if (variable.vhdl_shared
            && !variable.type.vhdl_protected) {
            const bool bounded_memory_extension =
                variable.type.vhdl_array
                && variable.type.vhdl_array->dimensions.size() == 1U
                && variable.type.vhdl_array->dimensions.front().range
                && !variable.type.vhdl_array->dimensions.front().null
                && variable.type.width().value_or(0U) != 0U;
            if (unit.language == frontend::Language::Vhdl2008
                && (unit.vhdl_standard
                        == frontend::VhdlStandard::Vhdl1993
                    || bounded_memory_extension)) {
                materialize_vhdl_shared_variable(
                    variable, path, local);
            } else {
                report(
                    "FSIM-ELAB-VHPROTECTED-008",
                    "shared variable '" + path + "." + variable.name
                        + "' must have a protected type in VHDL-2000 and "
                          "later; only VHDL-1993 permits the legacy "
                          "unprotected form",
                    variable.span);
            }
            continue;
        }
        if (variable.vhdl_shared) {
            const auto& protected_info = *variable.type.vhdl_protected;
            if (!protected_info.has_body
                || !protected_info.body_conformant) {
                report(
                    "FSIM-ELAB-VHPROTECTED-009",
                    "shared protected object '" + path + "."
                        + variable.name
                        + "' requires one conforming protected body",
                    variable.span);
                continue;
            }
            if (variable.initializer) {
                report(
                    "FSIM-ELAB-VHPROTECTED-010",
                    "a protected shared variable is constructed from "
                    "its private member defaults and cannot have an "
                    "object initializer",
                    variable.initializer->span);
                continue;
            }
            const auto object_index = design_.vhdl_protected_object_info_.size();
            const auto object_id = static_cast<
                VhdlProtectedObjectId>(object_index);
            if (static_cast<std::size_t>(object_id)
                != object_index) {
                throw std::length_error {
                    "too many elaborated VHDL protected objects"
                };
            }
            VhdlProtectedObjectInfo object;
            object.id = object_id;
            object.name = path + "." + variable.name;
            object.type_name = variable.type.spelling;
            object.nominal_type = variable.type.nominal_type;
            object.declaration_span = variable.span;
            for (std::size_t member_index = 0;
                member_index < protected_info.variables.size();
                ++member_index) {
                const auto& member = protected_info.variables[member_index];
                const auto width = member.type.width();
                if (!width || *width == 0
                    || *width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-VHPROTECTED-017",
                        "protected private storage for '"
                            + object.name + "." + member.name
                            + "' exceeds the executable container "
                              "representation",
                        member.span);
                    continue;
                }
                ContainerType storage_type;
                storage_type.element_width = static_cast<std::uint32_t>(*width);
                storage_type.element_nominal_type = member.type.nominal_type;
                storage_type.two_state = is_two_state_domain(member.type.domain);
                storage_type.signed_elements = member.type.is_signed;
                storage_type.fixed = true;
                storage_type.index_left = 0;
                storage_type.index_right = 0;
                storage_type.dimensions.push_back({ 0, 0 });
                auto initial = default_container_value(storage_type);
                initial.elements[0] = default_packed_value(
                    member.type,
                    static_cast<std::size_t>(*width));
                if (member.initializer) {
                    std::string error;
                    const auto value = static_vhdl_value(
                        *member.initializer,
                        member.type,
                        error);
                    if (!value || value->width() != *width) {
                        report(
                            "FSIM-ELAB-VHPROTECTED-011",
                            "protected private initializer for '"
                                + object.name + "." + member.name
                                + "' is not a static value compatible "
                                  "with its declared subtype: "
                                + error,
                            member.initializer->span);
                        continue;
                    }
                    initial.elements[0] = *value;
                }
                const auto storage_index = design_.container_objects_.size();
                const auto storage_id = static_cast<
                    ContainerObjectId>(storage_index);
                if (static_cast<std::size_t>(storage_id)
                    != storage_index) {
                    throw std::length_error {
                        "too many elaborated container objects"
                    };
                }
                const auto member_name = object.name + "." + member.name;
                design_.container_object_info_.push_back(
                    ContainerObjectInfo {
                        storage_id,
                        member_name,
                        storage_type,
                        member.span,
                        false,
                        frontend::PortDirection::Unknown,
                        std::nullopt });
                design_.container_objects_.push_back(
                    ContainerObject {
                        member_name,
                        std::move(initial),
                        std::nullopt });
                local_container_objects.emplace(
                    variable.name + "." + member.name,
                    storage_id);
                local_container_objects.emplace(
                    member_name, storage_id);
                design_.container_by_name_.emplace(
                    member_name, storage_id);
                if (design_.roots_.size() == 1 && path == active_root_) {
                    design_.container_by_name_.emplace(
                        variable.name + "." + member.name,
                        storage_id);
                }
                object.members.push_back(
                    VhdlProtectedMemberInfo {
                        member.name,
                        member.type,
                        protected_info.variable_offsets[member_index],
                        static_cast<std::size_t>(*width),
                        storage_id,
                        member.span });
            }
            if (object.members.size()
                == protected_info.variables.size()) {
                design_.vhdl_protected_object_info_.push_back(
                    std::move(object));
            }
            continue;
        }
        if (variable.type.systemverilog_container) {
            const auto evaluate_container_constant =
                [&](const frontend::Expression& expression) {
                    std::string error;
                    const auto value = evaluate_systemverilog_constant_expression(
                        expression,
                        parameter_integral_environment,
                        parameter_environment,
                        error);
                    return value ? value->integer_value()
                                 : std::optional<std::int64_t> { };
                };
            auto materialized = materialize_systemverilog_container_type(
                variable.type,
                variable.span,
                evaluate_container_constant,
                [&](std::string code,
                    std::string message,
                    frontend::SourceSpan source) {
                    report(
                        std::move(code),
                        std::move(message),
                        std::move(source));
                });
            if (!materialized)
                continue;
            auto type = std::move(*materialized);
            if (variable.initializer) {
                report(
                    "FSIM-ELAB-SVCONTAINER-012",
                    "module container declaration initializers are not "
                    "executable; use an initial block",
                    variable.initializer->span);
                continue;
            }
            const auto index = design_.container_objects_.size();
            const auto id = static_cast<ContainerObjectId>(index);
            if (static_cast<std::size_t>(id) != index) {
                throw std::length_error {
                    "too many elaborated container objects"
                };
            }
            const auto full_name = path + "." + variable.name;
            design_.container_object_info_.push_back(
                ContainerObjectInfo {
                    id,
                    full_name,
                    type,
                    variable.span,
                    false,
                    frontend::PortDirection::Unknown,
                    std::nullopt });
            design_.container_objects_.push_back(
                ContainerObject {
                    full_name,
                    default_container_value(type),
                    std::nullopt });
            if (type.fixed && type.dimensions.size() == 1U
                && (type.element_kind == ContainerElementKind::Packed
                    || type.element_kind
                        == ContainerElementKind::Scalar)) {
                const auto packed_width = container_signal_bridge_width(type);
                if (packed_width && *packed_width != 0U) {
                    const auto signal_index = design_.signals_.size();
                    const auto signal_id = static_cast<SignalId>(signal_index);
                    if (static_cast<std::size_t>(signal_id) != signal_index) {
                        throw std::length_error {
                            "too many elaborated signals"
                        };
                    }
                    const auto signal_name =
                        full_name + ".$container_storage";
                    SignalInfo info;
                    info.id = signal_id;
                    info.name = signal_name;
                    info.width = *packed_width;
                    info.type_name = variable.type.spelling;
                    info.source_domain = type.two_state
                        ? frontend::ValueDomain::Bit2
                        : frontend::ValueDomain::Logic4;
                    info.systemverilog_net_type =
                        variable.type.systemverilog_net_type;
                    info.declaration_span = variable.span;
                    design_.signal_info_.push_back(std::move(info));
                    design_.signals_.push_back(Signal {
                        signal_name,
                        pack_container_signal_value(
                            design_.container_objects_.back().initial_value,
                            false),
                        ResolutionKind::none,
                        ValueKind::logic4 });
                    design_.signal_by_name_.emplace(
                        signal_name, signal_id);
                    design_.container_signal_aliases_.push_back(
                        ContainerSignalAlias {
                            id, signal_id, true, true });
                }
            }
            local_container_objects.emplace(
                variable.name, id);
            local_container_objects.emplace(
                full_name, id);
            design_.container_by_name_.emplace(
                full_name, id);
            if (design_.roots_.size() == 1 && path == active_root_) {
                design_.container_by_name_.emplace(
                    variable.name, id);
            }
            continue;
        }
        if (variable.type.systemverilog_virtual_interface) {
            const auto interface = std::ranges::find_if(
                parsed_.units,
                [&](const frontend::DesignUnit& candidate) {
                    return candidate.kind
                        == frontend::UnitKind::SystemVerilogInterface
                        && candidate.name
                        == variable.type
                               .systemverilog_interface_type;
                });
            if (interface == parsed_.units.end()) {
                report(
                    "FSIM-ELAB-SVIFACE-003",
                    "virtual interface '" + path + "."
                        + variable.name + "' requires unknown type '"
                        + variable.type.systemverilog_interface_type
                        + "'",
                    variable.span);
                continue;
            }
            if (!variable.type.systemverilog_interface_modport.empty()
                && std::ranges::none_of(
                    interface->systemverilog_modports,
                    [&](const frontend::SystemVerilogModport& modport) {
                        return modport.name
                            == variable.type
                                   .systemverilog_interface_modport;
                    })) {
                report(
                    "FSIM-ELAB-SVIFACE-004",
                    "virtual interface '" + path + "."
                        + variable.name + "' selects unknown modport '"
                        + variable.type.systemverilog_interface_modport
                        + "' on interface type '"
                        + variable.type.systemverilog_interface_type
                        + "'",
                    variable.span);
                continue;
            }
            const frontend::SignalDeclaration declaration {
                variable.name,
                variable.type,
                frontend::PortDirection::Unknown,
                false,
                variable.span
            };
            const auto signal = add_owned_signal(declaration, path, local);
            if (signal) {
                design_.signals_[*signal].initial_value = PackedLogic4::from_aval_bval(64, 0, 0);
            }
            continue;
        }
        if (std::ranges::any_of(
                unit.systemverilog_covergroups,
                [&](const frontend::SystemVerilogCovergroupDeclaration&
                        covergroup) {
                    return covergroup.name == variable.type.named_type;
                })) {
            // The coverage service owns this object's state and stable
            // identity; no scheduler signal/string allocation is needed.
            continue;
        }
        if (!variable.type.systemverilog_class_declaration.empty()) {
            const frontend::SignalDeclaration declaration {
                variable.name,
                variable.type,
                frontend::PortDirection::Unknown,
                false,
                variable.span
            };
            const auto signal = add_owned_signal(declaration, path, local);
            if (signal) {
                design_.signals_[*signal].initial_value = PackedLogic4::from_aval_bval(64, 0, 0);
            }
            continue;
        }
        if (variable.type.systemverilog_scalar
            != frontend::SystemVerilogScalarKind::None) {
            const frontend::SignalDeclaration declaration {
                variable.name,
                variable.type,
                frontend::PortDirection::Unknown,
                false,
                variable.span
            };
            (void)add_owned_signal(declaration, path, local);
            continue;
        }
        if (variable.type.domain
            == frontend::ValueDomain::Integer) {
            const frontend::SignalDeclaration declaration {
                variable.name,
                variable.type,
                frontend::PortDirection::Unknown,
                false,
                variable.span
            };
            const auto signal = add_owned_signal(declaration, path, local);
            if (signal && variable.initializer) {
                std::string error;
                const auto value = evaluate_systemverilog_constant_expression(
                    *variable.initializer,
                    parameter_integral_environment,
                    parameter_environment,
                    error);
                const auto converted = value
                    ? convert_systemverilog_parameter_value(
                          *value, variable.type, error)
                    : std::nullopt;
                if (!converted || !converted->known()) {
                    report(
                        "FSIM-ELAB-SVFILE-008",
                        "module integer initializer for '"
                            + path + "." + variable.name
                            + "' is not a known 32-bit constant: "
                            + error,
                        variable.span);
                } else {
                    design_.signals_[*signal].initial_value = PackedLogic4::from_aval_bval(
                        converted->width,
                        converted->bits,
                        0);
                }
            }
            continue;
        }
        if (variable.type.domain
            != frontend::ValueDomain::String) {
            report(
                "FSIM-ELAB-SVSTRING-016",
                "module variable '" + path + "."
                    + variable.name
                    + "' is not a supported string object",
                variable.span);
            continue;
        }
        SystemVerilogStringValue initial;
        initial.source = variable.span;
        if (variable.initializer) {
            std::string error;
            const auto value = evaluate_systemverilog_string_expression(
                *variable.initializer,
                string_values,
                { },
                error);
            if (!value) {
                report(
                    "FSIM-ELAB-SVSTRING-017",
                    "cannot evaluate module string initializer for '"
                        + path + "." + variable.name
                        + "': " + error,
                    variable.span);
                continue;
            }
            initial = *value;
        }
        if (initial.bytes.size()
            > maximum_string_bytes) {
            report(
                "FSIM-ELAB-SVSTRING-007",
                "module string initializer exceeds the 4096-byte "
                "limit",
                variable.span);
            continue;
        }
        const auto index = design_.string_objects_.size();
        const auto id = static_cast<StringObjectId>(index);
        if (static_cast<std::size_t>(id) != index) {
            throw std::length_error {
                "too many elaborated string objects"
            };
        }
        const auto full_name = path + "." + variable.name;
        design_.string_object_info_.push_back(
            StringObjectInfo {
                id,
                full_name,
                variable.span,
                false,
                frontend::PortDirection::Unknown });
        design_.string_objects_.push_back(
            StringObject { full_name, initial.bytes });
        local_string_objects.emplace(
            variable.name, id);
        local_string_objects.emplace(
            full_name, id);
        design_.string_by_name_.emplace(full_name, id);
        if (design_.roots_.size() == 1 && path == active_root_) {
            design_.string_by_name_.emplace(
                variable.name, id);
        }
        string_values.emplace(
            variable.name, std::move(initial));
    }

#include "hierarchy_instantiate_processes_and_children.tpp"
    for (const auto& declaration : resolved_defparams) {
        if (!declaration.matched) {
            report(
                "FSIM-ELAB-DEFPARAM-002",
                "unknown defparam hierarchical target '"
                    + defparam_path_text(declaration.segments) + "'",
                declaration.declaration->span);
        }
    }
    for (const auto& variable : unit.variables) {
        if (!variable.type.systemverilog_virtual_interface
            || !variable.initializer
            || (variable.initializer->kind
                    == frontend::ExpressionKind::Call
                && variable.initializer->text == "@sv-null")) {
            continue;
        }
        const auto signal = local.find(variable.name);
        if (signal == local.end())
            continue;
        std::string actual_name;
        if (variable.initializer->kind
            == frontend::ExpressionKind::Identifier) {
            actual_name = variable.initializer->text;
        } else if (variable.initializer->kind
                == frontend::ExpressionKind::Index
            && variable.initializer->operands.size() == 2
            && variable.initializer->operands[0].kind
                == frontend::ExpressionKind::Identifier
            && variable.initializer->operands[1].kind
                == frontend::ExpressionKind::IntegerLiteral) {
            actual_name = variable.initializer->operands[0].text
                + "[" + variable.initializer->operands[1].text + "]";
        }
        if (actual_name.empty()) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "virtual-interface initializer for '" + path + "."
                    + variable.name
                    + "' must name a scalar or statically selected "
                      "interface instance, or null",
                variable.initializer->span);
            continue;
        }
        auto actual_path = path.empty()
            ? actual_name
            : path + "." + actual_name;
        auto actual = systemverilog_interface_instances_.find(actual_path);
        auto lexical_path = path;
        while (actual == systemverilog_interface_instances_.end()
            && lexical_path.find('.') != std::string::npos) {
            lexical_path.resize(lexical_path.rfind('.'));
            actual_path = lexical_path + "." + actual_name;
            actual = systemverilog_interface_instances_.find(actual_path);
        }
        if (actual == systemverilog_interface_instances_.end()) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "virtual-interface initializer '" + actual_path
                    + "' must name an elaborated interface instance",
                variable.initializer->span);
            continue;
        }
        if (actual->second.name
            != variable.type.systemverilog_interface_type) {
            report(
                "FSIM-ELAB-SVIFACE-003",
                "virtual interface '" + path + "." + variable.name
                    + "' requires type '"
                    + variable.type.systemverilog_interface_type
                    + "' but initializer '" + actual_path
                    + "' has type '" + actual->second.name + "'",
                variable.initializer->span);
            continue;
        }
        if (!variable.type.systemverilog_class_parameter_actuals.empty()) {
            const auto declaration = std::ranges::find_if(
                parsed_.units,
                [&](const DesignUnit& candidate) {
                    return candidate.kind
                        == frontend::UnitKind::SystemVerilogInterface
                        && candidate.name
                        == variable.type.systemverilog_interface_type
                        && candidate.library == actual->second.library;
                });
            const auto actual_identity = systemverilog_interface_parameter_identities_.find(
                actual_path);
            if (declaration == parsed_.units.end()
                || actual_identity
                    == systemverilog_interface_parameter_identities_.end()) {
                report(
                    "FSIM-ELAB-SVIFACE-010",
                    "virtual interface '" + path + "." + variable.name
                        + "' cannot resolve the specialization identity of '"
                        + actual_path + "'",
                    variable.initializer->span);
                continue;
            }
            std::vector<frontend::ParameterOverride> overrides;
            overrides.reserve(
                variable.type.systemverilog_class_parameter_actuals.size());
            for (const auto& retained :
                variable.type.systemverilog_class_parameter_actuals) {
                frontend::ParameterOverride override;
                override.name = retained.name;
                override.value = retained.value;
                if (retained.type_actual) {
                    override.type_value = *retained.type_actual;
                }
                override.span = retained.span;
                overrides.push_back(std::move(override));
            }
            auto required = specialize_selected_unit(
                *declaration,
                overrides,
                parameter_environment,
                parameter_integral_environment,
                parent_domains,
                parent_types,
                unit.functions,
                unit.procedures,
                package_environment,
                unit.language);
            if (required.identity_values
                != actual_identity->second) {
                report(
                    "FSIM-ELAB-SVIFACE-010",
                    "virtual interface '" + path + "." + variable.name
                        + "' requires a different specialization of interface '"
                        + variable.type.systemverilog_interface_type
                        + "' than initializer '" + actual_path + "'",
                    variable.initializer->span);
                continue;
            }
        }
        const auto handle = systemverilog_interface_handles_.find(actual_path);
        if (handle == systemverilog_interface_handles_.end()) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "virtual-interface initializer '" + actual_path
                    + "' has no owned interface identity",
                variable.initializer->span);
            continue;
        }
        design_.signals_[signal->second].initial_value = PackedLogic4::from_aval_bval(64, handle->second, 0);
    }
    stack_.pop_back();
}

} // namespace fsim::elaboration
