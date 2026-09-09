// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

void Interpreter::Impl::capture_callable_values(
    ProcessState& process,
    ProcessState::CallableFrameState& context,
    const std::set<RegisterId>& excluded_packed,
    const std::set<StringRegisterId>& excluded_strings,
    const std::set<ContainerRegisterId>& excluded_containers)
{
    if (context.packed.size() != context.packed_ids.size()) {
        context.packed.resize(context.packed_ids.size());
    }
    if (context.strings.size() != context.string_ids.size()) {
        context.strings.resize(context.string_ids.size());
    }
    if (context.containers.size() != context.container_ids.size()) {
        context.containers.resize(context.container_ids.size());
    }
    for (std::size_t index = 0; index < context.packed_ids.size(); ++index) {
        if (excluded_packed.contains(context.packed_ids[index])) {
            continue;
        }
        context.packed[index] = process.executor
            ? process.executor->snapshot_register(context.packed_ids[index])
            : get_register(process, context.packed_ids[index]);
    }
    for (std::size_t index = 0; index < context.string_ids.size(); ++index) {
        if (excluded_strings.contains(context.string_ids[index])) {
            continue;
        }
        context.strings[index] = process.executor
            ? process.executor->read_string_register(context.string_ids[index])
            : get_string_register(process, context.string_ids[index]);
    }
    for (std::size_t index = 0; index < context.container_ids.size(); ++index) {
        if (excluded_containers.contains(context.container_ids[index])) {
            continue;
        }
        context.containers[index] = process.executor
            ? std::make_shared<ContainerValue>(
                process.executor->read_container_register(
                    context.container_ids[index]))
            : container_register_storage(process, context.container_ids[index]);
    }

    context.storage_bytes = 0U;
    const auto account = [&](const std::size_t bytes) {
        if (bytes > maximum_container_storage_bytes - context.storage_bytes) {
            fail(process,
                "automatic callable capture exceeds its owning-storage budget");
        }
        context.storage_bytes += bytes;
    };
    for (const auto& value : context.packed) {
        account(
            sizeof(PackedLogic4)
            + value.aval_words().size_bytes()
            + value.bval_words().size_bytes()
                * (value.is_logic9() ? 3U : 1U));
    }
    for (const auto& value : context.strings) {
        account(sizeof(std::string) + value.size());
    }
    for (const auto& value : context.containers) {
        if (!value) {
            fail(process, "automatic callable capture has null container storage");
        }
        account(
            sizeof(ContainerValue) + container_value_storage_bytes(*value));
    }
}

void Interpreter::Impl::restore_callable_values(
    ProcessState& process,
    const ProcessState::CallableFrameState& context)
{
    for (std::size_t index = 0; index < context.packed.size(); ++index) {
        if (process.executor) {
            process.executor->write_register(
                context.packed_ids[index], context.packed[index]);
        } else {
            get_register(process, context.packed_ids[index])
                = context.packed[index];
        }
    }
    for (std::size_t index = 0; index < context.strings.size(); ++index) {
        if (process.executor) {
            process.executor->write_string_register(
                context.string_ids[index], context.strings[index]);
        } else {
            get_string_register(process, context.string_ids[index])
                = context.strings[index];
        }
    }
    for (std::size_t index = 0; index < context.containers.size(); ++index) {
        if (!context.containers[index]) {
            fail(process, "automatic callable capture has null container storage");
        }
        if (process.executor) {
            process.executor->write_container_register_storage(
                context.container_ids[index], context.containers[index]);
        } else {
            set_container_register_storage(
                process, context.container_ids[index],
                context.containers[index]);
        }
    }
}

} // namespace fsim::runtime::simir
