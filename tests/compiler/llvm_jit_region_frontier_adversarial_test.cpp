// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_adversarial_test.hpp"
#include "llvm_jit_region_frontier_test_support.hpp"
#include "llvm/region_frontier_codegen_v2.hpp"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#error "Region frontier prefix guard test requires Windows or POSIX virtual memory"
#endif

namespace fsim::compiler::test {
namespace {

using namespace runtime::simir;
using namespace runtime::simir::scratch;

constexpr std::uint64_t full_static_trigger_mask = UINT64_C(1) << 63U;
constexpr std::uint64_t first_task_stable_order = 17U;
constexpr std::uint64_t first_task_sequence = 3U;
constexpr std::uint32_t declared_task_capacity = 32U;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

class GuardedFramePrefix final {
    struct Prefix;

public:
    GuardedFramePrefix(const std::uint32_t abi_version,
        const std::uint32_t struct_size)
    {
#if defined(_WIN32)
        SYSTEM_INFO info { };
        GetSystemInfo(&info);
        page_size_ = static_cast<std::size_t>(info.dwPageSize);
        allocation_ = VirtualAlloc(nullptr, page_size_ * 2U,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (allocation_ == nullptr) {
            throw std::runtime_error { "could not allocate frame prefix pages" };
        }
        DWORD prior_protection { };
        auto* const guard_page = static_cast<std::byte*>(allocation_)
            + page_size_;
        if (VirtualProtect(guard_page, page_size_, PAGE_NOACCESS,
                &prior_protection) == 0) {
            VirtualFree(allocation_, 0U, MEM_RELEASE);
            allocation_ = nullptr;
            throw std::runtime_error { "could not protect frame guard page" };
        }
#else
        const auto page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) {
            throw std::runtime_error { "could not query frame page size" };
        }
        page_size_ = static_cast<std::size_t>(page_size);
        const auto zero_device = open("/dev/zero", O_RDWR);
        if (zero_device < 0) {
            throw std::runtime_error { "could not open zero device for frame pages" };
        }
        allocation_ = mmap(nullptr, page_size_ * 2U,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE, zero_device, 0);
        close(zero_device);
        if (allocation_ == MAP_FAILED) {
            allocation_ = nullptr;
            throw std::runtime_error { "could not allocate frame prefix pages" };
        }
        auto* const guard_page = static_cast<std::byte*>(allocation_)
            + page_size_;
        if (mprotect(guard_page, page_size_, PROT_NONE) != 0) {
            munmap(allocation_, page_size_ * 2U);
            allocation_ = nullptr;
            throw std::runtime_error { "could not protect frame guard page" };
        }
#endif
        auto* const prefix_address = static_cast<std::byte*>(allocation_)
            + page_size_ - sizeof(Prefix);
        prefix_ = ::new (prefix_address) Prefix { abi_version, struct_size };
#if defined(_WIN32)
        DWORD prefix_prior_protection { };
        if (VirtualProtect(allocation_, page_size_, PAGE_READONLY,
                &prefix_prior_protection) == 0) {
            VirtualFree(allocation_, 0U, MEM_RELEASE);
            allocation_ = nullptr;
            prefix_ = nullptr;
            throw std::runtime_error { "could not protect frame prefix page" };
        }
#else
        if (mprotect(allocation_, page_size_, PROT_READ) != 0) {
            munmap(allocation_, page_size_ * 2U);
            allocation_ = nullptr;
            prefix_ = nullptr;
            throw std::runtime_error { "could not protect frame prefix page" };
        }
#endif
    }

    ~GuardedFramePrefix()
    {
        release();
    }

    GuardedFramePrefix(const GuardedFramePrefix&) = delete;
    GuardedFramePrefix& operator=(const GuardedFramePrefix&) = delete;

    [[nodiscard]] Prefix* prefix() const noexcept
    {
        return prefix_;
    }

    [[nodiscard]] RegionFrontierFrameV2* frame() const noexcept
    {
        return reinterpret_cast<RegionFrontierFrameV2*>(prefix_);
    }

private:
    struct Prefix {
        std::uint32_t abi_version { };
        std::uint32_t struct_size { };
    };
    static_assert(sizeof(Prefix) == 2U * sizeof(std::uint32_t));

    void release() noexcept
    {
        if (allocation_ == nullptr) {
            return;
        }
#if defined(_WIN32)
        VirtualFree(allocation_, 0U, MEM_RELEASE);
#else
        munmap(allocation_, page_size_ * 2U);
#endif
        allocation_ = nullptr;
        prefix_ = nullptr;
    }

    void* allocation_ { };
    std::size_t page_size_ { };
    Prefix* prefix_ { };
};

void expect_guarded_prefix_decline(
    const RegionFrontierStepEntryV2 entry,
    const std::uint32_t abi_version,
    const std::uint32_t struct_size,
    const std::string_view case_name)
{
    GuardedFramePrefix storage { abi_version, struct_size };
    const auto status = entry(storage.frame());
    require(status == RegionFrontierStatusV2::decline_before_mutation,
        case_name);
    require(storage.prefix()->abi_version == abi_version
            && storage.prefix()->struct_size == struct_size,
        "the emitted entry leaves the readable ABI prefix unchanged");
}

[[nodiscard]] std::size_t words_for(const std::uint32_t width) noexcept
{
    return (static_cast<std::size_t>(width) + 63U) / 64U;
}

[[nodiscard]] bool same_key(const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_slot(const RegionFrontierSlotV2& left,
    const RegionFrontierSlotV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool same_cut(const RegionFrontierCutV2& left,
    const RegionFrontierCutV2& right) noexcept
{
    return left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && same_key(left.next_key, right.next_key)
        && left.kind == right.kind
        && std::equal(std::begin(left.reserved), std::end(left.reserved),
            std::begin(right.reserved));
}

[[nodiscard]] bool same_frame(const RegionFrontierFrameV2& left,
    const RegionFrontierFrameV2& right) noexcept
{
    return left.abi_version == right.abi_version
        && left.struct_size == right.struct_size
        && left.value_plane_contract == right.value_plane_contract
        && left.runtime_generation == right.runtime_generation
        && left.bound_runtime_generation == right.bound_runtime_generation
        && left.certificate_generation == right.certificate_generation
        && left.component_generation == right.component_generation
        && left.scheduler_frontier_generation
            == right.scheduler_frontier_generation
        && left.member_count == right.member_count
        && left.scheduler_task_count == right.scheduler_task_count
        && left.scheduler_task_cursor == right.scheduler_task_cursor
        && left.scheduler_task_capacity == right.scheduler_task_capacity
        && left.readiness_word_count == right.readiness_word_count
        && left.signal_slot_count == right.signal_slot_count
        && left.metadata_count == right.metadata_count
        && left.fanout_edge_count == right.fanout_edge_count
        && left.committed_signal_capacity == right.committed_signal_capacity
        && left.committed_signal_count == right.committed_signal_count
        && left.pending_write_capacity == right.pending_write_capacity
        && left.pending_write_count == right.pending_write_count
        && left.staged_event_capacity == right.staged_event_capacity
        && left.staged_event_count == right.staged_event_count
        && left.current_member == right.current_member
        && left.current_pending_write == right.current_pending_write
        && left.current_commit_changed == right.current_commit_changed
        && left.saved_body_pc == right.saved_body_pc
        && left.ready_words == right.ready_words
        && left.members == right.members
        && left.scheduler_tasks == right.scheduler_tasks
        && left.planes == right.planes
        && left.metadata == right.metadata
        && left.fanout_edges == right.fanout_edges
        && left.port_planes == right.port_planes
        && left.pending_writes == right.pending_writes
        && left.staged_events == right.staged_events
        && left.committed_signals == right.committed_signals
        && left.native_frontier_member_dispatches
            == right.native_frontier_member_dispatches
        && left.stop_requested == right.stop_requested
        && same_slot(left.slot, right.slot)
        && same_cut(left.cut, right.cut);
}

[[nodiscard]] bool same_member(const RegionFrontierMemberV2& left,
    const RegionFrontierMemberV2& right) noexcept
{
    return left.process_id == right.process_id
        && left.flags == right.flags
        && left.static_trigger_mask == right.static_trigger_mask
        && same_key(left.queued_key, right.queued_key)
        && same_key(left.activation_origin, right.activation_origin)
        && same_key(left.pending_activation_origin,
            right.pending_activation_origin);
}

[[nodiscard]] bool same_task(const RegionFrontierSchedulerTaskV2& left,
    const RegionFrontierSchedulerTaskV2& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.payload == right.payload;
}

[[nodiscard]] bool same_plane(const RegionFrontierPlaneV2& left,
    const RegionFrontierPlaneV2& right) noexcept
{
    return left.signal_id == right.signal_id
        && left.owner_process_id == right.owner_process_id
        && left.value_kind == right.value_kind
        && left.width == right.width
        && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && left.flags == right.flags
        && left.metadata_index == right.metadata_index
        && std::equal(std::begin(left.boundary_planes),
            std::end(left.boundary_planes),
            std::begin(right.boundary_planes))
        && std::equal(std::begin(left.current_planes),
            std::end(left.current_planes),
            std::begin(right.current_planes))
        && std::equal(std::begin(left.previous_planes),
            std::end(left.previous_planes),
            std::begin(right.previous_planes))
        && std::equal(std::begin(left.stored_planes),
            std::end(left.stored_planes),
            std::begin(right.stored_planes))
        && std::equal(std::begin(left.owner_planes),
            std::end(left.owner_planes),
            std::begin(right.owner_planes));
}

[[nodiscard]] bool same_metadata(const RegionFrontierSignalMetadataV2& left,
    const RegionFrontierSignalMetadataV2& right) noexcept
{
    return left.event_time == right.event_time
        && left.event_delta == right.event_delta
        && left.transaction_time == right.transaction_time
        && left.transaction_delta == right.transaction_delta
        && left.value_revision == right.value_revision
        && left.systemverilog_round == right.systemverilog_round
        && left.event_process_domain == right.event_process_domain
        && left.event_phase == right.event_phase
        && left.event_valid == right.event_valid
        && left.transaction_valid == right.transaction_valid
        && std::equal(std::begin(left.reserved), std::end(left.reserved),
            std::begin(right.reserved));
}

[[nodiscard]] bool same_write(const RegionFrontierPendingWriteV2& left,
    const RegionFrontierPendingWriteV2& right) noexcept
{
    return left.member_index == right.member_index
        && left.signal_slot == right.signal_slot
        && left.source_instruction == right.source_instruction
        && left.update_kind == right.update_kind
        && left.flags == right.flags
        && left.reserved == right.reserved
        && same_key(left.commit_key, right.commit_key)
        && same_key(left.origin, right.origin)
        && left.value_kind == right.value_kind
        && left.width == right.width
        && left.word_count == right.word_count
        && left.plane_count == right.plane_count
        && std::equal(std::begin(left.value_planes),
            std::end(left.value_planes),
            std::begin(right.value_planes));
}

[[nodiscard]] bool same_event(const RegionFrontierStagedEventV2& left,
    const RegionFrontierStagedEventV2& right) noexcept
{
    return left.kind == right.kind
        && left.descriptor_index == right.descriptor_index
        && left.stable_order == right.stable_order
        && same_key(left.origin, right.origin);
}

[[nodiscard]] bool same_edge(const RegionFrontierFanoutEdgeV2& left,
    const RegionFrontierFanoutEdgeV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.member_index == right.member_index
        && left.trigger_mask == right.trigger_mask;
}

[[nodiscard]] bool same_committed_signal(
    const RegionFrontierCommittedSignalV2& left,
    const RegionFrontierCommittedSignalV2& right) noexcept
{
    return left.signal_slot == right.signal_slot
        && left.changed == right.changed
        && left.state_changed == right.state_changed;
}

template<typename T, typename Predicate>
[[nodiscard]] bool same_records(const std::vector<T>& left,
    const std::vector<T>& right, Predicate predicate)
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), predicate);
}

struct PlaneStorage {
    std::array<std::vector<std::uint64_t>, 10U> roles;

    bool operator==(const PlaneStorage&) const = default;
};

struct WriteStorage {
    std::vector<std::uint64_t> aval;
    std::vector<std::uint64_t> bval;

    bool operator==(const WriteStorage&) const = default;
};

struct FrameSnapshot {
    RegionFrontierFrameV2 frame;
    std::vector<std::uint64_t> readiness;
    std::vector<RegionFrontierMemberV2> members;
    std::vector<RegionFrontierSchedulerTaskV2> tasks;
    std::vector<RegionFrontierPlaneV2> planes;
    std::vector<RegionFrontierSignalMetadataV2> metadata;
    std::vector<RegionFrontierFanoutEdgeV2> fanout;
    std::vector<const RegionFrontierPlaneV2*> port_planes;
    std::vector<RegionFrontierPendingWriteV2> writes;
    std::vector<RegionFrontierStagedEventV2> events;
    std::vector<RegionFrontierCommittedSignalV2> committed;
    std::vector<PlaneStorage> plane_storage;
    std::vector<WriteStorage> write_storage;
    std::vector<std::uint64_t> pending_arena;
    std::uint64_t dispatch_count { };
    std::uint32_t stop_value { };
};

class FrameStorage final {
public:
    FrameStorage(const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout,
        const std::uint64_t runtime_generation)
        : kernel_ { kernel }
        , layout_ { layout }
        , readiness_ (static_cast<std::size_t>(layout.readiness_word_count) + 1U)
        , members_ (static_cast<std::size_t>(layout.member_count) + 1U)
        , tasks_ (static_cast<std::size_t>(declared_task_capacity) * 2U)
        , planes_ (static_cast<std::size_t>(layout.signal_slot_count) + 1U)
        , plane_storage_ (static_cast<std::size_t>(layout.signal_slot_count) + 1U)
        , metadata_ (static_cast<std::size_t>(layout.metadata_count) + 1U)
        , fanout_ (static_cast<std::size_t>(layout.fanout_edge_count) + 1U)
        , port_planes_ (static_cast<std::size_t>(layout.signal_slot_count) + 1U)
        , writes_ (static_cast<std::size_t>(layout.pending_write_capacity) + 1U)
        , write_storage_ (
              static_cast<std::size_t>(layout.pending_write_capacity) + 1U)
        , events_ (static_cast<std::size_t>(layout.staged_event_capacity) + 1U)
        , committed_ (
              static_cast<std::size_t>(layout.committed_signal_capacity) + 2U)
    {
        require(entry_shape_is_usable(),
            "the generated layout supplies preallocated descriptor storage");
        bind_signal_planes();
        bind_fanout();
        bind_write_sites();
        initialize_frame(runtime_generation);
    }

    [[nodiscard]] RegionFrontierFrameV2& frame() noexcept
    {
        return frame_;
    }

    void set_first_scheduler_task_payload(const std::uint64_t payload)
    {
        tasks_.at(0U).payload = payload;
    }

    void set_scheduler_task(const std::size_t index,
        const RegionFrontierSchedulerTaskV2& task)
    {
        tasks_.at(index) = task;
    }

    [[nodiscard]] std::vector<RegionFrontierMemberV2>& members() noexcept
    {
        return members_;
    }

    [[nodiscard]] std::vector<RegionFrontierPlaneV2>& planes() noexcept
    {
        return planes_;
    }

    [[nodiscard]] std::vector<RegionFrontierFanoutEdgeV2>& fanout() noexcept
    {
        return fanout_;
    }

    [[nodiscard]] std::vector<const RegionFrontierPlaneV2*>& port_planes() noexcept
    {
        return port_planes_;
    }

    [[nodiscard]] std::vector<RegionFrontierPendingWriteV2>& writes() noexcept
    {
        return writes_;
    }

    void bind_pending_ranges(const std::vector<std::size_t>& word_offsets,
        const std::size_t arena_word_count)
    {
        std::size_t expected_range_count = 0U;
        for (std::size_t site_index = 0U;
             site_index < layout_.write_site_count; ++site_index) {
            expected_range_count += layout_.write_sites[site_index].plane_count;
        }
        require(word_offsets.size() == expected_range_count,
            "the pending arena supplies one offset for every value plane");
        pending_arena_.assign(arena_word_count, 0U);

        std::size_t range_index = 0U;
        for (std::size_t site_index = 0U;
             site_index < layout_.write_site_count; ++site_index) {
            const auto& site = layout_.write_sites[site_index];
            auto& write = writes_.at(site.pending_slot);
            for (std::size_t value_plane = 0U;
                 value_plane < site.plane_count; ++value_plane) {
                const auto offset_words = word_offsets.at(range_index++);
                require(offset_words <= pending_arena_.size()
                        && site.word_count
                            <= pending_arena_.size() - offset_words,
                    "each pending arena range remains within owned storage");
                write.value_planes[value_plane]
                    = pending_arena_.data() + offset_words;
            }
        }
    }

    void bind_boundary_planes_to_pending_arena(
        const std::size_t first_word)
    {
        const auto boundary = boundary_plane_index();
        const auto& descriptor = layout_.signals[boundary];
        require(descriptor.plane_count == kRegionFrontierLogic4PlaneCountV2,
            "the pending-gap boundary fixture uses two Logic4 planes");
        require(first_word <= pending_arena_.size()
                && descriptor.word_count
                    <= pending_arena_.size() - first_word,
            "the boundary test range fits in pending arena storage");
        auto& plane = planes_.at(boundary);
        for (std::size_t value_plane = 0U;
             value_plane < descriptor.plane_count; ++value_plane) {
            const auto offset_words = first_word
                + value_plane * descriptor.word_count;
            require(offset_words <= pending_arena_.size()
                    && descriptor.word_count
                        <= pending_arena_.size() - offset_words,
                "each boundary plane fits in the padded pending gap");
            const auto source_role = value_plane == 0U
                ? boundary_aval_role : boundary_bval_role;
            std::copy_n(plane_storage_[boundary].roles[source_role].begin(),
                descriptor.word_count,
                pending_arena_.begin()
                    + static_cast<std::ptrdiff_t>(offset_words));
            plane.boundary_planes[value_plane]
                = pending_arena_.data() + offset_words;
        }
    }

    [[nodiscard]] std::vector<RegionFrontierCommittedSignalV2>& committed() noexcept
    {
        return committed_;
    }

    [[nodiscard]] std::size_t root_member_index() const
    {
        const auto root_process = kernel_.members.front().process;
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            if (layout_.members[index].process_id == root_process) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture root maps to one compiled local member"
        };
    }

    [[nodiscard]] std::size_t boundary_plane_index() const
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            if ((layout_.signals[index].flags
                    & RegionFrontierPlaneFlagsV2::read_only_boundary_port) != 0U) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture includes a read-only boundary plane"
        };
    }

    [[nodiscard]] std::size_t internal_plane_index() const
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            if ((layout_.signals[index].flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture includes an internal mutable plane"
        };
    }

    [[nodiscard]] std::size_t first_internal_write_site() const
    {
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            if (layout_.write_sites[index].event_kind
                == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::internal_commit)) {
                return index;
            }
        }
        throw std::runtime_error {
            "the production fixture includes a unique internal-commit write site"
        };
    }

    [[nodiscard]] std::size_t first_internal_write_pending_slot() const
    {
        return layout_.write_sites[first_internal_write_site()].pending_slot;
    }

    [[nodiscard]] std::uint64_t dispatch_count() const noexcept
    {
        return dispatch_count_;
    }

    void prepare_activation_task()
    {
        reset_scheduler_member_state();
        const auto root = root_member_index();
        const auto key = make_key(first_task_stable_order,
            first_task_sequence);
        readiness_.at(root / 64U) |= UINT64_C(1) << (root % 64U);
        auto& member = members_.at(root);
        member.flags = RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid;
        member.queued_key = key;
        member.activation_origin = key;
        member.pending_activation_origin = key;
        tasks_[0U] = RegionFrontierSchedulerTaskV2 {
            key.stable_order,
            key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::member_activation,
                static_cast<std::uint64_t>(root)),
        };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        frame_.pending_write_count = 0U;
        frame_.staged_event_count = 0U;
        set_closed_prefix();
    }

    void prepare_internal_commit_task()
    {
        reset_scheduler_member_state();
        const auto& site = layout_.write_sites[first_internal_write_site()];
        require(site.pending_slot < layout_.pending_write_capacity,
            "the internal write site's reservation slot is in bounds");
        auto& write = writes_.at(site.pending_slot);
        const auto key = make_key(first_task_stable_order + 1U,
            first_task_sequence + 1U);
        write.flags = RegionFrontierPendingWriteFlagsV2::pending_active
            | RegionFrontierPendingWriteFlagsV2::pending_value_ready
            | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
            | RegionFrontierPendingWriteFlagsV2::pending_internal_target;
        write.commit_key = key;
        write.origin = make_key(first_task_stable_order,
            first_task_sequence);
        members_.at(site.member_index).activation_origin = write.origin;
        write.value_planes[0U][0U] = UINT64_C(1);
        write.value_planes[1U][0U] = UINT64_C(0);
        frame_.pending_write_count = 1U;
        tasks_[0U] = RegionFrontierSchedulerTaskV2 {
            key.stable_order,
            key.sequence,
            encode_region_frontier_payload_v2(
                RegionFrontierEventKindV2::internal_commit,
                site.pending_slot),
        };
        frame_.scheduler_task_count = 1U;
        frame_.scheduler_task_cursor = 0U;
        frame_.staged_event_count = 0U;
        set_closed_prefix();
    }

    [[nodiscard]] FrameSnapshot snapshot() const
    {
        return FrameSnapshot {
            frame_, readiness_, members_, tasks_, planes_, metadata_,
            fanout_, port_planes_, writes_, events_, committed_,
            plane_storage_, write_storage_, pending_arena_, dispatch_count_,
            stop_value_,
        };
    }

    void restore(const FrameSnapshot& snapshot)
    {
        const auto copy_values = [](auto& destination, const auto& source) {
            require(destination.size() == source.size(),
                "snapshot restore retains every preallocated buffer extent");
            std::copy(source.begin(), source.end(), destination.begin());
        };
        frame_ = snapshot.frame;
        copy_values(readiness_, snapshot.readiness);
        copy_values(members_, snapshot.members);
        copy_values(tasks_, snapshot.tasks);
        copy_values(planes_, snapshot.planes);
        copy_values(metadata_, snapshot.metadata);
        copy_values(fanout_, snapshot.fanout);
        copy_values(port_planes_, snapshot.port_planes);
        copy_values(writes_, snapshot.writes);
        copy_values(events_, snapshot.events);
        copy_values(committed_, snapshot.committed);
        require(plane_storage_.size() == snapshot.plane_storage.size(),
            "snapshot restore retains every plane allocation");
        for (std::size_t plane = 0U; plane < plane_storage_.size(); ++plane) {
            for (std::size_t role = 0U;
                 role < plane_storage_[plane].roles.size(); ++role) {
                copy_values(plane_storage_[plane].roles[role],
                    snapshot.plane_storage[plane].roles[role]);
            }
        }
        require(write_storage_.size() == snapshot.write_storage.size(),
            "snapshot restore retains every pending-value allocation");
        for (std::size_t write = 0U; write < write_storage_.size(); ++write) {
            copy_values(write_storage_[write].aval,
                snapshot.write_storage[write].aval);
            copy_values(write_storage_[write].bval,
                snapshot.write_storage[write].bval);
        }
        copy_values(pending_arena_, snapshot.pending_arena);
        dispatch_count_ = snapshot.dispatch_count;
        stop_value_ = snapshot.stop_value;
    }

    void require_unchanged(const FrameSnapshot& before,
        const std::string_view message) const
    {
        require_matches(before, message);
    }

    void require_matches(const FrameSnapshot& expected,
        const std::string_view message) const
    {
        require(same_frame(frame_, expected.frame), message);
        require(readiness_ == expected.readiness, message);
        require(same_records(members_, expected.members, same_member), message);
        require(same_records(tasks_, expected.tasks, same_task), message);
        require(same_records(planes_, expected.planes, same_plane), message);
        require(same_records(metadata_, expected.metadata, same_metadata), message);
        require(same_records(fanout_, expected.fanout, same_edge), message);
        require(port_planes_ == expected.port_planes, message);
        require(same_records(writes_, expected.writes, same_write), message);
        require(same_records(events_, expected.events, same_event), message);
        require(same_records(committed_, expected.committed,
                    same_committed_signal), message);
        require(plane_storage_ == expected.plane_storage, message);
        require(write_storage_ == expected.write_storage, message);
        require(pending_arena_ == expected.pending_arena, message);
        require(dispatch_count_ == expected.dispatch_count
                && stop_value_ == expected.stop_value, message);
    }

    [[nodiscard]] std::vector<std::uint64_t>& role_words(
        const std::size_t plane, const std::size_t role) noexcept
    {
        return plane_storage_[plane].roles[role];
    }

private:
    static constexpr std::size_t boundary_aval_role = 0U;
    static constexpr std::size_t boundary_bval_role = 1U;
    static constexpr std::size_t current_aval_role = 2U;
    static constexpr std::size_t current_bval_role = 3U;
    static constexpr std::size_t previous_aval_role = 4U;
    static constexpr std::size_t previous_bval_role = 5U;
    static constexpr std::size_t stored_aval_role = 6U;
    static constexpr std::size_t stored_bval_role = 7U;
    static constexpr std::size_t owner_aval_role = 8U;
    static constexpr std::size_t owner_bval_role = 9U;

    [[nodiscard]] bool entry_shape_is_usable() const noexcept
    {
        const bool generic = layout_.execution_mode
            == RegionFrontierExecutionModeV2::generic_deferred_update;
        return layout_.abi_version == kRegionFrontierAbiVersionV2
            && layout_.struct_size == sizeof(RegionFrontierLayoutV2)
            && region_frontier_layout_header_valid_v2(layout_)
            && layout_.member_count == kernel_.members.size()
            && layout_.member_count >= 2U
            && layout_.members != nullptr
            && layout_.readiness_word_count
                == (layout_.member_count + 63U) / 64U
            && layout_.signal_slot_count != 0U
            && layout_.signals != nullptr
            && (generic || layout_.metadata_count != 0U)
            && (layout_.fanout_edge_count == 0U
                || layout_.fanout_edges != nullptr)
            && layout_.write_site_count != 0U
            && layout_.write_sites != nullptr
            && layout_.pending_write_capacity
                >= layout_.write_site_count
            && layout_.staged_event_capacity != 0U
            && (generic || layout_.committed_signal_capacity
                == layout_.pending_write_capacity)
            && (generic || layout_.max_member_write_counts != nullptr)
            && (generic || layout_.max_member_staged_event_counts != nullptr);
    }

    void bind_signal_planes()
    {
        for (std::size_t index = 0U; index < layout_.signal_slot_count; ++index) {
            const auto& descriptor = layout_.signals[index];
            const auto word_count = std::max<std::size_t>(
                descriptor.word_count, 4U);
            auto& storage = plane_storage_[index];
            for (std::size_t role = 0U; role < storage.roles.size(); ++role) {
                storage.roles[role].assign(word_count, 0U);
                for (std::size_t word = descriptor.word_count;
                     word < word_count; ++word) {
                    storage.roles[role][word]
                        = UINT64_C(0x5a5a000000000000)
                        | (static_cast<std::uint64_t>(index) << 8U)
                        | static_cast<std::uint64_t>(role);
                }
            }

            auto& plane = planes_[index];
            plane.signal_id = descriptor.signal_id;
            plane.owner_process_id = descriptor.owner_process_id;
            plane.value_kind = descriptor.value_kind;
            plane.plane_count = descriptor.plane_count;
            plane.width = descriptor.width;
            plane.word_count = descriptor.word_count;
            plane.flags = descriptor.flags;
            plane.metadata_index = descriptor.metadata_index;
            if ((descriptor.flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U) {
                plane.current_planes[0]
                    = storage.roles[current_aval_role].data();
                plane.current_planes[1]
                    = storage.roles[current_bval_role].data();
                plane.previous_planes[0]
                    = storage.roles[previous_aval_role].data();
                plane.previous_planes[1]
                    = storage.roles[previous_bval_role].data();
                plane.stored_planes[0]
                    = storage.roles[stored_aval_role].data();
                plane.stored_planes[1]
                    = storage.roles[stored_bval_role].data();
                plane.owner_planes[0] = storage.roles[owner_aval_role].data();
                plane.owner_planes[1] = storage.roles[owner_bval_role].data();
            } else {
                require((descriptor.flags
                            & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                        != 0U,
                    "the immutable layout identifies boundary versus internal planes");
                plane.boundary_planes[0]
                    = storage.roles[boundary_aval_role].data();
                plane.boundary_planes[1]
                    = storage.roles[boundary_bval_role].data();
                storage.roles[boundary_aval_role][0U] = UINT64_C(1);
            }
            require(region_frontier_plane_bindings_valid_v2(plane),
                "fixture backing satisfies the typed plane contract");
            port_planes_[index] = &plane;
        }
    }

    void bind_fanout()
    {
        require(kernel_.program.static_trigger_regions.empty(),
            "the fixture uses the runtime full-mask grouped-fanout path");
        for (std::size_t index = 0U; index < layout_.fanout_edge_count; ++index) {
            const auto& topology = layout_.fanout_edges[index];
            require(topology.signal_slot < layout_.signal_slot_count
                    && topology.member_index < layout_.member_count,
                "immutable fanout topology points to valid local descriptors");
            const auto process_id
                = layout_.members[topology.member_index].process_id;
            const auto member = std::ranges::find_if(kernel_.members,
                [process_id](const auto& candidate) {
                    return candidate.process == process_id;
                });
            require(member != kernel_.members.end(),
                "each topology edge maps to one original kernel member");
            const auto signal_id = layout_.signals[topology.signal_slot].signal_id;
            const auto sensitivity_count = std::ranges::count_if(
                member->sensitivities, [signal_id](const auto& sensitivity) {
                    return sensitivity.signal == signal_id;
                });
            require(sensitivity_count == 1,
                "each fixture topology edge maps to one whole-signal sensitivity");
            // SV grouped fanout uses the full static trigger mask. Generic
            // deferred Update edges carry no static trigger mask.
            const auto trigger_mask = layout_.execution_mode
                    == RegionFrontierExecutionModeV2::generic_deferred_update
                ? UINT64_C(0)
                : full_static_trigger_mask;
            fanout_[index] = RegionFrontierFanoutEdgeV2 {
                topology.signal_slot,
                topology.member_index,
                trigger_mask,
            };
            members_[topology.member_index].static_trigger_mask
                |= trigger_mask;
        }
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            members_[index].process_id = layout_.members[index].process_id;
            members_[index].flags
                = RegionFrontierMemberFlagsV2::waiting_on_static;
        }
    }

    void bind_write_sites()
    {
        for (std::size_t index = 0U; index < layout_.write_site_count; ++index) {
            const auto& site = layout_.write_sites[index];
            require(site.signal_slot < layout_.signal_slot_count,
                "write site signal slot is in the immutable layout");
            const auto& signal = layout_.signals[site.signal_slot];
            require(site.pending_slot < layout_.pending_write_capacity
                    && site.member_index < layout_.member_count
                    && site.signal_slot < layout_.signal_slot_count
                    && site.value_kind == RegionFrontierValueKindV2::logic4
                    && site.plane_count
                        == kRegionFrontierLogic4PlaneCountV2
                    && site.width != 0U
                    && site.word_count == words_for(site.width)
                    && site.value_kind == signal.value_kind
                    && site.plane_count == signal.plane_count,
                "every immutable write site has a safe frame-backed slot");
            const auto backing_words = std::max<std::size_t>(
                static_cast<std::size_t>(site.word_count) + 1U, 4U);
            auto& backing = write_storage_[site.pending_slot];
            backing.aval.assign(backing_words, 0U);
            backing.bval.assign(backing_words, 0U);
            for (std::size_t word = site.word_count;
                 word < backing_words; ++word) {
                backing.aval[word] = UINT64_C(0x123456789abcdef0);
                backing.bval[word] = UINT64_C(0x0fedcba987654321);
            }
            auto& write = writes_[site.pending_slot];
            write.member_index = site.member_index;
            write.signal_slot = site.signal_slot;
            write.source_instruction = site.source_instruction;
            write.update_kind = site.update_kind;
            write.value_planes[0U] = backing.aval.data();
            write.value_planes[1U] = backing.bval.data();
            write.value_kind = site.value_kind;
            write.plane_count = site.plane_count;
            write.width = site.width;
            write.word_count = site.word_count;
        }
    }

    void initialize_frame(const std::uint64_t runtime_generation)
    {
        frame_.abi_version = kRegionFrontierAbiVersionV2;
        frame_.struct_size = sizeof(RegionFrontierFrameV2);
        frame_.value_plane_contract = kRegionFrontierValuePlaneContractV2;
        frame_.runtime_generation = runtime_generation;
        frame_.bound_runtime_generation = runtime_generation;
        frame_.certificate_generation = layout_.certificate_generation;
        frame_.component_generation = layout_.component_generation;
        frame_.scheduler_frontier_generation = 1U;
        frame_.member_count = layout_.member_count;
        frame_.scheduler_task_capacity = declared_task_capacity;
        frame_.readiness_word_count = layout_.readiness_word_count;
        frame_.signal_slot_count = layout_.signal_slot_count;
        frame_.metadata_count = layout_.metadata_count;
        frame_.fanout_edge_count = layout_.fanout_edge_count;
        frame_.committed_signal_capacity
            = layout_.committed_signal_capacity;
        frame_.pending_write_capacity = layout_.pending_write_capacity;
        frame_.staged_event_capacity = layout_.staged_event_capacity;
        frame_.current_member = UINT32_MAX;
        frame_.current_pending_write = UINT32_MAX;
        frame_.ready_words = readiness_.data();
        frame_.members = members_.data();
        frame_.scheduler_tasks = tasks_.data();
        frame_.planes = planes_.data();
        frame_.metadata = metadata_.data();
        frame_.fanout_edges = fanout_.data();
        frame_.port_planes = port_planes_.data();
        frame_.pending_writes = writes_.data();
        frame_.staged_events = events_.data();
        frame_.committed_signals = committed_.data();
        frame_.native_frontier_member_dispatches = &dispatch_count_;
        frame_.stop_requested = &stop_value_;
        const auto process_domain = layout_.execution_mode
                == RegionFrontierExecutionModeV2::generic_deferred_update
            ? kRegionFrontierGenericDomainV2
            : kRegionFrontierSystemVerilogDomainV2;
        frame_.slot = RegionFrontierSlotV2 {
            23U, 0U, 0U, process_domain, 0U,
        };
        set_closed_prefix();
    }

    void reset_scheduler_member_state()
    {
        std::fill(readiness_.begin(), readiness_.end(), 0U);
        std::fill(tasks_.begin(), tasks_.end(), RegionFrontierSchedulerTaskV2 { });
        for (std::size_t index = 0U; index < layout_.member_count; ++index) {
            auto& member = members_[index];
            member.flags = RegionFrontierMemberFlagsV2::waiting_on_static;
            member.queued_key = { };
            member.activation_origin = { };
            member.pending_activation_origin = { };
        }
        for (std::size_t index = 0U; index < layout_.pending_write_capacity; ++index) {
            writes_[index].flags = 0U;
            writes_[index].commit_key = { };
            writes_[index].origin = { };
        }
        frame_.current_member = UINT32_MAX;
        frame_.current_pending_write = UINT32_MAX;
        frame_.scheduler_frontier_generation = 1U;
        frame_.committed_signal_count = 0U;
        frame_.pending_write_count = 0U;
        frame_.staged_event_count = 0U;
        frame_.current_commit_changed = 0U;
        frame_.saved_body_pc = 0U;
    }

    [[nodiscard]] RegionFrontierKeyV2 make_key(
        const std::uint64_t stable_order,
        const std::uint64_t sequence) const noexcept
    {
        return RegionFrontierKeyV2 {
            frame_.slot.time,
            frame_.slot.delta,
            frame_.slot.systemverilog_round,
            stable_order,
            sequence,
            frame_.slot.process_domain,
            frame_.slot.phase,
        };
    }

    void set_closed_prefix() noexcept
    {
        frame_.cut.scheduler_frontier_generation
            = frame_.scheduler_frontier_generation;
        frame_.cut.next_key = { };
        frame_.cut.kind = RegionFrontierCutKindV2::closed_prefix;
        std::fill(std::begin(frame_.cut.reserved), std::end(frame_.cut.reserved), 0U);
    }

    const RegionConeActivationKernel& kernel_;
    const RegionFrontierLayoutV2& layout_;
    RegionFrontierFrameV2 frame_;
    std::vector<std::uint64_t> readiness_;
    std::vector<RegionFrontierMemberV2> members_;
    std::vector<RegionFrontierSchedulerTaskV2> tasks_;
    std::vector<RegionFrontierPlaneV2> planes_;
    std::vector<PlaneStorage> plane_storage_;
    std::vector<RegionFrontierSignalMetadataV2> metadata_;
    std::vector<RegionFrontierFanoutEdgeV2> fanout_;
    std::vector<const RegionFrontierPlaneV2*> port_planes_;
    std::vector<RegionFrontierPendingWriteV2> writes_;
    std::vector<WriteStorage> write_storage_;
    std::vector<RegionFrontierStagedEventV2> events_;
    std::vector<RegionFrontierCommittedSignalV2> committed_;
    std::vector<std::uint64_t> pending_arena_;
    std::uint64_t dispatch_count_ { };
    std::uint32_t stop_value_ { };
};

void expect_status_and_no_mutation(const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    const bool internal_commit,
    const RegionFrontierStatusV2 expected,
    const std::string_view case_name,
    const std::function<void(FrameStorage&)>& mutate)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    if (internal_commit) {
        storage.prepare_internal_commit_task();
    } else {
        storage.prepare_activation_task();
    }
    mutate(storage);
    const auto before = storage.snapshot();
    const auto status = entry(&storage.frame());
    require(status == expected, case_name);
    storage.require_unchanged(before, case_name);
}

void run_adversarial_cases(const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    const auto decline = RegionFrontierStatusV2::decline_before_mutation;
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "a V1 frame ABI tag declines before any stores",
        [](FrameStorage& storage) {
            storage.frame().abi_version = kRegionFrontierAbiVersionV2 - 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "an unknown frame ABI version declines without mutation",
        [](FrameStorage& storage) {
            storage.frame().abi_version = kRegionFrontierAbiVersionV2 + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "wrong frame struct size declines without mutation",
        [](FrameStorage& storage) {
            storage.frame().struct_size = static_cast<std::uint32_t>(
                sizeof(RegionFrontierFrameV2) - 1U);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "wrong typed-plane contract declines without mutation",
        [](FrameStorage& storage) {
            storage.frame().value_plane_contract
                ^= UINT32_C(0x00000001);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, RegionFrontierStatusV2::stale_generation,
        "unbound runtime generation is stale without mutation",
        [runtime_generation](FrameStorage& storage) {
            storage.frame().bound_runtime_generation = runtime_generation + 1U;
        });

    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "member count must match the compiled layout",
        [&layout](FrameStorage& storage) {
            storage.frame().member_count = layout.member_count - 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "readiness word count must match compiled members",
        [&layout](FrameStorage& storage) {
            storage.frame().readiness_word_count
                = layout.readiness_word_count + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "signal slot count must match the compiled union",
        [&layout](FrameStorage& storage) {
            storage.frame().signal_slot_count = layout.signal_slot_count + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "metadata count must match internal signal slots",
        [&layout](FrameStorage& storage) {
            storage.frame().metadata_count = layout.metadata_count + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "fanout edge count must match compiled topology",
        [&layout](FrameStorage& storage) {
            storage.frame().fanout_edge_count = layout.fanout_edge_count + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "pending write capacity must match the write-site plan",
        [&layout](FrameStorage& storage) {
            storage.frame().pending_write_capacity
                = layout.pending_write_capacity + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "staged event capacity must match the compiled plan",
        [&layout](FrameStorage& storage) {
            storage.frame().staged_event_capacity
                = layout.staged_event_capacity + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "committed log capacity must match its allocation recipe",
        [&layout](FrameStorage& storage) {
            storage.frame().committed_signal_capacity
                = layout.committed_signal_capacity + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "task count cannot exceed its preallocated borrowed span",
        [](FrameStorage& storage) {
            storage.frame().scheduler_task_count
                = storage.frame().scheduler_task_capacity + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "an activation index outside the compiled member range declines before mutation",
        [&layout](FrameStorage& storage) {
            storage.set_first_scheduler_task_payload(
                encode_region_frontier_payload_v2(
                    RegionFrontierEventKindV2::member_activation,
                    layout.member_count));
        });

    const auto check_pending_mapping = [&](const std::string_view message,
                                           const auto& mutation) {
        expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
            true, decline, message,
            [&mutation](FrameStorage& storage) {
                mutation(storage.writes().at(
                    storage.first_internal_write_pending_slot()));
            });
    };
    check_pending_mapping(
        "pending descriptor source instruction must match one unique write site",
        [](RegionFrontierPendingWriteV2& write) {
            write.source_instruction ^= UINT32_C(0x80000000);
        });
    check_pending_mapping(
        "pending descriptor update kind must match its compiled write site",
        [](RegionFrontierPendingWriteV2& write) {
            ++write.update_kind;
        });
    check_pending_mapping(
        "pending descriptor member must match its compiled write site",
        [&layout](RegionFrontierPendingWriteV2& write) {
            write.member_index = (write.member_index + 1U) % layout.member_count;
        });
    check_pending_mapping(
        "pending descriptor signal must match its compiled write site",
        [&layout](RegionFrontierPendingWriteV2& write) {
            write.signal_slot = (write.signal_slot + 1U) % layout.signal_slot_count;
        });
    check_pending_mapping(
        "pending descriptor word count must match its compiled write site",
        [](RegionFrontierPendingWriteV2& write) {
            ++write.word_count;
        });
    check_pending_mapping(
        "pending descriptor value kind must match its bound plane count",
        [](RegionFrontierPendingWriteV2& write) {
            write.value_kind = RegionFrontierValueKindV2::logic9;
        });
    check_pending_mapping(
        "pending descriptor cannot bind an incomplete Logic4 plane pair",
        [](RegionFrontierPendingWriteV2& write) {
            write.plane_count = 1U;
        });

    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "Logic4 signal plane rejects a Logic9 plane count",
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            storage.planes()[internal].plane_count
                = kRegionFrontierLogic9PlaneCountV2;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "Logic4 signal plane rejects a Logic9 value kind",
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            storage.planes()[internal].value_kind
                = RegionFrontierValueKindV2::logic9;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "Logic4 plane roles reject a non-null third plane",
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            storage.planes()[internal].current_planes[2U]
                = storage.role_words(internal, 2U).data();
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "Logic4 input binding rejects an extraneous fourth plane",
        [](FrameStorage& storage) {
            const auto boundary = storage.boundary_plane_index();
            storage.planes()[boundary].boundary_planes[3U]
                = storage.role_words(boundary, 1U).data();
        });

    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "boundary planes cannot expose mutable A4 roles",
        [](FrameStorage& storage) {
            const auto boundary = storage.boundary_plane_index();
            storage.planes()[boundary].current_planes[0U]
                = storage.role_words(boundary, 2U).data();
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "mutable A4 roles cannot alias one another",
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            storage.planes()[internal].previous_planes[0U]
                = storage.planes()[internal].current_planes[0U];
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "partially overlapping mutable A4 ranges decline before mutation",
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            auto& plane = storage.planes()[internal];
            require(plane.word_count >= 2U,
                "the partial-overlap fixture spans at least two words");
            plane.previous_planes[0U] = plane.current_planes[0U] + 1U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "scheduler task storage cannot overlap the frame object",
        [](FrameStorage& storage) {
            storage.frame().scheduler_tasks
                = reinterpret_cast<const RegionFrontierSchedulerTaskV2*>(
                    &storage.frame());
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "stored and owner planes cannot alias across value-plane indices",
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            auto& plane = storage.planes()[internal];
            plane.owner_planes[1U] = plane.stored_planes[0U];
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "body ports must bind to their matching frame planes",
        [](FrameStorage& storage) {
            const auto boundary = storage.boundary_plane_index();
            const auto internal = storage.internal_plane_index();
            storage.port_planes()[boundary] = &storage.planes()[internal];
        });

    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "runtime fanout masks cannot be zero",
        [](FrameStorage& storage) {
            require(!storage.fanout().empty(),
                "the tested fixture has at least one fanout descriptor");
            storage.fanout().front().trigger_mask = 0U;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline, "runtime fanout edge must retain compiled signal/member topology",
        [](FrameStorage& storage) {
            require(!storage.fanout().empty(),
                "the tested fixture has at least one fanout descriptor");
            auto& edge = storage.fanout().front();
            edge.member_index = (edge.member_index + 1U)
                % storage.frame().member_count;
        });

    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        true, decline, "initial committed log plus offered commits must fit capacity",
        [](FrameStorage& storage) {
            auto& frame = storage.frame();
            frame.committed_signal_count = frame.committed_signal_capacity;
            const auto internal = storage.internal_plane_index();
            for (std::size_t index = 0U;
                 index < frame.committed_signal_capacity; ++index) {
                storage.committed()[index] = RegionFrontierCommittedSignalV2 {
                    static_cast<std::uint32_t>(internal),
                    static_cast<std::uint32_t>(index & 1U),
                    static_cast<std::uint32_t>((index + 1U) & 1U),
                };
            }
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        true, decline, "initial committed log count cannot exceed capacity",
        [](FrameStorage& storage) {
            auto& frame = storage.frame();
            frame.committed_signal_count = frame.committed_signal_capacity + 1U;
        });
}

void run_valid_entry_witness(const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_activation_task();
    const auto before = storage.snapshot();
    const auto status = entry(&storage.frame());
    require(status != RegionFrontierStatusV2::decline_before_mutation
            && status != RegionFrontierStatusV2::stale_generation,
        "the generated entry accepts a correctly bound scheduler prefix");
    require(storage.frame().scheduler_task_cursor == 1U
            && storage.frame().current_member == UINT32_MAX,
        "the real member activation completes and advances the borrowed cursor");
    require(storage.dispatch_count() == before.dispatch_count + 1U,
        "the generated entry executes one body before returning");
    require(storage.frame().pending_write_count != 0U
            || storage.frame().staged_event_count != 0U,
        "the executed body preserves its private write or staged-key work");
}

void run_valid_internal_commit_witness(const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_internal_commit_task();
    const auto& site = layout.write_sites[storage.first_internal_write_site()];
    const auto internal = static_cast<std::size_t>(site.signal_slot);
    // The current plane already has the submitted value, while stored and
    // owner roles do not. This isolates A4 role mutation from current change.
    storage.role_words(internal, 2U)[0U] = UINT64_C(1);
    const auto status = entry(&storage.frame());
    require(status == RegionFrontierStatusV2::quiescent,
        "a valid same-value internal commit completes without new scheduler work");
    require(storage.frame().scheduler_task_cursor == 1U
            && storage.frame().pending_write_count == 0U
            && storage.frame().staged_event_count == 0U,
        "the generated entry retires the accepted internal commit task");
    require(storage.frame().committed_signal_count == 1U,
        "the generated commit appends one ordered diagnostic record");
    const auto& record = storage.committed().front();
    require(record.signal_slot == site.signal_slot
            && record.changed == 0U
            && record.state_changed == 1U,
        "the record separates an unchanged current value from changed owner state");
    require(storage.frame().current_commit_changed == 0U,
        "unchanged current value does not trigger dependent member scheduling");
    require(storage.role_words(internal, 6U)[0U] == UINT64_C(1)
            && storage.role_words(internal, 8U)[0U] == UINT64_C(1),
        "the accepted commit copies the value into stored and owner roles");
}

void run_valid_stored_owner_alias_witness(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_activation_task();
    const auto internal = storage.internal_plane_index();
    auto& plane = storage.planes()[internal];
    plane.owner_planes[0U] = plane.stored_planes[0U];
    plane.owner_planes[1U] = plane.stored_planes[1U];

    const auto status = entry(&storage.frame());
    require(status != RegionFrontierStatusV2::decline_before_mutation
            && status != RegionFrontierStatusV2::stale_generation,
        "the same signal's stored and owner value planes may share backing");
    require(storage.frame().scheduler_task_cursor == 1U,
        "the valid stored-owner alias still executes its offered activation");
}

void run_pending_range_compaction_witnesses(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    std::vector<std::size_t> range_words;
    std::size_t total_words = 0U;
    for (std::size_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        for (std::uint32_t value_plane = 0U;
             value_plane < site.plane_count; ++value_plane) {
            require(site.word_count
                    <= std::numeric_limits<std::size_t>::max() - total_words,
                "the pending arena test-size calculation cannot overflow");
            range_words.push_back(site.word_count);
            total_words += site.word_count;
        }
    }
    require(range_words.size() >= 2U && total_words != 0U,
        "the fixture contains multiple nonempty pending value ranges");

    const auto make_contiguous_offsets = [&range_words]() {
        std::vector<std::size_t> offsets;
        offsets.reserve(range_words.size());
        std::size_t next_word = 0U;
        for (const auto word_count : range_words) {
            offsets.push_back(next_word);
            next_word += word_count;
        }
        return offsets;
    };
    const auto require_internal_commit = [entry, &layout](FrameStorage& storage,
        const std::string_view message) {
        storage.prepare_internal_commit_task();
        const auto site_index = storage.first_internal_write_site();
        require(site_index < layout.write_site_count,
            "the internal commit site remains inside its borrowed layout span");
        const auto& site = layout.write_sites[site_index];
        storage.role_words(site.signal_slot, 2U)[0U] = UINT64_C(1);
        const auto status = entry(&storage.frame());
        require(status == RegionFrontierStatusV2::quiescent, message);
        require(storage.frame().scheduler_task_cursor == 1U
                && storage.frame().pending_write_count == 0U
                && storage.frame().committed_signal_count == 1U,
            message);
    };

    {
        auto offsets = make_contiguous_offsets();
        FrameStorage storage { kernel, layout, runtime_generation };
        storage.bind_pending_ranges(offsets, total_words);
        require_internal_commit(storage,
            "a contiguous pending-value arena passes the compact range check");
    }

    {
        const auto boundary = [&layout]() {
            for (std::size_t index = 0U;
                 index < layout.signal_slot_count; ++index) {
                if ((layout.signals[index].flags
                        & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                    != 0U) {
                    return index;
                }
            }
            throw std::runtime_error {
                "the padded-gap fixture has one read-only boundary plane"
            };
        }();
        const auto& boundary_shape = layout.signals[boundary];
        const auto gap_words = static_cast<std::size_t>(
            boundary_shape.word_count) * boundary_shape.plane_count;
        require(gap_words != 0U,
            "the padded pending gap can hold every boundary value plane");

        std::vector<std::size_t> offsets;
        offsets.reserve(range_words.size());
        offsets.push_back(0U);
        std::size_t next_word = range_words.front();
        const auto gap_first_word = next_word;
        next_word += gap_words;
        for (std::size_t index = 1U; index < range_words.size(); ++index) {
            offsets.push_back(next_word);
            next_word += range_words[index];
        }

        FrameStorage storage { kernel, layout, runtime_generation };
        storage.bind_pending_ranges(offsets, next_word);
        storage.bind_boundary_planes_to_pending_arena(gap_first_word);
        require_internal_commit(storage,
            "a padded bounding-range false positive falls back and accepts the exact disjoint ranges");
    }

    {
        std::vector<std::size_t> offsets;
        offsets.reserve(range_words.size());
        std::size_t next_word = total_words;
        for (const auto word_count : range_words) {
            next_word -= word_count;
            offsets.push_back(next_word);
        }
        require(next_word == 0U,
            "the reverse pending ranges fill their arena without overlap");

        FrameStorage storage { kernel, layout, runtime_generation };
        storage.bind_pending_ranges(offsets, total_words);
        require_internal_commit(storage,
            "nonmonotone but disjoint pending ranges use the full validator and accept");
    }

    {
        auto offsets = make_contiguous_offsets();
        offsets[1U] = offsets[0U];
        FrameStorage storage { kernel, layout, runtime_generation };
        storage.bind_pending_ranges(offsets, total_words);
        storage.prepare_internal_commit_task();
        const auto before = storage.snapshot();
        const auto status = entry(&storage.frame());
        require(status == RegionFrontierStatusV2::decline_before_mutation,
            "overlapping pending ranges decline through exact validation");
        storage.require_unchanged(before,
            "pending-overlap decline preserves frame and owned arena state");
    }

    if constexpr (sizeof(std::uintptr_t) == sizeof(std::uint64_t)) {
        auto offsets = make_contiguous_offsets();
        FrameStorage storage { kernel, layout, runtime_generation };
        storage.bind_pending_ranges(offsets, total_words);
        storage.prepare_internal_commit_task();
        const auto pending_slot = storage.first_internal_write_pending_slot();
        const auto near_address_limit
            = std::numeric_limits<std::uintptr_t>::max()
            - (alignof(std::uint64_t) - 1U);
        storage.writes().at(pending_slot).value_planes[0U]
            = reinterpret_cast<std::uint64_t*>(near_address_limit);
        const auto before = storage.snapshot();
        const auto status = entry(&storage.frame());
        require(status == RegionFrontierStatusV2::decline_before_mutation,
            "an aligned pending range that wraps at the address limit declines");
        storage.require_unchanged(before,
            "pending-overflow decline preserves frame and owned arena state");
    }
}

[[nodiscard]] llvm::FunctionType* shared_body_type(
    llvm::LLVMContext& context)
{
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const i1 = llvm::Type::getInt1Ty(context);
    return llvm::FunctionType::get(i32, { pointer, pointer, i1 }, false);
}

void create_test_global(llvm::Module& module, const std::string& name)
{
    auto* const i32 = llvm::Type::getInt32Ty(module.getContext());
    new llvm::GlobalVariable(module, i32, true,
        llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantInt::get(i32, UINT64_C(0)), name);
}

} // namespace

void run_region_frontier_write_site_shape_guard_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    require(layout.write_site_count >= 2U,
        "the write-site shape guard fixture has a final pending slot");
    const auto decline = RegionFrontierStatusV2::decline_before_mutation;
    const auto first_slot = layout.write_sites[0U].pending_slot;
    const auto last_site_index = layout.write_site_count - 1U;
    const auto last_slot = layout.write_sites[last_site_index].pending_slot;
    require(first_slot == 0U && last_slot == last_site_index,
        "the certified write-site table uses ordered pending slots");

    const auto internal_site_index = [&layout]() {
        for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
            if (layout.write_sites[index].event_kind
                == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::internal_commit)) {
                return index;
            }
        }
        throw std::runtime_error {
            "the active-slot fixture includes an internal commit site"
        };
    }();
    const auto internal_slot
        = layout.write_sites[internal_site_index].pending_slot;
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        true, decline,
        "an active internal write rejects an unknown pending flag",
        [internal_slot](FrameStorage& storage) {
            storage.writes().at(internal_slot).flags |= UINT32_C(0x80000000);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        true, decline,
        "an active internal write rejects the opposite boundary target",
        [internal_slot](FrameStorage& storage) {
            storage.writes().at(internal_slot).flags
                |= RegionFrontierPendingWriteFlagsV2::pending_boundary_target;
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        true, decline,
        "an active internal write cannot already be marked committed",
        [internal_slot](FrameStorage& storage) {
            storage.writes().at(internal_slot).flags
                |= RegionFrontierPendingWriteFlagsV2::pending_committed;
        });

    require(layout.write_site_count > 1U,
        "the later-row validation fixture has multiple write sites");
    const auto prepare_all_active_sites = [&layout](FrameStorage& storage) {
        storage.prepare_internal_commit_task();
        const auto seed_slot = storage.first_internal_write_pending_slot();
        const auto seed_key = storage.writes().at(seed_slot).commit_key;
        const auto seed_origin = storage.writes().at(seed_slot).origin;
        require(layout.write_site_count
                <= storage.frame().scheduler_task_capacity,
            "every active write-site fixture row has a scheduler task slot");
        for (std::size_t site_index = 0U;
             site_index < layout.write_site_count; ++site_index) {
            const auto& site = layout.write_sites[site_index];
            auto& write = storage.writes().at(site.pending_slot);
            const auto target_flag = site.event_kind
                    == static_cast<std::uint32_t>(
                        RegionFrontierEventKindV2::internal_commit)
                ? RegionFrontierPendingWriteFlagsV2::pending_internal_target
                : RegionFrontierPendingWriteFlagsV2::pending_boundary_target;
            write.flags = RegionFrontierPendingWriteFlagsV2::pending_active
                | RegionFrontierPendingWriteFlagsV2::pending_value_ready
                | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
                | target_flag;
            write.commit_key = seed_key;
            write.commit_key.stable_order += site_index;
            write.commit_key.sequence += site_index;
            write.origin = seed_origin;
            storage.members().at(site.member_index).activation_origin
                = seed_origin;
            storage.set_scheduler_task(site_index,
                RegionFrontierSchedulerTaskV2 {
                    write.commit_key.stable_order,
                    write.commit_key.sequence,
                    encode_region_frontier_payload_v2(
                        static_cast<RegionFrontierEventKindV2>(site.event_kind),
                        site.pending_slot),
                });
        }
        storage.frame().pending_write_count
            = static_cast<std::uint32_t>(layout.write_site_count);
        storage.frame().scheduler_task_count
            = static_cast<std::uint32_t>(layout.write_site_count);
    };
    {
        FrameStorage storage { kernel, layout, runtime_generation };
        prepare_all_active_sites(storage);
        const auto status = entry(&storage.frame());
        require(status != decline
                && status != RegionFrontierStatusV2::stale_generation,
            "all correctly populated active write rows pass initial validation");
    }
    {
        FrameStorage storage { kernel, layout, runtime_generation };
        prepare_all_active_sites(storage);
        auto& later_write = storage.writes().at(last_slot);
        later_write.source_instruction ^= UINT32_C(0x80000000);
        const auto before = storage.snapshot();
        const auto status = entry(&storage.frame());
        require(status == decline,
            "a corrupt later active write row declines after an earlier valid row");
        storage.require_unchanged(before,
            "later active-row decline preserves frame and owned arena state");
    }

    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "a malformed final write-site width declines before mutation",
        [last_slot](FrameStorage& storage) {
            storage.writes().at(last_slot).width ^= UINT32_C(1);
        });

    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "a missing first write-site value plane declines before mutation",
        [first_slot](FrameStorage& storage) {
            storage.writes().at(first_slot).value_planes[0U] = nullptr;
        });

    const auto& last_site = layout.write_sites[last_site_index];
    require(last_site.plane_count < 4U,
        "the Logic4 write-site fixture has an unused value plane");
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        true, decline,
        "an unexpected final write-site value plane declines before mutation",
        [first_slot, last_slot, plane = last_site.plane_count](
            FrameStorage& storage) {
            storage.writes().at(last_slot).value_planes[plane]
                = storage.writes().at(first_slot).value_planes[0U];
        });

    const auto first_invalid_slot = layout.write_site_count;
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        true, decline,
        "a pending index outside the immutable write-site table declines",
        [first_invalid_slot](FrameStorage& storage) {
            storage.set_first_scheduler_task_payload(
                encode_region_frontier_payload_v2(
                    RegionFrontierEventKindV2::internal_commit,
                    first_invalid_slot));
        });
}

namespace {

template <typename Prepare>
void require_checked_trusted_success(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 checked_entry,
    const RegionFrontierStepEntryV2 trusted_entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    const Prepare& prepare,
    const std::string_view message)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    prepare(storage);
    const auto before = storage.snapshot();
    const auto checked_status = checked_entry(&storage.frame());
    require(checked_status != RegionFrontierStatusV2::decline_before_mutation
            && checked_status != RegionFrontierStatusV2::stale_generation,
        message);
    const auto checked_after = storage.snapshot();

    storage.restore(before);
    storage.require_unchanged(before,
        "checked and trusted calls reuse the exact previously validated buffers");
    const auto trusted_status = trusted_entry(&storage.frame());
    require(trusted_status == checked_status, message);
    storage.require_matches(checked_after,
        "trusted alias-prevalidated entry matches checked frame and backing state");
}

template <typename Mutate>
void require_checked_trusted_rejection(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 checked_entry,
    const RegionFrontierStepEntryV2 trusted_entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation,
    const RegionFrontierStatusV2 expected_status,
    const Mutate& mutate,
    const std::string_view message)
{
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_activation_task();
    const auto valid_before = storage.snapshot();
    const auto validation_status = checked_entry(&storage.frame());
    require(validation_status != RegionFrontierStatusV2::decline_before_mutation
            && validation_status != RegionFrontierStatusV2::stale_generation,
        "the unmodified frame first validates its exact range geometry");
    storage.restore(valid_before);
    storage.require_unchanged(valid_before,
        "geometry validation restores the exact original frame and ranges");

    mutate(storage);
    const auto before = storage.snapshot();
    const auto checked_status = checked_entry(&storage.frame());
    require(checked_status == expected_status, message);
    storage.require_unchanged(before,
        "checked rejection leaves the frame and owned buffers unchanged");
    const auto trusted_status = trusted_entry(&storage.frame());
    require(trusted_status == expected_status
            && trusted_status == checked_status, message);
    storage.require_unchanged(before,
        "trusted rejection leaves the same frame and owned buffers unchanged");
}

} // namespace

void run_region_frontier_alias_prevalidated_entry_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 checked_entry,
    const RegionFrontierStepEntryV2 trusted_entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    require(checked_entry != nullptr && trusted_entry != nullptr,
        "the built-in executor provides checked and trusted entries");
    require(checked_entry != trusted_entry,
        "the trusted alias-prevalidated entry is a separate thunk");
    require(layout.execution_mode
            == RegionFrontierExecutionModeV2::systemverilog_active,
        "alias-prevalidated entry equivalence is scoped to the SV kernel");

    require_checked_trusted_success(kernel, checked_entry, trusted_entry,
        layout, runtime_generation,
        [](FrameStorage& storage) {
            storage.prepare_activation_task();
        },
        "checked and trusted activation entries both accept the certified frame");
    require_checked_trusted_success(kernel, checked_entry, trusted_entry,
        layout, runtime_generation,
        [](FrameStorage& storage) {
            storage.prepare_internal_commit_task();
            const auto pending_slot
                = storage.first_internal_write_pending_slot();
            const auto signal_slot = storage.writes().at(pending_slot).signal_slot;
            storage.role_words(signal_slot, 2U)[0U] = UINT64_C(1);
        },
        "checked and trusted internal-commit entries produce identical state");

    const auto stale = RegionFrontierStatusV2::stale_generation;
    const auto decline = RegionFrontierStatusV2::decline_before_mutation;
    require_checked_trusted_rejection(kernel, checked_entry, trusted_entry,
        layout, runtime_generation, stale,
        [&layout](FrameStorage& storage) {
            storage.frame().certificate_generation
                = layout.certificate_generation ^ UINT64_C(1);
        },
        "trusted entry retains the certificate-generation guard");
    require_checked_trusted_rejection(kernel, checked_entry, trusted_entry,
        layout, runtime_generation, stale,
        [&layout](FrameStorage& storage) {
            storage.frame().component_generation
                = layout.component_generation ^ UINT64_C(1);
        },
        "trusted entry retains the component-generation guard");
    require_checked_trusted_rejection(kernel, checked_entry, trusted_entry,
        layout, runtime_generation, decline,
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            storage.planes()[internal].width ^= UINT32_C(1);
        },
        "trusted entry retains the canonical plane-shape guard");
    require_checked_trusted_rejection(kernel, checked_entry, trusted_entry,
        layout, runtime_generation, decline,
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            storage.planes()[internal].signal_id ^= UINT32_C(0x40000000);
        },
        "trusted entry retains the bound physical-signal guard");
    require_checked_trusted_rejection(kernel, checked_entry, trusted_entry,
        layout, runtime_generation, decline,
        [](FrameStorage& storage) {
            const auto internal = storage.internal_plane_index();
            const auto width = storage.planes()[internal].width;
            require(width % 64U != 0U,
                "the tail-word witness uses a partial final word");
            const auto tail_bit = UINT64_C(1) << (width % 64U);
            storage.role_words(internal, 2U).at(
                storage.planes()[internal].word_count - 1U) |= tail_bit;
        },
        "trusted entry retains canonical Logic4 tail-bit validation");
    require_checked_trusted_rejection(kernel, checked_entry, trusted_entry,
        layout, runtime_generation, decline,
        [](FrameStorage& storage) {
            auto task = storage.snapshot().tasks.front();
            ++task.sequence;
            storage.set_scheduler_task(0U, task);
        },
        "trusted entry retains scheduler task-key authentication");
}

void run_region_frontier_adversarial_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    require(entry != nullptr,
        "the adversarial frame suite receives the production noexcept entry");
    require(runtime_generation != 0U,
        "the runtime generation is nonzero for trusted binding");
    require(layout.abi_version == kRegionFrontierAbiVersionV2
            && layout.struct_size == sizeof(RegionFrontierLayoutV2)
            && region_frontier_layout_header_valid_v2(layout),
        "the tested code owner publishes the current private ABI");
    run_valid_entry_witness(kernel, entry, layout, runtime_generation);
    run_valid_internal_commit_witness(
        kernel, entry, layout, runtime_generation);
    run_valid_stored_owner_alias_witness(
        kernel, entry, layout, runtime_generation);
    run_pending_range_compaction_witnesses(
        kernel, entry, layout, runtime_generation);
    run_adversarial_cases(kernel, entry, layout, runtime_generation);
    run_region_frontier_shared_binding_guard_tests(
        kernel, entry, layout, runtime_generation);
    run_region_frontier_entry_thunk_guard_tests(layout);
}

void run_region_frontier_shared_binding_guard_tests(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    require(entry != nullptr && layout.execution_mode
                == RegionFrontierExecutionModeV2::systemverilog_active,
        "the shared binding guard uses an exact SV entry and layout");
    const auto decline = RegionFrontierStatusV2::decline_before_mutation;
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, RegionFrontierStatusV2::stale_generation,
        "certificate generation is loaded from this plan's shared binding",
        [&layout](FrameStorage& storage) {
            storage.frame().certificate_generation
                = layout.certificate_generation ^ UINT64_C(1);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, RegionFrontierStatusV2::stale_generation,
        "component generation is loaded from this plan's shared binding",
        [&layout](FrameStorage& storage) {
            storage.frame().component_generation
                = layout.component_generation ^ UINT64_C(1);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "a caller cannot substitute a different physical member ProcessId",
        [](FrameStorage& storage) {
            storage.members().front().process_id ^= UINT32_C(0x40000000);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "a caller cannot substitute an internal physical SignalId",
        [](FrameStorage& storage) {
            storage.planes()[storage.internal_plane_index()].signal_id
                ^= UINT32_C(0x40000000);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "a caller cannot substitute an internal physical owner ProcessId",
        [](FrameStorage& storage) {
            storage.planes()[storage.internal_plane_index()].owner_process_id
                ^= UINT32_C(0x40000000);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "a caller cannot substitute a boundary physical SignalId",
        [](FrameStorage& storage) {
            storage.planes()[storage.boundary_plane_index()].signal_id
                ^= UINT32_C(0x40000000);
        });
    expect_status_and_no_mutation(kernel, entry, layout, runtime_generation,
        false, decline,
        "a caller cannot give a read-only boundary plane an owner",
        [&layout](FrameStorage& storage) {
            storage.planes()[storage.boundary_plane_index()].owner_process_id
                = layout.members[0U].process_id;
        });

    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_activation_task();
    const auto root_member = storage.root_member_index();
    const auto expected_process_id = layout.members[root_member].process_id;
    const auto status = entry(&storage.frame());
    require(status != RegionFrontierStatusV2::decline_before_mutation
            && status != RegionFrontierStatusV2::stale_generation,
        "the exact SV wrapper accepts its matching immutable physical binding");
    bool found_root_write = false;
    for (std::size_t event_index = 0U;
         event_index < storage.frame().staged_event_count; ++event_index) {
        const auto& event = storage.frame().staged_events[event_index];
        if (event.kind != static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::internal_commit)
            && event.kind != static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::boundary_commit)) {
            continue;
        }
        for (std::size_t site_index = 0U;
             site_index < layout.write_site_count; ++site_index) {
            const auto& site = layout.write_sites[site_index];
            if (site.member_index != root_member
                || site.pending_slot != event.descriptor_index) {
                continue;
            }
            found_root_write = true;
            require(event.stable_order == expected_process_id,
                "SV active stable order comes from the bound physical ProcessId");
            require(event.origin.stable_order == first_task_stable_order,
                "the event keeps the distinct scheduler activation origin");
        }
    }
    require(found_root_write,
        "the matching bound root member emits a source write event");
}

void run_region_frontier_entry_thunk_guard_tests(
    const RegionFrontierLayoutV2& layout)
{
    using SharedBodySetup
        = std::function<void(llvm::LLVMContext&, llvm::Module&)>;
    const auto expect_rejection = [&](const std::string_view wrapper_symbol,
                                      const std::string_view body_symbol,
                                      const SharedBodySetup& setup,
                                      const std::string_view message) {
        llvm::LLVMContext context;
        llvm::Module module { "region.frontier.thunk.guard", context };
        setup(context, module);
        bool rejected = false;
        try {
            (void) emit_region_frontier_entry_thunk_v2(module,
                std::string { wrapper_symbol }, std::string { body_symbol },
                layout);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, message);
    };

    expect_rejection("occupied.wrapper", "body",
        [](llvm::LLVMContext&, llvm::Module& module) {
            create_test_global(module, "occupied.wrapper");
        },
        "an exact wrapper rejects a preoccupied public thunk symbol");
    expect_rejection("wrapper", "body",
        [](llvm::LLVMContext&, llvm::Module& module) {
            create_test_global(module, "wrapper.physical.binding");
        },
        "an exact wrapper rejects a preoccupied immutable binding symbol");
    expect_rejection("wrapper", "occupied.body",
        [](llvm::LLVMContext&, llvm::Module& module) {
            create_test_global(module, "occupied.body");
        },
        "an exact wrapper rejects a non-function shared-body symbol");
    expect_rejection("wrapper", "wrong.signature",
        [](llvm::LLVMContext& context, llvm::Module& module) {
            auto* const i32 = llvm::Type::getInt32Ty(context);
            auto* const pointer = llvm::PointerType::getUnqual(context);
            auto* const type = llvm::FunctionType::get(i32, { pointer }, false);
            (void) llvm::Function::Create(type,
                llvm::GlobalValue::ExternalLinkage, "wrong.signature", module);
        },
        "an exact wrapper rejects a shared body with the wrong signature");
    expect_rejection("wrapper", "may.unwind",
        [](llvm::LLVMContext& context, llvm::Module& module) {
            auto* const type = shared_body_type(context);
            (void) llvm::Function::Create(type,
                llvm::GlobalValue::ExternalLinkage, "may.unwind", module);
        },
        "an exact wrapper does not bless an existing potentially-unwinding body");
    expect_rejection("wrapper", "wrong.calling.convention",
        [](llvm::LLVMContext& context, llvm::Module& module) {
            auto* const type = shared_body_type(context);
            auto* const body = llvm::Function::Create(type,
                llvm::GlobalValue::ExternalLinkage,
                "wrong.calling.convention", module);
            body->addFnAttr(llvm::Attribute::NoUnwind);
            body->setCallingConv(llvm::CallingConv::Fast);
        },
        "an exact wrapper requires the shared body's C calling convention");

    llvm::LLVMContext context;
    llvm::Module module { "region.frontier.thunk.accept", context };
    auto* const body = llvm::Function::Create(shared_body_type(context),
        llvm::GlobalValue::ExternalLinkage, "body", module);
    body->addFnAttr(llvm::Attribute::NoUnwind);
    auto* const wrapper = emit_region_frontier_entry_thunk_v2(module,
        "wrapper", "body", layout);
    require(wrapper != nullptr
            && wrapper->getCallingConv() == llvm::CallingConv::C
            && wrapper->hasFnAttribute(llvm::Attribute::NoUnwind),
        "a matching exact body produces a C-convention no-unwind wrapper");
    bool found_no_unwind_call = false;
    for (const auto& block : *wrapper) {
        for (const auto& instruction : block) {
            const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
            if (call == nullptr) {
                continue;
            }
            const auto* const alias_prevalidated = call->arg_size() == 3U
                ? llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(2U))
                : nullptr;
            found_no_unwind_call = call->doesNotThrow()
                && call->getCalledFunction() == body
                && alias_prevalidated != nullptr
                && alias_prevalidated->isZero();
        }
    }
    require(found_no_unwind_call,
        "the exact wrapper's call preserves the checked no-unwind contract");
    const auto* const binding = module.getGlobalVariable(
        "wrapper.physical.binding", true);
    require(binding != nullptr && binding->isConstant()
            && binding->getLinkage() == llvm::GlobalValue::PrivateLinkage,
        "the thunk keeps its per-plan physical binding in an immutable private global");
}

void run_region_frontier_generic_stable_order_test(
    const RegionConeActivationKernel& kernel,
    const RegionFrontierStepEntryV2 entry,
    const RegionFrontierLayoutV2& layout,
    const std::uint64_t runtime_generation)
{
    require(entry != nullptr && layout.execution_mode
                == RegionFrontierExecutionModeV2::generic_deferred_update,
        "the stable-order binding check receives a Generic deferred entry");
    FrameStorage storage { kernel, layout, runtime_generation };
    storage.prepare_activation_task();
    const auto root_member = storage.root_member_index();
    const auto physical_process_id = layout.members[root_member].process_id;
    const auto status = entry(&storage.frame());
    require(status == RegionFrontierStatusV2::generic_update_batch_ready,
        "the Generic shared entry stages an ordinary Update batch");
    require(storage.frame().staged_event_count != 0U,
        "the Generic entry emits a deferred Update event");

    bool found_root_write = false;
    for (std::size_t event_index = 0U;
         event_index < storage.frame().staged_event_count; ++event_index) {
        const auto& event = storage.frame().staged_events[event_index];
        if (event.kind != static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::generic_deferred_update)) {
            continue;
        }
        for (std::size_t site_index = 0U;
             site_index < layout.write_site_count; ++site_index) {
            const auto& site = layout.write_sites[site_index];
            if (site.member_index != root_member
                || site.pending_slot != event.descriptor_index) {
                continue;
            }
            found_root_write = true;
            require(event.stable_order == first_task_stable_order
                    && event.origin.stable_order == first_task_stable_order,
                "Generic stable order comes from the captured runtime origin");
            require(event.stable_order != physical_process_id,
                "the Generic origin distinguishes itself from remapped physical ProcessId");
        }
    }
    require(found_root_write,
        "the deferred event belongs to the remapped root member's output");
}

void run_region_frontier_prefix_guard_tests(
    const RegionFrontierStepEntryV2 entry)
{
    require(entry != nullptr,
        "the prefix guard suite receives the production noexcept entry");
    expect_guarded_prefix_decline(entry, kRegionFrontierAbiVersionV1,
        static_cast<std::uint32_t>(sizeof(RegionFrontierFrameV1)),
        "the emitted V2 entry rejects a protected V1-sized frame");
    expect_guarded_prefix_decline(entry, kRegionFrontierAbiVersionV2,
        static_cast<std::uint32_t>(sizeof(RegionFrontierFrameV1)),
        "the emitted V2 entry rejects a V2-tagged V1-sized frame");
    expect_guarded_prefix_decline(entry, kRegionFrontierAbiVersionV2,
        static_cast<std::uint32_t>(sizeof(RegionFrontierFrameV2) - 1U),
        "the emitted V2 entry rejects a protected truncated frame");
}

} // namespace fsim::compiler::test
