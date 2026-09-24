// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {

std::vector<HierarchyBuilder::CompiledVhdlInstanceMaterialization>
HierarchyBuilder::collect_compiled_vhdl_instance_worklist(
    const semantic::vhdl::Unit& architecture,
    const semantic::SpecializedHirUnit& specialization,
    const SignalMap& signals,
    const ReadOnlySignalSet& read_only_signals,
    const std::string_view path,
    const std::vector<VhdlHirMaterialization>& generated_materializations)
{
    std::vector<CompiledVhdlInstanceMaterialization> result;
    const auto append = [&](
                            const semantic::InstanceId id,
                            const semantic::SpecializedHirUnit& working_specialization,
                            const SignalMap& working_signals,
                            const ReadOnlySignalSet& working_read_only_signals,
                            const std::string_view working_path,
                            const bool generated) {
        if (const auto instance
            = working_specialization.find_instance(id)) {
            result.push_back({
                *instance,
                &working_specialization,
                &working_signals,
                &working_read_only_signals,
                working_path,
                generated,
            });
        }
    };

    for (const auto instance : architecture.instances) {
        append(instance, specialization, signals, read_only_signals,
            path, false);
    }
    for (const auto& materialization : generated_materializations) {
        for (const auto instance : materialization.region->instances) {
            append(instance, materialization.specialization,
                materialization.signals,
                materialization.read_only_signals,
                materialization.path, true);
        }
    }

    return result;
}

} // namespace fsim::elaboration
