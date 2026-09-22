// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {
    bool belongs_to_hierarchy(
        const std::string_view candidate,
        const std::string_view path)
    {
        return candidate == path
            || (candidate.size() > path.size()
                && candidate.starts_with(path)
                && candidate[path.size()] == '.');
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
        selected_systemverilog_classes_.size(),
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
        systemverilog_resolution_unit_registrations_.size(),
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
    selected_systemverilog_classes_.resize(
        checkpoint.selected_systemverilog_classes);
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
    erase_path_map(systemverilog_interface_handles_);
    erase_path_map(systemverilog_interface_parameter_identities_);
    erase_path_map(systemverilog_interface_types_);
    erase_path_map(systemverilog_interface_modport_views_);
    erase_path_map(systemverilog_clocking_event_signals_);
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
    systemverilog_resolution_unit_registrations_.resize(
        checkpoint.systemverilog_resolution_unit_registrations);
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
} // namespace fsim::elaboration
