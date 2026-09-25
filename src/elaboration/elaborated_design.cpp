// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {

const std::string& ElaboratedDesign::top() const noexcept
{
    return top_;
}

const std::vector<std::string>& ElaboratedDesign::roots() const noexcept
{
    return roots_;
}

const semantic::HierarchyPathTable&
ElaboratedDesign::hierarchy_paths() const noexcept
{
    return hierarchy_paths_;
}

bool ElaboratedDesign::rebind_path_table(
    semantic::HierarchyPathTable paths)
{
    if (paths.size() < hierarchy_paths_.size()) {
        return false;
    }
    for (std::size_t index = 0; index < hierarchy_paths_.size(); ++index) {
        const auto id = semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        if (paths.view(id) != hierarchy_paths_.view(id)) {
            return false;
        }
    }
    hierarchy_paths_ = std::move(paths);
    return true;
}

bool ElaboratedDesign::remap_path_table(
    semantic::HierarchyPathTable paths)
{
    for (std::size_t index = 0; index < hierarchy_paths_.size(); ++index) {
        const auto id = semantic::HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        if (!paths.contains(hierarchy_paths_.view(id))) {
            return false;
        }
    }

    decltype(signal_by_path_) remapped_signals;
    decltype(string_by_path_) remapped_strings;
    decltype(container_by_path_) remapped_containers;
    remapped_signals.reserve(signal_by_path_.size());
    remapped_strings.reserve(string_by_path_.size());
    remapped_containers.reserve(container_by_path_.size());
    for (const auto& [id, signal] : signal_by_path_) {
        remapped_signals.emplace(*paths.find(hierarchy_paths_.view(id)), signal);
    }
    for (const auto& [id, object] : string_by_path_) {
        remapped_strings.emplace(*paths.find(hierarchy_paths_.view(id)), object);
    }
    for (const auto& [id, object] : container_by_path_) {
        remapped_containers.emplace(*paths.find(hierarchy_paths_.view(id)), object);
    }
    signal_by_path_.swap(remapped_signals);
    string_by_path_.swap(remapped_strings);
    container_by_path_.swap(remapped_containers);
    hierarchy_paths_ = std::move(paths);
    return true;
}

void ElaboratedDesign::freeze_hierarchy_paths()
{
    std::vector<std::string_view> paths;
    const auto add = [&](const std::string_view path) {
        if (!path.empty()) {
            paths.push_back(path);
        }
    };
    add(top_);
    for (const auto& root : roots_) {
        add(root);
    }
    for (const auto& signal : signal_info_) {
        add(signal.name);
        for (const auto& binding : signal.vhdl_mode_view_bindings) {
            for (const auto& element : binding.elements) {
                add(element.formal_path);
                add(element.actual_path);
            }
        }
    }
    for (const auto& conversion : boundary_conversions_) {
        add(conversion.path);
    }
    for (const auto& signal : signals_) {
        add(signal.name);
    }
    for (const auto& object : string_object_info_) {
        add(object.name);
    }
    for (const auto& object : string_objects_) {
        add(object.name);
    }
    for (const auto& object : container_object_info_) {
        add(object.name);
    }
    for (const auto& object : container_objects_) {
        add(object.name);
    }
    for (const auto& object : vhdl_protected_object_info_) {
        add(object.name);
    }
    for (const auto& process : processes_) {
        add(process.name);
    }
    for (const auto& specialization : specializations_) {
        add(specialization.instance);
    }
    for (const auto& specify : verilog_specify_paths_) {
        add(specify.instance);
    }
    for (const auto& instance : systemc_instances_) {
        add(instance.instance);
    }
    for (const auto& object : systemc_objects_) {
        add(object.name);
        add(object.parent);
    }
    for (const auto& [path, signal] : signal_by_name_) {
        (void)signal;
        paths.push_back(path);
    }
    for (const auto& [path, object] : string_by_name_) {
        (void)object;
        paths.push_back(path);
    }
    for (const auto& [path, object] : container_by_name_) {
        (void)object;
        paths.push_back(path);
    }
    std::ranges::sort(paths);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    semantic::HierarchyPathTable::Builder builder;
    for (const auto path : paths) {
        (void)builder.intern(path);
    }
    signal_by_path_.clear();
    string_by_path_.clear();
    container_by_path_.clear();
    signal_by_path_.reserve(signal_by_name_.size());
    string_by_path_.reserve(string_by_name_.size());
    container_by_path_.reserve(container_by_name_.size());
    for (const auto& [path, signal] : signal_by_name_) {
        signal_by_path_.emplace(*builder.find(path), signal);
    }
    for (const auto& [path, object] : string_by_name_) {
        string_by_path_.emplace(*builder.find(path), object);
    }
    for (const auto& [path, object] : container_by_name_) {
        container_by_path_.emplace(*builder.find(path), object);
    }
    hierarchy_paths_ = std::move(builder).freeze();
    signal_by_name_.clear();
    string_by_name_.clear();
    container_by_name_.clear();
}

const std::vector<SignalInfo>&
ElaboratedDesign::signals() const noexcept
{
    return signal_info_;
}

const std::vector<BoundaryConversionInfo>&
ElaboratedDesign::boundary_conversions() const noexcept
{
    return boundary_conversions_;
}

const std::vector<StringObjectInfo>&
ElaboratedDesign::string_objects() const noexcept
{
    return string_object_info_;
}

const std::vector<ContainerObjectInfo>&
ElaboratedDesign::container_objects() const noexcept
{
    return container_object_info_;
}

const std::vector<VhdlProtectedObjectInfo>&
ElaboratedDesign::vhdl_protected_objects() const noexcept
{
    return vhdl_protected_object_info_;
}

const std::vector<runtime::simir::Process>&
ElaboratedDesign::processes() const noexcept
{
    return processes_;
}

const std::vector<SpecializationInfo>&
ElaboratedDesign::specializations() const noexcept
{
    return specializations_;
}

const std::vector<UdpTableInfo>&
ElaboratedDesign::udp_tables() const noexcept
{
    return udp_tables_;
}

const std::vector<VerilogSpecifyPathInfo>&
ElaboratedDesign::verilog_specify_paths() const noexcept
{
    return verilog_specify_paths_;
}

const std::vector<runtime::simir::ModuleTimingCheck>&
ElaboratedDesign::verilog_timing_checks() const noexcept
{
    return verilog_timing_checks_;
}

const std::vector<SystemCInstanceInfo>&
ElaboratedDesign::systemc_instances() const noexcept
{
    return systemc_instances_;
}

const std::vector<SystemCProcessInfo>&
ElaboratedDesign::systemc_processes() const noexcept
{
    return systemc_processes_;
}

const std::vector<SystemCNamedObjectInfo>&
ElaboratedDesign::systemc_objects() const noexcept
{
    return systemc_objects_;
}

std::optional<runtime::simir::SignalId>
ElaboratedDesign::find_signal(
    const std::string_view name) const noexcept
{
    if (const auto path = hierarchy_paths_.find(name)) {
        if (const auto found = signal_by_path_.find(*path);
            found != signal_by_path_.end()) {
            return found->second;
        }
    }
    const auto object = std::find_if(
        systemc_objects_.begin(), systemc_objects_.end(),
        [&](const SystemCNamedObjectInfo& candidate) {
            return candidate.name == name && candidate.signal.has_value();
        });
    if (object != systemc_objects_.end()) {
        return object->signal;
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, runtime::simir::SignalId>>
ElaboratedDesign::signal_paths() const
{
    std::vector<std::pair<std::string, runtime::simir::SignalId>> result;
    result.reserve(signal_by_path_.size() + systemc_objects_.size());
    for (const auto& [path, signal] : signal_by_path_) {
        result.emplace_back(hierarchy_paths_.view(path), signal);
    }
    for (const auto& object : systemc_objects_) {
        if (object.signal) {
            result.emplace_back(object.name, *object.signal);
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& left, const auto& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            return left.second < right.second;
        });
    result.erase(
        std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::optional<runtime::simir::ContainerObjectId>
ElaboratedDesign::find_container(
    const std::string_view name) const noexcept
{
    if (const auto path = hierarchy_paths_.find(name)) {
        if (const auto found = container_by_path_.find(*path);
            found != container_by_path_.end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

std::vector<std::pair<
    std::string, runtime::simir::ContainerObjectId>>
ElaboratedDesign::container_paths() const
{
    std::vector<std::pair<
        std::string, runtime::simir::ContainerObjectId>>
        result;
    result.reserve(container_by_path_.size());
    for (const auto& [path, object] : container_by_path_) {
        result.emplace_back(hierarchy_paths_.view(path), object);
    }
    std::ranges::sort(
        result,
        [](const auto& left, const auto& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            return left.second < right.second;
        });
    return result;
}

std::unique_ptr<runtime::simir::Interpreter>
ElaboratedDesign::create_interpreter(
    const runtime::SchedulerOptions options,
    const std::uint64_t seed) const&
{
    auto interpreter = std::make_unique<runtime::simir::Interpreter>(options, seed);
    populate_interpreter(interpreter.get(), false);
    return interpreter;
}

std::unique_ptr<runtime::simir::Interpreter>
ElaboratedDesign::create_interpreter(
    const runtime::SchedulerOptions options,
    const std::uint64_t seed) &&
{
    auto interpreter = std::make_unique<runtime::simir::Interpreter>(options, seed);
    populate_interpreter(interpreter.get(), false, &processes_);
    return interpreter;
}

void ElaboratedDesign::populate_interpreter(
    runtime::simir::Interpreter* const interpreter,
    const bool validation_only,
    std::vector<runtime::simir::Process>* const consumed_processes) const
{
    if (interpreter == nullptr) {
        throw std::invalid_argument("cannot populate a null SimIR interpreter");
    }
    for (const auto& signal : signals_) {
        (void)interpreter->add_signal(signal);
    }
    for (const auto& object : string_objects_) {
        (void)interpreter->add_string_object(object);
    }
    for (const auto& object : container_objects_) {
        (void)interpreter->add_container_object(object);
    }
    for (const auto& alias : container_signal_aliases_) {
        interpreter->add_container_signal_alias(alias);
    }
    if (consumed_processes != nullptr) {
        for (auto& process : *consumed_processes) {
            (void)interpreter->add_process(std::move(process));
        }
        consumed_processes->clear();
    } else {
        for (const auto& process : processes_) {
            if (validation_only) {
                (void)interpreter->validate_process(process);
            } else {
                (void)interpreter->add_process(process);
            }
        }
    }
    for (const auto& path : verilog_specify_paths_) {
        runtime::simir::ModulePath runtime_path;
        runtime_path.id = path.id;
        runtime_path.identity = path.identity;
        const auto append_terminals = [](const auto& source, auto& destination) {
            destination.reserve(source.size());
            for (const auto& terminal : source) {
                destination.push_back(runtime::simir::ModulePathTerminal {
                    terminal.signal, terminal.offset, terminal.width });
            }
        };
        append_terminals(path.sources, runtime_path.sources);
        append_terminals(path.destinations, runtime_path.destinations);
        runtime_path.drivers = path.drivers;
        runtime_path.delays = path.delays;
        runtime_path.condition = path.condition_program;
        runtime_path.data_source = path.data_source_program;
        runtime_path.selection_group = path.selection_group;
        runtime_path.full = path.kind == frontend::VerilogModulePathKind::Full;
        runtime_path.conditional = path.conditional;
        runtime_path.ifnone = path.ifnone;
        switch (path.source_edge) {
        case frontend::VerilogSpecifyEdge::None:
            runtime_path.source_edge = runtime::simir::ModulePathEdge::none;
            break;
        case frontend::VerilogSpecifyEdge::Posedge:
            runtime_path.source_edge = runtime::simir::ModulePathEdge::posedge;
            break;
        case frontend::VerilogSpecifyEdge::Negedge:
            runtime_path.source_edge = runtime::simir::ModulePathEdge::negedge;
            break;
        case frontend::VerilogSpecifyEdge::Edge:
            runtime_path.source_edge = runtime::simir::ModulePathEdge::edge;
            break;
        }
        switch (path.polarity) {
        case frontend::VerilogPathPolarity::None:
            runtime_path.polarity = runtime::simir::ModulePathPolarity::none;
            break;
        case frontend::VerilogPathPolarity::Positive:
            runtime_path.polarity = runtime::simir::ModulePathPolarity::positive;
            break;
        case frontend::VerilogPathPolarity::Negative:
            runtime_path.polarity = runtime::simir::ModulePathPolarity::negative;
            break;
        }
        runtime_path.pulse_style = path.pulse_style == frontend::VerilogPulseStyle::Ondetect
            ? runtime::simir::ModulePathPulseStyle::ondetect
            : runtime::simir::ModulePathPulseStyle::onevent;
        runtime_path.show_cancelled = path.show_cancelled;
        runtime_path.pulse_reject_limit = path.pulse_reject_limit;
        runtime_path.pulse_error_limit = path.pulse_error_limit;
        runtime_path.pulse_reject_delays = path.pulse_reject_delays;
        runtime_path.pulse_error_delays = path.pulse_error_delays;
        runtime_path.retain_delays = path.retain_delays;
        runtime_path.source = runtime::simir::SourceLocation {
            path.source.source_name.str(),
            static_cast<std::uint32_t>(path.source.begin.line),
            static_cast<std::uint32_t>(path.source.begin.column)
        };
        (void)interpreter->add_module_path(std::move(runtime_path));
    }
    for (const auto& check : verilog_timing_checks_) {
        (void)interpreter->add_module_timing_check(check);
    }
}

ElaboratedDesignState ElaboratedDesign::state() const&
{
    ElaboratedDesignState result {
        top_, roots_, signal_info_, boundary_conversions_, signals_,
        string_object_info_, string_objects_, container_object_info_,
        container_objects_, container_signal_aliases_,
        vhdl_protected_object_info_, processes_,
        specializations_, udp_tables_, verilog_specify_paths_,
        verilog_timing_checks_,
        systemc_instances_, systemc_processes_, systemc_objects_, { }, { },
        { }, code_coverage_inventory_
    };
    result.signal_names.reserve(signal_by_path_.size());
    for (const auto& [path, signal] : signal_by_path_) {
        result.signal_names.emplace_back(hierarchy_paths_.view(path), signal);
    }
    result.string_names.reserve(string_by_path_.size());
    for (const auto& [path, object] : string_by_path_) {
        result.string_names.emplace_back(hierarchy_paths_.view(path), object);
    }
    result.container_names.reserve(container_by_path_.size());
    for (const auto& [path, object] : container_by_path_) {
        result.container_names.emplace_back(hierarchy_paths_.view(path), object);
    }
    const auto by_name = [](const auto& left, const auto& right) {
        return left.first < right.first;
    };
    std::ranges::sort(result.signal_names, by_name);
    std::ranges::sort(result.string_names, by_name);
    std::ranges::sort(result.container_names, by_name);
    return result;
}

ElaboratedDesignState ElaboratedDesign::state() &&
{
    ElaboratedDesignState result {
        std::move(top_), std::move(roots_), std::move(signal_info_),
        std::move(boundary_conversions_), std::move(signals_),
        std::move(string_object_info_), std::move(string_objects_),
        std::move(container_object_info_), std::move(container_objects_),
        std::move(container_signal_aliases_),
        std::move(vhdl_protected_object_info_), std::move(processes_),
        std::move(specializations_), std::move(udp_tables_),
        std::move(verilog_specify_paths_), std::move(verilog_timing_checks_),
        std::move(systemc_instances_), std::move(systemc_processes_),
        std::move(systemc_objects_), { }, { }, { },
        std::move(code_coverage_inventory_)
    };
    result.signal_names.reserve(signal_by_path_.size());
    for (const auto& [path, signal] : signal_by_path_) {
        result.signal_names.emplace_back(hierarchy_paths_.view(path), signal);
    }
    result.string_names.reserve(string_by_path_.size());
    for (const auto& [path, object] : string_by_path_) {
        result.string_names.emplace_back(hierarchy_paths_.view(path), object);
    }
    result.container_names.reserve(container_by_path_.size());
    for (const auto& [path, object] : container_by_path_) {
        result.container_names.emplace_back(hierarchy_paths_.view(path), object);
    }
    const auto by_name = [](const auto& left, const auto& right) {
        return left.first < right.first;
    };
    std::ranges::sort(result.signal_names, by_name);
    std::ranges::sort(result.string_names, by_name);
    std::ranges::sort(result.container_names, by_name);
    return result;
}

std::optional<ElaboratedDesign> ElaboratedDesign::from_state(
    ElaboratedDesignState state)
{
    if (state.top.empty() || state.roots.empty()
        || state.signal_info.size() != state.signals.size()
        || state.string_object_info.size() != state.string_objects.size()
        || state.container_object_info.size() != state.container_objects.size()) {
        return std::nullopt;
    }
    if (state.code_coverage_inventory) {
        std::vector<CoverageInventoryOwner> owners;
        owners.reserve(state.specializations.size());
        for (const auto& specialization : state.specializations) {
            const auto& parameters
                = specialization.parameter_identity_values.empty()
                ? specialization.parameter_values
                : specialization.parameter_identity_values;
            owners.push_back(CoverageInventoryOwner {
                specialization.id, specialization.instance,
                specialization.language, specialization.source,
                specialization.source_dependencies, specialization.library,
                specialization.unit, parameters });
        }
        if (!validate_code_coverage_inventory(
                *state.code_coverage_inventory, owners)
                .ok()) {
            return std::nullopt;
        }
    }
    for (std::size_t index = 0; index < state.signal_info.size(); ++index) {
        const auto valid_strength = [](const runtime::simir::StrengthRank rank) {
            return static_cast<std::underlying_type_t<
                       runtime::simir::StrengthRank>>(rank)
                <= static_cast<std::underlying_type_t<
                    runtime::simir::StrengthRank>>(
                    runtime::simir::StrengthRank::supply);
        };
        const auto& signal_info = state.signal_info[index];
        if (signal_info.id != index
            || state.signal_info[index].width
                != state.signals[index].initial_value.width()
            || !valid_strength(
                state.signals[index].implicit_drive_strength.zero)
            || !valid_strength(
                state.signals[index].implicit_drive_strength.one)
            || (state.signals[index].charge_strength
                && !valid_strength(
                    *state.signals[index].charge_strength))) {
            return std::nullopt;
        }
        for (const auto& binding : signal_info.vhdl_mode_view_bindings) {
            if (binding.formal.empty() || binding.view.empty()
                || binding.elements.size() > 65'536U) {
                return std::nullopt;
            }
            std::unordered_set<std::uint64_t> endpoint_offsets;
            endpoint_offsets.reserve(binding.elements.size());
            for (const auto& endpoint : binding.elements) {
                if (endpoint.formal_path.empty()
                    || endpoint.actual_path.empty()
                    || endpoint.signal != index
                    || endpoint.width == 0U
                    || endpoint.lsb_offset > signal_info.width
                    || endpoint.width
                        > signal_info.width - endpoint.lsb_offset
                    || !endpoint_offsets.insert(endpoint.lsb_offset).second) {
                    return std::nullopt;
                }
            }
        }
    }
    for (std::size_t index = 0; index < state.string_object_info.size(); ++index) {
        if (state.string_object_info[index].id != index) {
            return std::nullopt;
        }
    }
    for (std::size_t index = 0; index < state.container_object_info.size(); ++index) {
        if (state.container_object_info[index].id != index) {
            return std::nullopt;
        }
    }
    for (std::size_t index = 0; index < state.processes.size(); ++index) {
        const auto& process = state.processes[index];
        const auto valid_strength = [](const runtime::simir::StrengthRank rank) {
            return static_cast<std::underlying_type_t<
                       runtime::simir::StrengthRank>>(rank)
                <= static_cast<std::underlying_type_t<
                    runtime::simir::StrengthRank>>(
                    runtime::simir::StrengthRank::supply);
        };
        if (process.id != index
            || !valid_strength(process.drive_strength.zero)
            || !valid_strength(process.drive_strength.one)
            || (process.switch_source
                && *process.switch_source
                    >= state.signals.size())
            || (process.switch_target
                && *process.switch_target
                    >= state.signals.size())
            || (process.switch_control
                && *process.switch_control
                    >= state.signals.size())
            || ((process.switch_source_offset != 0
                    || process.switch_target_offset != 0
                    || process.switch_width != 0)
                && (!process.switch_source || !process.switch_target))
            || (process.switch_bidirectional
                && (!process.switch_source || !process.switch_target))) {
            return std::nullopt;
        }
        if (process.switch_source && process.switch_target) {
            const auto source_width = state.signals[*process.switch_source]
                                          .initial_value.width();
            const auto target_width = state.signals[*process.switch_target]
                                          .initial_value.width();
            const auto selected_width = process.switch_width;
            const bool invalid_selected_region = selected_width != 0
                && (process.switch_source_offset > source_width
                    || selected_width
                        > source_width - process.switch_source_offset
                    || process.switch_target_offset > target_width
                    || selected_width
                        > target_width - process.switch_target_offset);
            if (invalid_selected_region
                || (selected_width == 0
                    && (process.switch_source_offset != 0
                        || process.switch_target_offset != 0
                        || (source_width != target_width
                            && source_width != 1 && target_width != 1)))
                || (process.switch_control
                    && state.signals[*process.switch_control]
                            .initial_value.width()
                        != 1
                    && state.signals[*process.switch_control]
                            .initial_value.width()
                        != (selected_width == 0
                                ? std::max(source_width, target_width)
                                : selected_width))) {
                return std::nullopt;
            }
        }
    }
    for (std::size_t index = 0; index < state.specializations.size(); ++index) {
        if (state.specializations[index].id != index) {
            return std::nullopt;
        }
    }
    for (std::size_t index = 0; index < state.udp_tables.size(); ++index) {
        const auto& table = state.udp_tables[index];
        const auto digest_is_hex = table.digest.size() == 64
            && std::ranges::all_of(table.digest, [](const char character) {
                   return (character >= '0' && character <= '9')
                       || (character >= 'a' && character <= 'f');
               });
        std::unordered_set<std::string_view> terminal_names;
        bool terminals_valid = !table.terminals.empty();
        for (const auto& terminal : table.terminals) {
            terminals_valid = terminals_valid && !terminal.empty()
                && terminal_names.insert(terminal).second;
        }
        if (table.id != index || !table.identity.starts_with("udp:")
            || !digest_is_hex || !terminals_valid
            || !frontend::verilog_udp_table_well_formed(
                table.sequential,
                table.terminals.size() - 1U,
                table.initial_output,
                table.rows)) {
            return std::nullopt;
        }
    }
    std::unordered_map<std::string, std::string> udp_digests;
    for (const auto& table : state.udp_tables) {
        if (!udp_digests.emplace(table.identity, table.digest).second) {
            return std::nullopt;
        }
    }
    std::unordered_set<std::string> referenced_udp_tables;
    for (const auto& specialization : state.specializations) {
        std::optional<std::string_view> identity;
        std::optional<std::string_view> digest;
        for (const auto& [name, value] :
            specialization.parameter_identity_values) {
            if (name == "__udp") {
                if (identity)
                    return std::nullopt;
                identity = value;
            } else if (name == "__udp_table") {
                if (digest)
                    return std::nullopt;
                digest = value;
            }
        }
        if (identity.has_value() != digest.has_value())
            return std::nullopt;
        if (identity) {
            const auto table = udp_digests.find(std::string { *identity });
            if (table == udp_digests.end() || table->second != *digest) {
                return std::nullopt;
            }
            referenced_udp_tables.emplace(*identity);
        }
    }
    if (referenced_udp_tables.size() != state.udp_tables.size()) {
        return std::nullopt;
    }
    std::unordered_set<std::string> specify_identities;
    for (std::size_t index = 0;
        index < state.verilog_specify_paths.size(); ++index) {
        const auto& path = state.verilog_specify_paths[index];
        const auto valid_terminal = [&](const VerilogSpecifyTerminalInfo& terminal) {
            return terminal.signal < state.signals.size()
                && terminal.width != 0
                && terminal.offset
                <= state.signals[terminal.signal].initial_value.width()
                && terminal.width
                <= state.signals[terminal.signal].initial_value.width()
                    - terminal.offset;
        };
        const bool valid_delay_count = path.delays.size() == 1
            || path.delays.size() == 2 || path.delays.size() == 3
            || path.delays.size() == 6 || path.delays.size() == 12;
        if (path.id != index || path.instance.empty()
            || !path.identity.starts_with("sdf:iopath:" + path.instance + ":")
            || !specify_identities.emplace(path.identity).second
            || path.sources.empty() || path.destinations.empty()
            || !valid_delay_count
            || !std::ranges::all_of(path.sources, valid_terminal)
            || !std::ranges::all_of(path.destinations, valid_terminal)
            || (path.kind == frontend::VerilogModulePathKind::Parallel
                && (path.sources.size() != path.destinations.size()
                    || !std::ranges::equal(
                        path.sources, path.destinations,
                        [](const auto& left, const auto& right) {
                            return left.width == right.width;
                        })))) {
            return std::nullopt;
        }
        if (!std::ranges::is_sorted(path.drivers)
            || std::ranges::adjacent_find(path.drivers)
                != path.drivers.end()
            || !std::ranges::all_of(
                path.drivers,
                [&](const runtime::simir::ProcessId driver) {
                    return driver < state.processes.size();
                })) {
            return std::nullopt;
        }
    }
    for (const auto& check : state.verilog_timing_checks) {
        if (!check.identity.starts_with("sdf:timingcheck:")
            || !specify_identities.emplace(check.identity).second) {
            return std::nullopt;
        }
    }
    ElaboratedDesign result;
    result.top_ = std::move(state.top);
    result.roots_ = std::move(state.roots);
    result.signal_info_ = std::move(state.signal_info);
    result.boundary_conversions_ = std::move(state.boundary_conversions);
    result.signals_ = std::move(state.signals);
    result.string_object_info_ = std::move(state.string_object_info);
    result.string_objects_ = std::move(state.string_objects);
    result.container_object_info_ = std::move(state.container_object_info);
    result.container_objects_ = std::move(state.container_objects);
    result.container_signal_aliases_ = std::move(state.container_signal_aliases);
    result.vhdl_protected_object_info_ = std::move(state.vhdl_protected_object_info);
    result.processes_ = std::move(state.processes);
    result.specializations_ = std::move(state.specializations);
    result.udp_tables_ = std::move(state.udp_tables);
    result.verilog_specify_paths_ = std::move(state.verilog_specify_paths);
    result.verilog_timing_checks_ = std::move(state.verilog_timing_checks);
    result.systemc_instances_ = std::move(state.systemc_instances);
    result.systemc_processes_ = std::move(state.systemc_processes);
    result.systemc_objects_ = std::move(state.systemc_objects);
    result.code_coverage_inventory_
        = std::move(state.code_coverage_inventory);
    for (auto& entry : state.signal_names) {
        if (entry.first.empty() || entry.second >= result.signals_.size()
            || !result.signal_by_name_.emplace(std::move(entry)).second) {
            return std::nullopt;
        }
    }
    for (auto& entry : state.string_names) {
        if (entry.first.empty() || entry.second >= result.string_objects_.size()
            || !result.string_by_name_.emplace(std::move(entry)).second) {
            return std::nullopt;
        }
    }
    for (auto& entry : state.container_names) {
        if (entry.first.empty() || entry.second >= result.container_objects_.size()
            || !result.container_by_name_.emplace(std::move(entry)).second) {
            return std::nullopt;
        }
    }
    result.freeze_hierarchy_paths();
    try {
        runtime::simir::Interpreter validator;
        result.populate_interpreter(&validator, true);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return result;
}

} // namespace fsim::elaboration
