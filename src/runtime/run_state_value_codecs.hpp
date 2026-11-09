// SPDX-License-Identifier: Apache-2.0
#pragma once

// Run-state codecs (run_state_archive.hpp) for the runtime value types that are
// not aggregates the generic walker can read: a scheduled-task handle, a
// container value and the class heap's handle container. Both the engine sections (src/runtime) and the
// application sections (src/app) include this header so one description serves
// every user (IEEE 1800-2023 D.9).

#include "fsim/runtime/class_heap.hpp"
#include "run_state_archive.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir_container_value.hpp"

#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fsim::runtime {

/// A scheduled-task handle names a slot of the running scheduler, so a file
/// holds only whether it was live; the owner of the record binds the new handle
/// when the event is restored (Interpreter::Impl::bind_restored_event_handle).
template <> struct RunStateCodec<ScheduledTaskHandle> {
    using is_codec = void;
    static void io(RunStateArchive& archive, ScheduledTaskHandle& handle)
    {
        std::uint8_t live = static_cast<bool>(handle) ? 1U : 0U;
        archive.io(live);
        if (archive.loading()) {
            handle = ScheduledTaskHandle { };
        }
    }
};

/// ContainerValue has constructors, so it is not an aggregate.
template <> struct RunStateCodec<simir::ContainerValue> {
    using is_codec = void;
    static void io(RunStateArchive& archive, simir::ContainerValue& value)
    {
        archive.io(value.type);
        archive.io(value.elements);
        archive.io(value.string_elements);
        archive.io(value.nested_elements);
        archive.io(value.keys);
        archive.io(value.string_keys);
        archive.io(value.shape);
        archive.io(value.associative_default);
        archive.io(value.associative_default_text);
    }
};

/// A handle container keeps its elements private. The handles name objects the
/// heap section restores, so loading does not check them against a heap (that
/// would see the heap before it is restored).
template <> struct RunStateCodec<SystemVerilogClassHandleContainer> {
    using is_codec = void;
    static void io(
        RunStateArchive& archive, SystemVerilogClassHandleContainer& value)
    {
        auto descriptor = value.descriptor();
        const auto elements = value.sequential_values();
        std::vector<SystemVerilogClassHandle> sequential(
            elements.begin(), elements.end());
        auto keyed = value.keyed_values();
        archive.io(descriptor);
        archive.io(sequential);
        archive.io(keyed);
        if (archive.loading() && archive.ok()) {
            value = SystemVerilogClassHandleContainer { std::move(descriptor) };
            value.restore_state(std::move(sequential), std::move(keyed));
        }
    }
};

} // namespace fsim::runtime
