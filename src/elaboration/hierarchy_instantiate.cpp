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

    const auto clocking_one_step_delay =
        [&](const frontend::SourceSpan& span)
        -> std::optional<frontend::Delay> {
        std::uint64_t magnitude = 0;
        std::size_t split = 0;
        while (split < unit.time_precision.size()
            && unit.time_precision[split] >= '0'
            && unit.time_precision[split] <= '9') {
            magnitude = magnitude * 10
                + static_cast<std::uint64_t>(
                    unit.time_precision[split] - '0');
            ++split;
        }
        if (magnitude == 0
            || split == unit.time_precision.size()) {
            report(
                "FSIM-ELAB-CLOCK-006",
                "clocking #1step requires a concrete design-unit "
                "time precision",
                span);
            return std::nullopt;
        }
        frontend::Delay delay;
        delay.magnitude = magnitude;
        delay.unit = unit.time_precision.substr(split);
        delay.span = span;
        return delay;
    };
    std::vector<frontend::Statement> clocking_skew_statements;
    struct ClockingEventProcess {
        frontend::Process process;
        bool observed { };
    };
    std::vector<ClockingEventProcess> clocking_event_processes;
    for (const auto& block : unit.systemverilog_clocking_blocks) {
        if (block.event.size() != 1
            || block.event.front().signal.empty()) {
            report(
                "FSIM-ELAB-CLOCK-001",
                "clocking block '" + path + "." + block.name
                    + "' requires one signal event",
                block.span);
            continue;
        }
        const auto event = local.find(block.event.front().signal);
        if (event == local.end()) {
            report(
                "FSIM-ELAB-CLOCK-002",
                "unknown event signal '"
                    + block.event.front().signal
                    + "' for clocking block '" + path + "."
                    + block.name + "'",
                block.event.front().span);
            continue;
        }
        local.insert_or_assign(block.name, event->second);
        local.insert_or_assign(
            path + "." + block.name, event->second);
        design_.signal_by_name_.emplace(
            path + "." + block.name, event->second);

        for (const auto& member : block.signals) {
            const auto actual_name = member.expression
                ? member.expression->text
                : member.name;
            if (member.expression
                && member.expression->kind
                    != frontend::ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-CLOCK-003",
                    "clocking member '" + block.name + "."
                        + member.name
                        + "' requires a signal identifier expression",
                    member.expression->span);
                continue;
            }
            const auto actual = local.find(actual_name);
            const auto type = visible_types.find(actual_name);
            if (actual == local.end()
                || type == visible_types.end()) {
                report(
                    "FSIM-ELAB-CLOCK-004",
                    "unknown signal '" + actual_name
                        + "' for clocking member '" + block.name + "."
                        + member.name + "'",
                    member.span);
                continue;
            }
            const auto member_name = block.name + "." + member.name;
            const auto* selected_skew = member.skew
                ? &*member.skew
                : member.direction
                    == frontend::PortDirection::Input
                ? block.default_input_skew
                    ? &*block.default_input_skew
                    : nullptr
                : member.direction
                        == frontend::PortDirection::Output
                    && block.default_output_skew
                ? &*block.default_output_skew
                : nullptr;
            std::optional<frontend::Delay> skew_delay;
            if (selected_skew && selected_skew->delay) {
                skew_delay = selected_skew->delay;
            }
            if (member.direction
                    == frontend::PortDirection::Input
                && ((!selected_skew)
                    || selected_skew->one_step
                    || !selected_skew->delay)) {
                skew_delay = clocking_one_step_delay(member.span);
            }
            if (member.direction
                    == frontend::PortDirection::Output
                && skew_delay && skew_delay->magnitude != 0) {
                const frontend::SignalDeclaration request {
                    member_name,
                    *type->second,
                    frontend::PortDirection::Unknown,
                    false,
                    member.span
                };
                const auto request_signal = add_owned_signal(request, path, local);
                if (!request_signal)
                    continue;
                visible_types.insert_or_assign(
                    member_name, type->second);
                visible_types.insert_or_assign(
                    path + "." + member_name, type->second);

                frontend::Statement drive;
                drive.kind = frontend::StatementKind::Assignment;
                drive.assignment_kind = frontend::AssignmentKind::Blocking;
                drive.label = "$clocking$" + block.name + "$"
                    + member.name + "$drive";
                drive.target = frontend::Expression {
                    frontend::ExpressionKind::Identifier,
                    actual_name,
                    { },
                    member.span
                };
                drive.value = frontend::Expression {
                    frontend::ExpressionKind::Identifier,
                    member_name,
                    { },
                    member.span
                };
                drive.delay = std::move(skew_delay);
                drive.span = member.span;
                if (selected_skew->edge
                    != frontend::EdgeKind::Any) {
                    frontend::Process driver;
                    driver.kind = frontend::ProcessKind::VerilogAlways;
                    driver.name = "$clocking$" + block.name
                        + "$" + member.name + "$drive";
                    driver.sensitivities = block.event;
                    driver.sensitivities.front().edge = selected_skew->edge;
                    driver.statements.push_back(std::move(drive));
                    driver.span = member.span;
                    clocking_event_processes.push_back(
                        { std::move(driver), false });
                } else {
                    clocking_skew_statements.push_back(
                        std::move(drive));
                }
                continue;
            }
            if (member.direction
                != frontend::PortDirection::Input) {
                local.insert_or_assign(
                    member_name, actual->second);
                local.insert_or_assign(
                    path + "." + member_name, actual->second);
                visible_types.insert_or_assign(
                    member_name, type->second);
                visible_types.insert_or_assign(
                    path + "." + member_name, type->second);
                design_.signal_by_name_.emplace(
                    path + "." + member_name, actual->second);
            }
            if (member.direction
                == frontend::PortDirection::Output) {
                continue;
            }

            const frontend::SignalDeclaration sampled {
                member_name,
                *type->second,
                frontend::PortDirection::Unknown,
                false,
                member.span
            };
            const auto sample = add_owned_signal(sampled, path, local);
            if (!sample)
                continue;
            visible_types.insert_or_assign(
                member_name, type->second);
            visible_types.insert_or_assign(
                path + "." + member_name, type->second);

            std::string sample_source = actual_name;
            if (skew_delay && skew_delay->magnitude != 0) {
                const auto skew_name = "$clocking$"
                    + block.name + "$" + member.name + "$skew";
                const frontend::SignalDeclaration delayed {
                    skew_name,
                    *type->second,
                    frontend::PortDirection::Unknown,
                    false,
                    member.span
                };
                const auto delayed_signal = add_owned_signal(delayed, path, local);
                if (!delayed_signal)
                    continue;
                visible_types.insert_or_assign(
                    skew_name, type->second);
                visible_types.insert_or_assign(
                    path + "." + skew_name, type->second);
                frontend::Statement history;
                history.kind = frontend::StatementKind::Assignment;
                history.assignment_kind = frontend::AssignmentKind::Blocking;
                history.label = "$clocking$" + block.name + "$"
                    + member.name + "$history";
                history.target = frontend::Expression {
                    frontend::ExpressionKind::Identifier,
                    skew_name,
                    { },
                    member.span
                };
                history.value = member.expression.value_or(
                    frontend::Expression {
                        frontend::ExpressionKind::Identifier,
                        member.name,
                        { },
                        member.span });
                history.delay = std::move(skew_delay);
                history.span = member.span;
                clocking_skew_statements.push_back(
                    std::move(history));
                sample_source = skew_name;
            }
            frontend::Statement assignment;
            assignment.kind = frontend::StatementKind::Assignment;
            assignment.assignment_kind = frontend::AssignmentKind::Blocking;
            assignment.target = frontend::Expression {
                frontend::ExpressionKind::Identifier,
                member_name,
                { },
                member.span
            };
            assignment.value = frontend::Expression {
                frontend::ExpressionKind::Identifier,
                std::move(sample_source),
                { },
                member.span
            };
            assignment.span = member.span;
            frontend::Process sampler;
            sampler.kind = frontend::ProcessKind::VerilogAlways;
            sampler.name = "$clocking$" + block.name + "$"
                + member.name + "$sample";
            sampler.sensitivities = block.event;
            if (selected_skew
                && selected_skew->edge
                    != frontend::EdgeKind::Any) {
                sampler.sensitivities.front().edge = selected_skew->edge;
            }
            sampler.statements.push_back(std::move(assignment));
            sampler.span = member.span;
            clocking_event_processes.push_back(
                { std::move(sampler), true });
        }
    }

    const auto specialization_index = design_.specializations_.size();
    const auto specialization_id = static_cast<SpecializationId>(specialization_index);
    if (static_cast<std::size_t>(specialization_id)
        != specialization_index) {
        throw std::length_error(
            "too many elaborated design-unit specializations");
    }
    const auto program_owner
        = unit.kind == frontend::UnitKind::SystemVerilogProgram
        ? std::optional<std::uint32_t> { specialization_id }
        : std::nullopt;
    SpecializationInfo specialization;
    specialization.id = specialization_id;
    specialization.unit = identity;
    specialization.instance = path;
    specialization.source = std::string { frontend::physical_source(unit.span) };
    if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
        if (const auto* entity = find_vhdl_entity(parsed_, unit);
            entity != nullptr
            && frontend::physical_source(entity->span)
                != specialization.source) {
            specialization.source_dependencies.push_back(
                std::string {
                    frontend::physical_source(entity->span) });
        }
    }
    for (const auto& dependency : unit.source_dependencies) {
        if (dependency != specialization.source
            && std::find(
                   specialization.source_dependencies.begin(),
                   specialization.source_dependencies.end(),
                   dependency)
                == specialization.source_dependencies.end()) {
            specialization.source_dependencies.push_back(
                dependency);
        }
    }
    specialization.language = unit.language;
    specialization.library = unit.library.empty() ? "work" : unit.library;
    specialization.is_cell = unit.is_cell;
    specialization.parameter_values = std::move(parameter_values);
    specialization.parameter_identity_values = std::move(parameter_identity_values);
    add_systemverilog_alias_connections(
        systemverilog_alias_plan,
        path,
        local,
        specialization);

    const auto specify_path_begin = design_.verilog_specify_paths_.size();
    validate_verilog_specify(
        unit, path, local, parameter_environment);

    const frontend::SystemVerilogClockingBlock*
        default_clocking = nullptr;
    if (unit.systemverilog_default_clocking_block) {
        const auto selected = std::ranges::find(
            unit.systemverilog_clocking_blocks,
            *unit.systemverilog_default_clocking_block,
            &frontend::SystemVerilogClockingBlock::name);
        if (selected
            != unit.systemverilog_clocking_blocks.end()) {
            default_clocking = &*selected;
        }
    }
    const auto prepare_clocking_cycle_waits =
        [&](auto&& self,
            std::vector<frontend::Statement>& statements) -> void {
        for (auto& statement : statements) {
            if (statement.clocking_cycle_delay) {
                if (default_clocking == nullptr) {
                    report(
                        "FSIM-ELAB-CLOCK-005",
                        "a ## cycle delay requires a default "
                        "clocking block",
                        statement.span);
                } else {
                    statement.sensitivities = default_clocking->event;
                    statement.procedural_assignment_repeat = true;
                    statement.loop_limit = statement.clocking_cycle_count;
                }
            }
            self(self, statement.statements);
            self(self, statement.else_statements);
            for (auto& alternative :
                statement.case_alternatives) {
                self(self, alternative.statements);
            }
        }
    };

    Lowerer lowerer {
        design_,
        local,
        read_only_signals,
        local_string_objects,
        read_only_strings,
        local_container_objects,
        read_only_container_objects,
        visible_types,
        visible_type_marks,
        unit.functions,
        unit.tasks,
        unit.procedures,
        systemverilog_scalar_evaluation_context(unit),
        diagnostics_
    };
    lowerer.set_systemverilog_program_owner(program_owner);
    lowerer.set_vhdl_standard(unit.vhdl_standard);
    const auto uses_synopsys_package = [&](const std::string_view package) {
        const auto prefix = "ieee." + std::string { package };
        return std::ranges::any_of(
            unit.vhdl_context,
            [&](const frontend::VhdlContextItem& item) {
                return item.kind == frontend::VhdlContextItemKind::UseClause
                    && std::ranges::any_of(
                        item.selected_names,
                        [&](const std::string& selected) {
                            return selected == prefix
                                || selected.starts_with(prefix + ".");
                        });
            });
    };
    lowerer.set_vhdl_synopsys_numeric_context(
        uses_synopsys_package("std_logic_signed"),
        uses_synopsys_package("std_logic_unsigned"));
    const auto append_profiled_process =
        [&](Process process) {
            if (unit.language == frontend::Language::Vhdl2008) {
                process.language_standard = frontend::to_string(unit.vhdl_standard);
                process.compatibility_profile = unit.vhdl_compatibility_profile;
            }
            canonicalize_process_operations(process);
            specialization.processes.push_back(process.id);
            design_.processes_.push_back(std::move(process));
        };
    for (std::size_t index = 0;
        index < clocking_skew_statements.size(); ++index) {
        auto process = lowerer.lower_concurrent(
            clocking_skew_statements[index],
            unit.language,
            path,
            unit.concurrent_statements.size() + index);
        append_profiled_process(std::move(process));
        for (auto& generated :
            lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    }
    for (const auto& event_process : clocking_event_processes) {
        auto lowered = lowerer.lower_process(
            event_process.process, unit.language, path);
        lowered.observed = event_process.observed;
        append_profiled_process(std::move(lowered));
        for (auto& generated : lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    }
    const auto append_generated_processes = [&] {
        for (auto& generated : lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    };
    const auto fusion_eligible = [&](const frontend::Statement& statement) {
        return statement.kind == frontend::StatementKind::Assignment
            && statement.assignment_kind
                == frontend::AssignmentKind::Continuous
            && statement.label != "$port_input_driver"
            && !statement.delay
            && (unit.language != frontend::Language::Vhdl2008
                || (statement.vhdl_waveform.size() == 1U
                    && !statement.vhdl_waveform.front().disconnect
                    && !statement.vhdl_unaffected))
            && !statement.verilog_drive_strength
            && !statement.verilog_switch_driver
            && !statement.vhdl_guarded_assignment
            && !statement.vhdl_postponed;
    };
    const auto exact_conditional_fusion_eligible
        = [&](const auto& self, const frontend::Statement& statement)
        -> bool {
        if (fusion_eligible(statement)) {
            return true;
        }
        if (unit.language != frontend::Language::Vhdl2008
            || statement.kind != frontend::StatementKind::If
            || !statement.vhdl_conditional_assignment
            || statement.vhdl_guarded_assignment
            || statement.vhdl_postponed
            || statement.statements.empty()
            || statement.else_statements.empty()) {
            return false;
        }
        return std::ranges::all_of(
                   statement.statements,
                   [&](const frontend::Statement& nested) {
                       return self(self, nested);
                   })
            && std::ranges::all_of(
                statement.else_statements,
                [&](const frontend::Statement& nested) {
                    return self(self, nested);
                });
    };
    const auto fusion_assignment = [&](const auto& self,
                                       const frontend::Statement& statement)
        -> const frontend::Statement* {
        if (statement.kind == frontend::StatementKind::Assignment) {
            return &statement;
        }
        for (const auto& nested : statement.statements) {
            if (const auto* assignment = self(self, nested)) {
                return assignment;
            }
        }
        for (const auto& nested : statement.else_statements) {
            if (const auto* assignment = self(self, nested)) {
                return assignment;
            }
        }
        return nullptr;
    };
    const auto fusion_target = [&](const frontend::Statement& statement)
        -> std::optional<SignalId> {
        const auto* assignment = fusion_assignment(
            fusion_assignment, statement);
        if (assignment == nullptr) {
            return std::nullopt;
        }
        const auto* target = &assignment->target;
        while ((target->kind == frontend::ExpressionKind::Index
                   || target->kind == frontend::ExpressionKind::Slice)
            && !target->operands.empty()) {
            target = &target->operands.front();
        }
        if (target->kind != frontend::ExpressionKind::Identifier) {
            return std::nullopt;
        }
        const auto found = local.find(target->text);
        return found == local.end()
            ? std::nullopt
            : std::optional<SignalId> { found->second };
    };
    constexpr std::size_t minimum_fused_group_size = 8U;
    const std::span<const frontend::Statement> concurrent_statements {
        unit.concurrent_statements
    };

    struct VhdlFusionBank {
        std::vector<std::size_t> members;
        std::unordered_set<SignalId> targets;
        std::vector<SignalId> sensitivity;
        bool exact_sensitivity { };
    };
    std::vector<VhdlFusionBank> vhdl_fusion_banks;
    std::vector<std::optional<std::size_t>> vhdl_fusion_bank_by_statement(
        concurrent_statements.size());
    if (unit.language == frontend::Language::Vhdl2008
        && std::getenv("FSIM_DISABLE_CONTINUOUS_FUSION") == nullptr) {
        constexpr std::size_t maximum_bank_members = 64U;
        constexpr std::size_t maximum_bank_sensitivity = 63U;
        for (std::size_t statement = 0U;
             statement < concurrent_statements.size(); ++statement) {
            const auto& source = concurrent_statements[statement];
            const bool eligible = fusion_eligible(source);
            const bool exact_conditional
                = !eligible
                && exact_conditional_fusion_eligible(
                    exact_conditional_fusion_eligible, source);
            const bool trigger_safe = (eligible || exact_conditional)
                && lowerer.concurrent_trigger_fusion_safe(source);
            if ((!eligible && !exact_conditional) || !trigger_safe) {
                continue;
            }
            const auto target = fusion_target(source);
            const auto sensitivity = lowerer.concurrent_sensitivity(source);
            if (!target || sensitivity.empty()) {
                continue;
            }
            std::optional<std::size_t> selected_bank;
            std::vector<SignalId> selected_sensitivity;
            for (std::size_t bank = 0U;
                 bank < vhdl_fusion_banks.size(); ++bank) {
                const auto& candidate = vhdl_fusion_banks[bank];
                if (candidate.members.size() >= maximum_bank_members
                    || candidate.targets.contains(*target)
                    || candidate.exact_sensitivity
                        != exact_conditional) {
                    continue;
                }
                if (exact_conditional
                    && candidate.sensitivity != sensitivity) {
                    continue;
                }
                std::vector<SignalId> combined;
                if (exact_conditional) {
                    combined = sensitivity;
                } else {
                    std::ranges::set_union(
                        candidate.sensitivity, sensitivity,
                        std::back_inserter(combined));
                }
                if (combined.size() > maximum_bank_sensitivity) {
                    continue;
                }
                selected_bank = bank;
                selected_sensitivity = std::move(combined);
                break;
            }
            if (!selected_bank) {
                vhdl_fusion_banks.push_back(VhdlFusionBank {
                    { }, { }, sensitivity, exact_conditional
                });
                selected_bank = vhdl_fusion_banks.size() - 1U;
            } else {
                vhdl_fusion_banks[*selected_bank].sensitivity
                    = std::move(selected_sensitivity);
            }
            auto& bank = vhdl_fusion_banks[*selected_bank];
            bank.targets.insert(*target);
            bank.members.push_back(statement);
            vhdl_fusion_bank_by_statement[statement]
                = *selected_bank;
        }
        for (std::size_t bank = 0U; bank < vhdl_fusion_banks.size(); ++bank) {
            if (vhdl_fusion_banks[bank].members.size()
                >= minimum_fused_group_size) {
                continue;
            }
            for (const auto statement : vhdl_fusion_banks[bank].members) {
                vhdl_fusion_bank_by_statement[statement].reset();
            }
        }
    }

    if (unit.language == frontend::Language::Vhdl2008) {
        for (std::size_t index = 0U;
             index < concurrent_statements.size(); ++index) {
            const auto bank_id = vhdl_fusion_bank_by_statement[index];
            if (!bank_id) {
                append_profiled_process(lowerer.lower_concurrent(
                    concurrent_statements[index], unit.language, path, index));
                append_generated_processes();
                continue;
            }
            const auto& bank = vhdl_fusion_banks[*bank_id];
            if (bank.members.front() == index) {
                std::vector<frontend::Statement> statements;
                statements.reserve(bank.members.size());
                for (const auto member : bank.members) {
                    statements.push_back(concurrent_statements[member]);
                }
                auto lowered = lowerer.lower_concurrent_group(
                    statements, unit.language, path, index);
                if (bank.exact_sensitivity) {
                    lowered.static_trigger_regions.clear();
                }
                append_profiled_process(std::move(lowered));
                append_generated_processes();
                continue;
            }
            // Retain the vacated process identity so later process handles and
            // per-process random streams are invariant under banking.
            Process placeholder;
            placeholder.id = static_cast<ProcessId>(design_.processes_.size());
            placeholder.name = path + ".concurrent_fused_slot_"
                + std::to_string(index);
            placeholder.operations.emplace_back(Halt { });
            append_profiled_process(std::move(placeholder));
        }
    } else {

    for (std::size_t index = 0; index < concurrent_statements.size();) {
        std::size_t end = index;
        while (end < concurrent_statements.size()
            && fusion_eligible(concurrent_statements[end])) {
            ++end;
        }
        if (end - index >= minimum_fused_group_size
            && std::getenv("FSIM_DISABLE_CONTINUOUS_FUSION") == nullptr) {
            const auto run
                = concurrent_statements.subspan(index, end - index);
            append_profiled_process(lowerer.lower_concurrent_group(
                run, unit.language, path, index));
            append_generated_processes();
            index = end;
            continue;
        }
        const auto individual_end = end == index ? index + 1U : end;
        for (; index < individual_end; ++index) {
            auto process = lowerer.lower_concurrent(
                concurrent_statements[index],
                unit.language,
                path,
                index);
            append_profiled_process(std::move(process));
            append_generated_processes();
        }
    }
    }
    for (const auto& source_process : unit.processes) {
        auto process = source_process;
        const bool concurrent_assertion = process.statements.size() == 1U
            && process.statements.front().kind
                == frontend::StatementKind::Assert
            && process.statements.front().assertion_message.starts_with(
                "concurrent assertion '");
        prepare_clocking_cycle_waits(
            prepare_clocking_cycle_waits, process.statements);
        auto lowered = lowerer.lower_process(process, unit.language, path);
        lowered.observed = concurrent_assertion;
        lowered.reactive = !concurrent_assertion
            && unit.kind == frontend::UnitKind::SystemVerilogProgram;
        lowered.program_owner = program_owner;
        append_profiled_process(std::move(lowered));
        for (auto& generated : lowerer.take_generated_processes()) {
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            append_profiled_process(std::move(generated));
        }
    }
    // These frontend bodies have been fully lowered.  Child binding still
    // needs the unit's declarations, callables, configurations, and instance
    // inventory, but retaining consumed process syntax only overlaps it with
    // the growing runtime design.
    {
        decltype(unit.concurrent_statements) empty;
        unit.concurrent_statements.swap(empty);
    }
    {
        decltype(unit.processes) empty;
        unit.processes.swap(empty);
    }
    const auto regions_overlap = [](
                                     const Process::DriverRegion& region,
                                     const VerilogSpecifyTerminalInfo& terminal) {
        if (region.signal != terminal.signal)
            return false;
        if (region.whole)
            return true;
        const auto region_end = static_cast<std::uint64_t>(region.offset) + region.width;
        const auto terminal_end = static_cast<std::uint64_t>(terminal.offset)
            + terminal.width;
        return region.offset < terminal_end
            && terminal.offset < region_end;
    };
    for (auto path_index = specify_path_begin;
        path_index < design_.verilog_specify_paths_.size();
        ++path_index) {
        auto& specify_path = design_.verilog_specify_paths_[path_index];
        for (const auto process_id : specialization.processes) {
            const auto& process = design_.processes_.at(process_id);
            if (std::ranges::any_of(
                    process.driver_regions,
                    [&](const auto& region) {
                        return std::ranges::any_of(
                            specify_path.destinations,
                            [&](const auto& terminal) {
                                return regions_overlap(region, terminal);
                            });
                    })) {
                specify_path.drivers.push_back(process_id);
            }
        }
    }
    design_.specializations_.push_back(std::move(specialization));

    validate_vhdl_component_configurations(unit, path);
    auto effective_instances = unit.instances;
    auto bound_instances = systemverilog_bound_instances(
        unit, path, parameter_environment, parent_domains);
    for (auto& bound_instance : bound_instances) {
        if (std::ranges::any_of(
                effective_instances,
                [&](const frontend::Instance& existing) {
                    return existing.name == bound_instance.name;
                })) {
            report(
                "FSIM-ELAB-SVBIND-001",
                "bound instance name '" + bound_instance.name
                    + "' collides in scope '" + path + "'",
                bound_instance.span);
            continue;
        }
        effective_instances.push_back(std::move(bound_instance));
    }
    for (const auto& instance : effective_instances) {
        const auto child_path = path + "." + instance.name;
        std::vector<std::pair<ResolvedVerilogDefparam*, std::size_t>>
            child_defparams;
        for (auto& declaration : resolved_defparams) {
            const auto prefix = defparam_instance_prefix(
                declaration.segments, instance.name);
            if (!prefix) {
                continue;
            }
            declaration.matched = true;
            child_defparams.emplace_back(
                &declaration, *prefix);
        }
        const auto checkpoint = hierarchy_checkpoint(child_path);
        const ScopeExit rollback_failed_child { [&, checkpoint] {
            if (diagnostics_.size() != checkpoint.diagnostics) {
                rollback_hierarchy(checkpoint);
            }
        } };
        const auto* binding = binding_for(child_path);
        const auto build_systemc =
            [&](const frontend::Instance& selected_instance,
                const std::string_view selected_target) {
                const auto* description = construct_systemc_description(
                    selected_instance,
                    child_path,
                    selected_target,
                    parameter_environment,
                    unit.language);
                if (description == nullptr) {
                    return;
                }
                auto [child_aliases, child_objects] = connect_systemc_instance(
                    selected_instance,
                    *description,
                    child_path,
                    local,
                    binding);
                instantiate_systemc(
                    *description,
                    child_path,
                    std::move(child_aliases),
                    std::move(child_objects));
            };
        if (binding != nullptr && binding->target.has_value()) {
            const auto target = parse_target(*binding->target);
            if (target
                && target->language == "systemc") {
                if (!child_defparams.empty()) {
                    report(
                        "FSIM-ELAB-DEFPARAM-004",
                        "defparam cannot target SystemC instance '"
                            + child_path + "'",
                        child_defparams.front()
                            .first->declaration->span);
                    continue;
                }
                build_systemc(instance, *binding->target);
                continue;
            }
        }
        ConfiguredVhdlInstance configured;
        ConfiguredSystemVerilogInstance systemverilog_configured;
        const frontend::Instance* selected_instance = &instance;
        const DesignUnit* target = nullptr;
        if (binding == nullptr || !binding->target.has_value()) {
            configured = instance.vhdl_configuration_instance
                ? bind_vhdl_direct_configuration_instance(
                      unit, instance, child_path)
                : bind_vhdl_component_instance(
                      unit,
                      instance,
                      child_path,
                      parameter_environment,
                      parent_domains,
                      parent_types,
                      unit.functions,
                      unit.procedures,
                      package_environment);
            if (!configured.valid) {
                continue;
            }
            if (configured.applied) {
                selected_instance = &configured.instance;
                if (configured.systemc_target.has_value()) {
                    if (!child_defparams.empty()) {
                        report(
                            "FSIM-ELAB-DEFPARAM-004",
                            "defparam cannot target SystemC instance '"
                                + child_path + "'",
                            child_defparams.front()
                                .first->declaration->span);
                        continue;
                    }
                    build_systemc(
                        *selected_instance,
                        *configured.systemc_target);
                    continue;
                }
                target = configured.target
                    ? &*configured.target
                    : nullptr;
                if (target == nullptr) {
                    continue;
                }
            }
        }
        if (target == nullptr
            && (binding == nullptr || !binding->target.has_value())) {
            systemverilog_configured = configure_systemverilog_instance(
                unit, *selected_instance, child_path);
            if (!systemverilog_configured.valid) {
                continue;
            }
            if (systemverilog_configured.applied) {
                target = systemverilog_configured.target;
            }
        }
        if (target == nullptr) {
            if ((binding == nullptr
                    || !binding->target.has_value())
                && selected_instance->unit_name.find_first_of(".(")
                    == std::string::npos) {
                const auto bound_library = systemverilog_bound_instance_libraries_.find(child_path);
                const auto library = bound_library
                        != systemverilog_bound_instance_libraries_.end()
                    ? bound_library->second
                    : (unit.library.empty() ? std::string { "work" }
                                            : unit.library);
                const auto inferred = inferred_target(
                    library,
                    selected_instance->unit_name,
                    child_path,
                    selected_instance->span);
                if (!inferred.has_value()) {
                    continue;
                }
                if (inferred->systemc_target.has_value()) {
                    if (!child_defparams.empty()) {
                        report(
                            "FSIM-ELAB-DEFPARAM-004",
                            "defparam cannot target SystemC instance '"
                                + child_path + "'",
                            child_defparams.front()
                                .first->declaration->span);
                        continue;
                    }
                    build_systemc(
                        *selected_instance,
                        *inferred->systemc_target);
                    continue;
                }
                if (inferred->udp != nullptr) {
                    if (!child_defparams.empty()) {
                        report(
                            "FSIM-ELAB-DEFPARAM-004",
                            "defparam cannot target UDP instance '"
                                + child_path + "'",
                            child_defparams.front()
                                .first->declaration->span);
                        continue;
                    }
                    instantiate_udp(
                        *inferred->udp,
                        *selected_instance,
                        child_path,
                        local,
                        local_string_objects,
                        read_only_strings,
                        local_container_objects,
                        read_only_container_objects,
                        binding);
                    continue;
                }
                target = inferred->unit;
            } else {
                target = bound_target(
                    *selected_instance,
                    unit,
                    child_path,
                    binding);
            }
        }
        if (target == nullptr)
            continue;
        if (selected_instance->anonymous) {
            report("FSIM-ELAB-BIND-062", "module instance '" + child_path + "' requires an explicit instance name", selected_instance->span);
            continue;
        }
        if (selected_instance->udp_delay
            || selected_instance->drive_strength) {
            const bool delay = selected_instance->udp_delay.has_value();
            report(delay ? "FSIM-ELAB-BIND-063" : "FSIM-ELAB-BIND-065",
                "module instance '" + child_path + "' cannot use UDP "
                    + (delay ? "propagation-delay" : "drive-strength")
                    + " syntax",
                selected_instance->span);
            continue;
        }
        if (!child_defparams.empty()
            && target->language != frontend::Language::Verilog2005
            && target->language
                != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-DEFPARAM-004",
                "defparam cannot cross into non-Verilog instance '"
                    + child_path + "'",
                child_defparams.front().first->declaration->span);
            continue;
        }
        auto defparam_instance = *selected_instance;
        std::vector<frontend::VerilogDefparamDeclaration>
            descendant_defparams;
        bool valid_defparams = true;
        for (const auto& [resolved, consumed] : child_defparams) {
            if (consumed + 1U == resolved->segments.size()) {
                const auto& parameter_name = resolved->segments.back();
                const bool duplicate = std::ranges::any_of(
                    defparam_instance.parameter_overrides,
                    [&](const auto& override) {
                        return override.name
                            && *override.name == parameter_name;
                    });
                if (duplicate) {
                    report(
                        "FSIM-ELAB-DEFPARAM-003",
                        "duplicate or conflicting override for defparam "
                        "target '"
                            + instance.name + "." + parameter_name
                            + "'",
                        resolved->declaration->span);
                    valid_defparams = false;
                    continue;
                }
                defparam_instance.parameter_overrides.emplace_back(
                    parameter_name,
                    resolved->declaration->value,
                    resolved->declaration->span);
                continue;
            }
            frontend::VerilogDefparamDeclaration descendant;
            descendant.value = resolved->declaration->value;
            descendant.span = resolved->declaration->span;
            for (std::size_t index = consumed;
                index < resolved->segments.size(); ++index) {
                frontend::VerilogDefparamPathSegment segment;
                segment.name = resolved->segments[index];
                segment.span = resolved->declaration->span;
                descendant.path.push_back(std::move(segment));
            }
            descendant_defparams.push_back(std::move(descendant));
        }
        if (!valid_defparams) {
            continue;
        }
        selected_instance = &defparam_instance;
        auto child_specialized = specialize_selected_unit(
            *target,
            selected_instance->parameter_overrides,
            parameter_environment,
            parameter_integral_environment,
            parent_domains,
            parent_types,
            unit.functions,
            unit.procedures,
            package_environment,
            unit.language);
        child_specialized.unit.verilog_defparams.insert(
            child_specialized.unit.verilog_defparams.end(),
            std::make_move_iterator(descendant_defparams.begin()),
            std::make_move_iterator(descendant_defparams.end()));
        adapt_vhdl_array_port_shapes(
            child_specialized.unit,
            *selected_instance,
            local,
            design_.signals(),
            child_specialized.identity_values);
        if (!configured.component_identity.empty()) {
            if (child_specialized.identity_values.empty()) {
                child_specialized.identity_values = child_specialized.values;
            }
            child_specialized.values.emplace_back(
                "__component",
                configured.component_name);
            child_specialized.identity_values.emplace_back(
                "__component",
                configured.component_identity);
        }
        if (!configured.configuration_identity.empty()
            && configured.component_identity.empty()) {
            child_specialized.identity_values.emplace_back(
                "__configuration",
                configured.configuration_identity);
        }
        if (!systemverilog_configured.configuration_identity.empty()) {
            child_specialized.identity_values.emplace_back(
                "__configuration",
                systemverilog_configured.configuration_identity);
        }
        auto child_aliases = connect_instance(
            *selected_instance,
            child_specialized.unit,
            child_path,
            local,
            local_string_objects,
            read_only_strings,
            local_container_objects,
            read_only_container_objects,
            binding,
            target->language != unit.language);
        for (auto& alias : child_aliases.vhdl_input_aliases) {
            child_specialized.unit.signals.push_back(std::move(alias));
        }
        for (auto& driver : child_aliases.vhdl_input_drivers) {
            child_specialized.unit.concurrent_statements.push_back(
                std::move(driver));
        }
        if (configured.referenced_configuration != nullptr) {
            vhdl_configurations_by_path_[child_path] = configured.referenced_configuration;
        }
        if (systemverilog_configured.referenced_configuration != nullptr) {
            systemverilog_configurations_by_path_[child_path] = systemverilog_configured.referenced_configuration;
        }
        auto interface_parameter_identities = target->kind
                == frontend::UnitKind::SystemVerilogInterface
            ? child_specialized.identity_values
            : std::vector<
                  std::pair<std::string, std::string>> { };
        instantiate(
            child_specialized.unit,
            child_path,
            std::move(child_aliases.signals),
            std::move(child_aliases.strings),
            std::move(child_aliases.containers),
            std::move(child_aliases.read_only_signals),
            std::move(child_aliases.read_only_strings),
            std::move(child_specialized.environment),
            std::move(child_specialized.integral_environment),
            std::move(child_specialized.values),
            std::move(child_specialized.identity_values),
            std::move(child_specialized.packages));
        if (target->kind
            == frontend::UnitKind::SystemVerilogInterface) {
            systemverilog_interface_instances_.insert_or_assign(
                child_path, child_specialized.unit);
            systemverilog_interface_handles_.try_emplace(
                child_path,
                next_systemverilog_interface_handle_++);
            systemverilog_interface_parameter_identities_
                .insert_or_assign(
                    child_path, std::move(interface_parameter_identities));
        }
    }
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
