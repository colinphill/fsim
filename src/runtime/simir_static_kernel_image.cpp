// SPDX-License-Identifier: Apache-2.0
//
// Kernel images: the state the StaticKernel constructor builds before its
// native code (build_fresh), persisted beside the compiled bodies and the
// native image, so a later run of the same build restores it instead of
// planning members, levels and partitions again. The image holds every
// field build_fresh writes except the member operations, which are expanded
// again from the programs (they are the design's own), and the native view
// of the containers, which is derived from the containers and slots. The
// VHDL delta-collapse plan, which the constructor makes after the native
// and schedule stages, follows that state (record_collapse_plan).
//
// The bytes are BodyWriter varints, field by field in declaration order
// (boost::pfr), and are read back only by the same build: the cache key
// includes static_kernel_image_identity, and the image starts with a digest
// of the specification it was made for. Under FSIM_VERIFY_ARTIFACT=1 the
// caller builds afresh and compares the images instead.
#include "simir_static_kernel_compiled_internal.hpp"

#include <boost/pfr.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#define FSIM_KERNEL_IMAGE_ENVIRON _environ
#else
extern char** environ;
#define FSIM_KERNEL_IMAGE_ENVIRON environ
#endif

namespace fsim::runtime::simir {

using namespace static_kernel_detail;
using namespace static_kernel_compiled_detail;

namespace {

constexpr std::string_view image_magic { "fsim-static-kernel-image-v1" };

template <typename T>
struct is_vector : std::false_type { };
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type { };

template <typename T>
struct is_optional : std::false_type { };
template <typename T>
struct is_optional<std::optional<T>> : std::true_type { };

template <typename T>
struct is_pair : std::false_type { };
template <typename A, typename B>
struct is_pair<std::pair<A, B>> : std::true_type { };

template <typename T>
struct is_map : std::false_type { };
template <typename K, typename V, typename C, typename A>
struct is_map<std::map<K, V, C, A>> : std::true_type { };
template <typename K, typename V, typename H, typename E, typename A>
struct is_map<std::unordered_map<K, V, H, E, A>> : std::true_type { };

/// Stands for a reference-format register's initial value in a decoded
/// image until restore_image creates it.
[[nodiscard]] const std::shared_ptr<ContainerValue>& dynamic_marker()
{
    static const auto marker = std::make_shared<ContainerValue>();
    return marker;
}

/// Settings that change only how the kernel runs, not what it builds.
[[nodiscard]] bool run_only_setting(const std::string_view name)
{
    return name == "FSIM_KERNEL_THREADS" || name.starts_with("FSIM_KERNEL_MT_")
        || name == "FSIM_STATIC_KERNEL_PLAN_CACHE"
        || name == "FSIM_KERNEL_IMAGE_REQUIRED";
}

} // namespace

std::string static_kernel_image_identity()
{
    std::string identity { image_magic };
    identity += ";" __DATE__ " " __TIME__ ";";
    identity += static_kernel_constructor_identity();
    identity += ";";
    identity += static_kernel_canonical_identity();
    // Every kernel setting, in name order: a new one is covered unless it
    // is listed as run-only.
    std::vector<std::string> settings;
    for (char** entry = FSIM_KERNEL_IMAGE_ENVIRON; entry != nullptr && *entry != nullptr;
         ++entry) {
        const std::string_view text { *entry };
        const auto name = text.substr(0U, text.find('='));
        if ((name.starts_with("FSIM_KERNEL_")
                || name.starts_with("FSIM_STATIC_KERNEL_"))
            && !run_only_setting(name)) {
            settings.emplace_back(text);
        }
    }
    std::ranges::sort(settings);
    for (const auto& setting : settings) {
        identity += ";";
        identity += setting;
    }
    return identity;
}

class Interpreter::Impl::StaticKernel::ImageWriter {
public:
    explicit ImageWriter(std::string& out) : out_ { out } { }

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    template <typename T>
    void write(const T& value)
    {
        if constexpr (std::is_same_v<T, bool>) {
            out_.put(value ? 1U : 0U);
        } else if constexpr (std::is_enum_v<T>) {
            out_.put(static_cast<std::uint64_t>(
                static_cast<std::underlying_type_t<T>>(value)));
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            out_.put_signed(static_cast<std::int64_t>(value));
        } else if constexpr (std::is_integral_v<T>) {
            out_.put(static_cast<std::uint64_t>(value));
        } else if constexpr (std::is_same_v<T, std::string>) {
            out_.text(value);
        } else if constexpr (std::is_same_v<T, PackedLogic4>) {
            out_.packed(value);
        } else if constexpr (std::is_same_v<T, CompiledBody>) {
            out_.body(value);
        } else if constexpr (std::is_pointer_v<T> || std::is_same_v<T, NativeUnit>
            || std::is_same_v<T, std::vector<Operation>>) {
            // Rebuilt on restore: body identities, native code, operations.
        } else if constexpr (std::is_same_v<T, std::vector<SharedContainerValue>>) {
            // Reference-format registers hold their initial values, which
            // restore_image creates again: only which registers have one.
            out_.put(value.size());
            for (const auto& element : value) {
                out_.put(element ? 1U : 0U);
            }
        } else if constexpr (std::is_same_v<T, Member::Frame>) {
            // No thread has run, so no automatic frame is saved.
            ok_ = false;
        } else if constexpr (std::is_same_v<T, std::vector<PackedLogic4>>) {
            // Register files before the first run: mostly empty values.
            const bool empty = std::ranges::all_of(value,
                [](const PackedLogic4& element) {
                    return element.width() == 0U && !element.is_logic9();
                });
            out_.put(value.size());
            out_.put(empty ? 1U : 0U);
            if (!empty) {
                for (const auto& element : value) {
                    out_.packed(element);
                }
            }
        } else if constexpr (is_vector<T>::value) {
            out_.put(value.size());
            for (const auto& element : value) {
                write(element);
            }
        } else if constexpr (is_optional<T>::value) {
            out_.put(value ? 1U : 0U);
            if (value) {
                write(*value);
            }
        } else if constexpr (is_pair<T>::value) {
            write(value.first);
            write(value.second);
        } else if constexpr (is_map<T>::value) {
            // In key order, so the bytes do not depend on hashing.
            std::vector<const typename T::value_type*> entries;
            entries.reserve(value.size());
            for (const auto& entry : value) {
                entries.push_back(&entry);
            }
            std::ranges::sort(entries, { },
                [](const auto* entry) { return entry->first; });
            out_.put(entries.size());
            for (const auto* entry : entries) {
                write(entry->first);
                write(entry->second);
            }
        } else {
            static_assert(std::is_aggregate_v<T>, "kernel image field type");
            boost::pfr::for_each_field(value,
                [&](const auto& field) { write(field); });
        }
    }

private:
    BodyWriter out_;
    bool ok_ { true };
};

class Interpreter::Impl::StaticKernel::ImageReader {
public:
    explicit ImageReader(const std::string_view bytes) : in_ { bytes } { }

    [[nodiscard]] bool done() const noexcept { return in_.done(); }

    template <typename T>
    bool read(T& value)
    {
        if constexpr (std::is_same_v<T, bool> || std::is_enum_v<T>) {
            return in_.get(value);
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            std::int64_t raw { };
            if (!in_.get_signed(raw) || raw < std::numeric_limits<T>::min()
                || raw > std::numeric_limits<T>::max()) {
                return false;
            }
            value = static_cast<T>(raw);
            return true;
        } else if constexpr (std::is_integral_v<T>) {
            return in_.get(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return in_.text(value);
        } else if constexpr (std::is_same_v<T, PackedLogic4>) {
            return in_.packed(value);
        } else if constexpr (std::is_same_v<T, CompiledBody>) {
            return in_.body(value);
        } else if constexpr (std::is_pointer_v<T> || std::is_same_v<T, NativeUnit>
            || std::is_same_v<T, std::vector<Operation>>) {
            value = T { };
            return true;
        } else if constexpr (std::is_same_v<T, std::vector<SharedContainerValue>>) {
            std::uint64_t count { };
            if (!in_.get(count) || count > in_.remaining()) {
                return false;
            }
            value.assign(static_cast<std::size_t>(count), nullptr);
            for (auto& element : value) {
                bool present { };
                if (!in_.get(present)) {
                    return false;
                }
                if (present) {
                    element = dynamic_marker();
                }
            }
            return true;
        } else if constexpr (std::is_same_v<T, Member::Frame>) {
            return false;
        } else if constexpr (std::is_same_v<T, std::vector<PackedLogic4>>) {
            std::uint64_t count { };
            bool empty { };
            if (!in_.get(count) || !in_.get(empty)
                || (!empty && count > in_.remaining())
                || count > (std::uint64_t { 1 } << 32U)) {
                return false;
            }
            if (empty) {
                value.assign(static_cast<std::size_t>(count), PackedLogic4 { });
                return true;
            }
            value.resize(static_cast<std::size_t>(count));
            for (auto& element : value) {
                if (!in_.packed(element)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (is_vector<T>::value) {
            std::uint64_t count { };
            // Each element takes at least one byte.
            if (!in_.get(count) || count > in_.remaining()) {
                return false;
            }
            value.resize(static_cast<std::size_t>(count));
            for (auto& element : value) {
                if (!read(element)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (is_optional<T>::value) {
            bool present { };
            if (!in_.get(present)) {
                return false;
            }
            if (!present) {
                value.reset();
                return true;
            }
            return read(value.emplace());
        } else if constexpr (is_pair<T>::value) {
            return read(value.first) && read(value.second);
        } else if constexpr (is_map<T>::value) {
            std::uint64_t count { };
            if (!in_.get(count) || count > in_.remaining()) {
                return false;
            }
            value.clear();
            for (std::uint64_t entry = 0U; entry < count; ++entry) {
                typename T::key_type key { };
                typename T::mapped_type mapped { };
                if (!read(key) || !read(mapped)
                    || !value.emplace(std::move(key), std::move(mapped)).second) {
                    return false;
                }
            }
            return true;
        } else {
            static_assert(std::is_aggregate_v<T>, "kernel image field type");
            bool ok = true;
            boost::pfr::for_each_field(value,
                [&](auto& field) { ok = ok && read(field); });
            return ok;
        }
    }

private:
    BodyReader in_;
};

template <typename Self, typename Visit>
void Interpreter::Impl::StaticKernel::visit_image_fields(Self& self, Visit&& visit)
{
    visit(self.slots_);
    visit(self.arena_);
    visit(self.slot_of_signal_);
    visit(self.family_of_signal_);
    visit(self.families_);
    visit(self.container_of_object_);
    visit(self.containers_);
    visit(self.input_of_signal_);
    visit(self.inputs_);
    visit(self.members_);
    visit(self.member_process_);
    visit(self.program_members_);
    visit(self.startup_members_);
    visit(self.resolver_members_);
    visit(self.switch_members_);
    visit(self.transaction_inputs_);
    visit(self.slot_waitable_);
    visit(self.behavioral_);
    visit(self.vhdl_threads_);
    visit(self.named_disable_);
    visit(self.mirror_of_signal_);
    // slot_conversions_, conversion_queued_, wires_ and slot_wires_ are
    // planned again (plan_conversions_and_wires), with the host resolvers
    // they replace.
    visit(self.conversion_members_);
    visit(self.partitions_);
    visit(self.compiled_members_);
    visit(self.specialized_members_);
    visit(self.forwarded_loads_);
}

std::uint64_t Interpreter::Impl::StaticKernel::image_spec_digest(
    const StaticKernelRuntimeSpec& spec)
{
    // What build_fresh reads of the specification besides the programs.
    std::string bytes;
    ImageWriter writer { bytes };
    writer.write(spec.host);
    writer.write(spec.members);
    writer.write(spec.owned_signals);
    writer.write(spec.boundary_outputs);
    writer.write(spec.families);
    writer.write(spec.containers);
    writer.write(spec.import_host_writes);
    writer.write(spec.writer_regions);
    writer.write(spec.unwritten_inputs);
    writer.write(spec.sole_writer_inputs);
    writer.write(spec.codegen != nullptr);
    return std::hash<std::string_view> { }(bytes);
}

bool Interpreter::Impl::StaticKernel::image_scope(
    const StaticKernelRuntimeSpec& spec) const
{
    // Hosted processes and debug points are planned with the host; the
    // keep-names diagnostics name every member.
    return spec.hosted.empty() && !spec.debug_points && !debug_points_
        && !profile_ && !trace_
        && std::getenv("FSIM_KERNEL_DUMP") == nullptr
        && std::getenv("FSIM_KERNEL_VERIFY_BODIES") == nullptr
        && std::getenv("FSIM_STATIC_KERNEL_GENERIC") == nullptr;
}

void Interpreter::Impl::StaticKernel::note_image(
    const char* action, const char* outcome, const std::size_t bytes)
{
    if (std::getenv("FSIM_PROFILE_PHASES") != nullptr) {
        std::cerr << "fsim-profile: static-kernel-image " << action << '='
                  << outcome << " bytes=" << bytes << '\n';
    }
}

bool Interpreter::Impl::StaticKernel::record_image(
    const StaticKernelRuntimeSpec& spec, std::string& out) const
{
    out.clear();
    if (!image_scope(spec)) {
        note_image("record", "out_of_scope", 0U);
        return false;
    }
    const auto digest = image_spec_digest(spec);
    ImageWriter writer { out };
    writer.write(std::string { image_magic });
    writer.write(digest);
    // FSIM_PROFILE_PHASES: the bytes of each field, in visit order.
    const bool sizes = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    std::string field_sizes;
    visit_image_fields(*this, [&](const auto& field) {
        const auto before = out.size();
        writer.write(field);
        if (sizes) {
            field_sizes += ' ' + std::to_string(out.size() - before);
        }
    });
    if (sizes) {
        std::cerr << "fsim-profile: static-kernel-image fields" << field_sizes << '\n';
    }
    writer.write(not_compiled_reason_.size());
    for (const char* reason : not_compiled_reason_) {
        writer.write(reason != nullptr);
        if (reason != nullptr) {
            writer.write(std::string { reason });
        }
    }
    if (!writer.ok()) {
        out.clear();
        note_image("record", "frames", 0U);
        return false;
    }
    note_image("record", "recorded", out.size());
    return true;
}

void Interpreter::Impl::StaticKernel::Member::materialize_operations() const
{
    // As restore_image copies the operations of a member it does not change.
    const auto& operations
        = operations_source->processes.program_view(process).operations();
    operations_source = nullptr;
    operations_.reserve(operations.size());
    Operation expand_scratch;
    for (std::size_t op = 0U; op < operations.size(); ++op) {
        const auto& expanded = operations.expanded_ref(op, expand_scratch);
        if (&expanded == &expand_scratch) {
            operations_.push_back(std::move(expand_scratch));
        } else {
            operations_.push_back(expanded);
        }
    }
}

void Interpreter::Impl::StaticKernel::record_host_scan(const HostScan& scan)
{
    // Between the image's reasons and the collapse plan.
    if (!recording_image_ || recording_image_->empty()) {
        return;
    }
    ImageWriter writer { *recording_image_ };
    writer.write(scan);
    if (!writer.ok()) {
        recording_image_->clear();
    }
}

void Interpreter::Impl::StaticKernel::record_collapse_plan(const CollapsePlan& plan)
{
    // record_image wrote build_fresh's state; the constructor plans
    // collapsing after the native and schedule stages, so the plan follows.
    if (recording_image_->empty()) {
        return;
    }
    ImageWriter writer { *recording_image_ };
    writer.write(plan);
    if (!writer.ok()) {
        recording_image_->clear();
    }
}

bool Interpreter::Impl::StaticKernel::restore_image(
    StaticKernelRuntimeSpec& spec)
{
    if (!image_scope(spec)) {
        return false;
    }
    // FSIM_KERNEL_IMAGE_REQUIRED (tests): a kernel that may have an image
    // must restore one.
    const bool required = std::getenv("FSIM_KERNEL_IMAGE_REQUIRED") != nullptr;
    const auto refuse = [&](const char* reason) {
        note_image("restore", reason, 0U);
        if (required) {
            throw std::logic_error {
                std::string { "static kernel image was not restored: " } + reason };
        }
        return false;
    };
    if (!spec.kernel_image || spec.kernel_image->empty()) {
        return refuse("none");
    }
    // FSIM_PROFILE_PHASES: `static-kernel-image-stage NAME_ms=` per part.
    const bool stages = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    auto stage_started = std::chrono::steady_clock::now();
    const auto stage = [&](const char* name) {
        if (stages) {
            const auto now = std::chrono::steady_clock::now();
            std::cerr << "fsim-profile: static-kernel-image-stage " << name << "_ms="
                      << std::chrono::duration<double, std::milli>(now - stage_started).count()
                      << '\n';
            stage_started = now;
        }
    };
    ImageReader reader { *spec.kernel_image };
    std::string magic;
    std::uint64_t digest { };
    const char* failure = nullptr;
    if (!reader.read(magic) || magic != image_magic || !reader.read(digest)) {
        failure = "header";
    } else if (digest != image_spec_digest(spec)) {
        failure = "specification";
    }
    bool ok = failure == nullptr;
    if (ok) {
        visit_image_fields(*this, [&](auto& field) { ok = ok && reader.read(field); });
    }
    std::vector<std::optional<std::string>> reasons;
    HostScan host_scan;
    CollapsePlan collapse;
    ok = ok && reader.read(reasons) && reader.read(host_scan)
        && reader.read(collapse) && reader.done();
    if (!ok && failure == nullptr) {
        failure = "fields";
    }
    const auto signal_count = impl_.signals.size();
    ok = ok && members_.size() == spec.members.size()
        && member_process_.size() == members_.size()
        && slot_of_signal_.size() == signal_count
        && family_of_signal_.size() == signal_count
        && input_of_signal_.size() == signal_count
        && mirror_of_signal_.size() == signal_count
        && container_of_object_.size() == impl_.container_objects.size()
        && (reasons.empty() || reasons.size() == members_.size())
        && (collapse.member_levels.empty()
            || (collapse.member_levels.size() == members_.size()
                && std::ranges::all_of(collapse.member_levels,
                    [&](const std::uint32_t level) {
                        return level <= collapse.levels;
                    })));
    if (!ok && failure == nullptr) {
        failure = "shape";
    }
    // The operations, as build_fresh expands them: the program's, with a
    // conversion's resolver program rewritten, a SystemVerilog member's calls
    // inlined in its body, and a VHDL member's specialization (from the
    // persisted specializations) appended.
    stage("decode");
    std::unordered_map<ProcessId, StaticKernelSpecialization*> specialized;
    if (spec.specializations) {
        for (auto& entry : spec.specializations->members) {
            if (entry.specialized) {
                specialized.emplace(entry.process, &entry);
            }
        }
    }
    std::vector<StaticKernelSpecialization*> moved;
    Operation expand_scratch;
    for (std::size_t index = 0U; ok && index < members_.size(); ++index) {
        auto& member = members_[index];
        const auto& member_spec = spec.members[index];
        if (member.process != member_spec.process) {
            failure = "operations:process";
            ok = false;
            break;
        }
        const ProcessProgramView program = member_spec.process == spec.host
            ? ProcessProgramView { spec.host_original }
            : impl_.processes.program_view(member_spec.process);
        const auto& operations = program.operations();
        // A member whose operations are its program's copies them on first
        // use (Member::materialize_operations); a parallel kernel's lanes
        // could do that at once, so it copies them now.
        const bool lazy = member_spec.process != spec.host && !member.conversion
            && !member.inlined_calls && member.origin.empty() && mt_threads_ == 1U;
        if (lazy) {
            member.operations_source = &impl_;
        } else {
            member.operations().reserve(operations.size());
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                const auto& expanded = operations.expanded_ref(op, expand_scratch);
                if (&expanded == &expand_scratch) {
                    member.operations().push_back(std::move(expand_scratch));
                } else {
                    member.operations().push_back(expanded);
                }
            }
        }
        const auto operation_count = lazy ? operations.size() : member.operations_.size();
        member.body_identity = operations.body_identity();
        if (member.conversion) {
            const auto resolver
                = impl_.vhdl_resolvers.find(member_spec.conversion_target);
            if (resolver == impl_.vhdl_resolvers.end()
                || resolver->second.process != member.process) {
                failure = "operations:conversion";
                ok = false;
                break;
            }
            rewrite_conversion(member, member_spec.conversion_target,
                resolver->second.output);
        }
        // As build_fresh checks the body.
        if (member_spec.body_end > operation_count
            || member_spec.body_begin > (member.vhdl ? operation_count
                                                     : member_spec.body_end)) {
            failure = "operations:bounds";
            ok = false;
            break;
        }
        if (member.inlined_calls) {
            // build_fresh found calls in the body.
            const bool wait_first = member_spec.exit_alt == 0U;
            auto inlined = static_kernel_inline_calls(member.operations(),
                wait_first ? 0U : member_spec.body_end,
                wait_first ? member_spec.body_end : member_spec.exit_alt);
            if (!inlined || inlined->body_begin != member.body_begin
                || inlined->body_end != member.body_end
                || inlined->exit_alt != member.exit_alt) {
                failure = "operations:inline";
                ok = false;
                break;
            }
            member.operations() = std::move(inlined->operations);
        } else if (member.prologue_end != member_spec.body_begin
            || member.body_end != member_spec.body_end
            || member.exit_alt != member_spec.exit_alt) {
            failure = "operations:body";
            ok = false;
            break;
        }
        const auto original_size = static_cast<std::uint32_t>(
            lazy ? operation_count : member.operations_.size());
        if (!member.origin.empty()) {
            // As compile_members applies a restored specialization.
            const auto found = specialized.find(member.process);
            if (found == specialized.end()
                || found->second->original_size != original_size
                || member.origin.size()
                    != original_size + found->second->operations.size()
                || member.body_begin != original_size) {
                failure = "operations:specialization";
                ok = false;
                break;
            }
            // Each persisted entry serves one member: move it, as
            // compile_members does (a later refusal specializes afresh).
            member.operations().insert(member.operations().end(),
                std::make_move_iterator(found->second->operations.begin()),
                std::make_move_iterator(found->second->operations.end()));
            found->second->operations.clear();
            moved.push_back(found->second);
            member.body_identity = nullptr;
        } else if (member.body_begin != member.prologue_end) {
            failure = "operations:body_begin";
            ok = false;
            break;
        }
        if (std::ranges::find(member.dynamic_containers, dynamic_marker())
            != member.dynamic_containers.end()) {
            // As build_fresh gives them their initial values.
            const auto types = process_layout_detail::ProcessLayoutAccess::view(
                program.container_register_types());
            for (std::size_t id = 0U; id < member.dynamic_containers.size(); ++id) {
                if (member.dynamic_containers[id] != dynamic_marker()) {
                    continue;
                }
                if (id >= types.size()) {
                    failure = "operations:dynamic";
                    ok = false;
                    break;
                }
                member.dynamic_containers[id]
                    = impl_.default_container_register(types[id]);
            }
        }
    }
    if (!ok) {
        // Entries whose operations moved no longer match their members:
        // compile_members specializes those members afresh.
        for (auto* entry : moved) {
            entry->original_size = std::numeric_limits<std::uint32_t>::max();
        }
        visit_image_fields(*this, [](auto& field) {
            field = std::remove_cvref_t<decltype(field)> { };
        });
        return refuse(failure != nullptr ? failure : "operations");
    }
    for (const auto& member : members_) {
        // As build_fresh records it on the host.
        if (member.process == spec.host
            && !(vhdl_ && (!mixed_ || member.vhdl))
            && member.phase != SchedulerPhase::active) {
            impl_.static_kernel_region_host = member.process;
            impl_.static_kernel_region_host_phase = member.phase;
        }
    }
    stage("operations");
    plan_conversions_and_wires(spec);
    image_reasons_.clear();
    image_reasons_.reserve(reasons.size());
    not_compiled_reason_.assign(reasons.size(), nullptr);
    for (std::size_t index = 0U; index < reasons.size(); ++index) {
        if (reasons[index]) {
            not_compiled_reason_[index]
                = image_reasons_.emplace_back(std::move(*reasons[index])).c_str();
        }
    }
    build_container_info();
    fast_runs_.assign(members_.size(), FastRun { });
    restored_host_scan_ = host_scan;
    restored_collapse_ = std::move(collapse);
    stage("finish");
    note_image("restore", "restored", spec.kernel_image->size());
    return true;
}

} // namespace fsim::runtime::simir
