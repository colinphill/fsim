// SPDX-License-Identifier: Apache-2.0
#pragma once

// Run-state codecs (run_state_archive.hpp) for the handles of the UVM services
// (#188; IEEE 1800-2023 D.9, IEEE 1800.2-2020). A handle is an owner token
// with a slot and a generation, so a file holds the slot and the generation,
// and a restore binds them to the owner token of the restoring run's service,
// which the section's visitor puts in the archive's context.

#include "run_state_archive.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_phase.hpp"

#include <cstdint>
#include <memory>

namespace fsim::runtime {

/// The owner tokens of the services whose handles a section holds
/// (SystemVerilogUvmPhaseService::run_state_owner and the objection
/// service's); set with RunStateArchive::set_context.
struct SystemVerilogUvmRunStateOwners {
    std::shared_ptr<const void> phases;
    std::shared_ptr<const void> objections;
};

/// The friend of the handle classes: reads and writes their private fields.
class SystemVerilogUvmRunStateAccess final {
public:
    [[nodiscard]] static const SystemVerilogUvmRunStateOwners* owners(
        RunStateArchive& archive)
    {
        const auto* const found
            = static_cast<const SystemVerilogUvmRunStateOwners*>(archive.context());
        if (found == nullptr) {
            archive.fail("a UVM handle needs the owners of the UVM services");
        }
        return found;
    }

    template <typename Handle>
    static void slot_handle(RunStateArchive& archive, Handle& handle,
        const bool objection_service)
    {
        std::uint64_t slot = handle.slot_;
        std::uint64_t generation = handle.generation_;
        archive.io(slot);
        archive.io(generation);
        if (archive.loading() && archive.ok()) {
            if (slot == 0U && generation == 0U) {
                handle = Handle { };
                return;
            }
            const auto* const known = owners(archive);
            if (known == nullptr) {
                return;
            }
            const auto& owner
                = objection_service ? known->objections : known->phases;
            if (!owner) {
                archive.fail("a UVM handle names a service that is not part "
                             "of this run");
                return;
            }
            handle = Handle { owner, slot, generation };
        }
    }

    static void source_handle(
        RunStateArchive& archive, SystemVerilogUvmObjectionSourceHandle& handle)
    {
        SystemVerilogClassHandle object = handle.object_;
        SystemVerilogUvmRootHandle root = handle.root_;
        archive.io(object);
        archive.io(root);
        if (archive.loading() && archive.ok()) {
            if (object == 0U && root == 0U) {
                handle = SystemVerilogUvmObjectionSourceHandle { };
                return;
            }
            const auto* const known = owners(archive);
            if (known == nullptr) {
                return;
            }
            if (!known->objections) {
                archive.fail("a UVM handle names a service that is not part "
                             "of this run");
                return;
            }
            handle = SystemVerilogUvmObjectionSourceHandle {
                known->objections, object, root
            };
        }
    }
};

template <> struct RunStateCodec<SystemVerilogUvmDomainHandle> {
    using is_codec = void;
    static void io(RunStateArchive& archive, SystemVerilogUvmDomainHandle& handle)
    {
        SystemVerilogUvmRunStateAccess::slot_handle(archive, handle, false);
    }
};

template <> struct RunStateCodec<SystemVerilogUvmPhaseHandle> {
    using is_codec = void;
    static void io(RunStateArchive& archive, SystemVerilogUvmPhaseHandle& handle)
    {
        SystemVerilogUvmRunStateAccess::slot_handle(archive, handle, false);
    }
};

template <> struct RunStateCodec<SystemVerilogUvmPhaseProcessHandle> {
    using is_codec = void;
    static void io(
        RunStateArchive& archive, SystemVerilogUvmPhaseProcessHandle& handle)
    {
        SystemVerilogUvmRunStateAccess::slot_handle(archive, handle, false);
    }
};

/// The phases are an array, written element by element.
template <> struct RunStateCodec<SystemVerilogUvmStandardSchedule> {
    using is_codec = void;
    static void io(
        RunStateArchive& archive, SystemVerilogUvmStandardSchedule& schedule)
    {
        archive.io(schedule.common_domain);
        archive.io(schedule.runtime_domain);
        for (auto& phase : schedule.phases) {
            archive.io(phase);
        }
    }
};

template <> struct RunStateCodec<SystemVerilogUvmObjectionSourceHandle> {
    using is_codec = void;
    static void io(
        RunStateArchive& archive, SystemVerilogUvmObjectionSourceHandle& handle)
    {
        SystemVerilogUvmRunStateAccess::source_handle(archive, handle);
    }
};

} // namespace fsim::runtime
