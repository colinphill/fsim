// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include "fsim/runtime/simir_fused_container_reads.hpp"

#include <limits>
#include <type_traits>

namespace fsim::runtime::simir {

void Interpreter::Impl::build_fused_masked_normalized_programs()
{
    fused_masked_normalized_programs.assign(processes.size(), std::nullopt);
    auto mutable_objects = std::vector<std::uint8_t>(container_objects.size());
    for (const auto& object : container_objects) {
        if (object.slice_alias) {
            mutable_objects[object.slice_alias->object] = 1U;
        }
    }
    // The inventory covers every original process, including fork parents.
    // A fork child resumes a copy of its parent's program, so its writes are
    // already present here. Unknown file reads into an object are writers.
    for (const auto& state : processes) {
        const auto& program = state.program();
        for (std::size_t index = 0U; index < program.operations.size(); ++index) {
            const auto operation = program.operations.expanded(index);
            visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, WriteContainerObject>
                    || std::is_same_v<Type, WriteContainerObjectElement>) {
                    if (value.object < mutable_objects.size()) {
                        mutable_objects[value.object] = 1U;
                    }
                } else if constexpr (std::is_same_v<Type, FileBinaryRead>) {
                    if (value.target_kind
                            == FileBinaryTargetKind::container_object
                        && value.target < mutable_objects.size()) {
                        mutable_objects[value.target] = 1U;
                    }
                }
            }, operation);
        }
    }

    auto bindings = std::vector<FusedMaskedContainerRead> { };
    bindings.reserve(container_signal_aliases.size());
    for (ContainerObjectId object = 0U;
         object < container_signal_aliases.size(); ++object) {
        const auto& alias = container_signal_aliases[object];
        if (!alias || !alias->readable || !alias->writable
            || mutable_objects[object]
            || container_objects[object].slice_alias
            || alias->signal >= signals.size()
            || signal_container_aliases[alias->signal].size() != 1U
            || signals[alias->signal].value_kind != ValueKind::logic4
            || signals[alias->signal].initial_value.width()
                > std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        bindings.push_back(FusedMaskedContainerRead {
            object, alias->signal,
            &container_objects[object].initial_value.type,
            static_cast<std::uint32_t>(
                signals[alias->signal].initial_value.width())
        });
    }
    if (bindings.empty()) {
        return;
    }
    auto bound_objects = std::vector<std::uint8_t>(
        container_objects.size());
    for (const auto& binding : bindings) {
        bound_objects[binding.object] = 1U;
    }
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        const auto& original = processes[id].program();
        bool reads_bound_object { };
        if (original.container_register_count != 0U) {
            for (std::size_t index = 0U;
                 index < original.operations.size(); ++index) {
                const auto operation = original.operations.expanded(index);
                if (const auto* read
                        = operation_get_if<ReadContainerObject>(
                            &operation)) {
                    reads_bound_object |= read->object < bound_objects.size()
                        && bound_objects[read->object] != 0U;
                }
            }
        }
        if (reads_bound_object) {
            fused_masked_normalized_programs[id]
                = normalize_fused_container_reads(original, bindings);
        }
    }
}

} // namespace fsim::runtime::simir
