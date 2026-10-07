// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_container_helpers.hpp"

#include <algorithm>
#include <charconv>
#include <type_traits>
#include <utility>

namespace fsim::runtime::simir {
namespace {

static_assert(std::is_nothrow_move_constructible_v<AggregateSignalBatch>);
static_assert(std::is_nothrow_move_assignable_v<AggregateSignalBatch>);
static_assert(std::is_nothrow_move_assignable_v<PackedLogic4>);
static_assert(std::is_nothrow_move_assignable_v<ContainerValue>);
static_assert(std::is_nothrow_move_constructible_v<
    ContainerReferenceRefresh>);
static_assert(std::is_nothrow_move_assignable_v<
    ContainerReferenceRefresh>);

class ScopedAggregateSignalBatch final {
public:
    explicit ScopedAggregateSignalBatch(AggregateSignalBatch& batch)
        : batch_(batch)
    {
        if (batch_.depth != 0U) {
            suspended_.emplace(std::move(batch_));
            batch_ = { };
        }
    }

    ScopedAggregateSignalBatch(const ScopedAggregateSignalBatch&) = delete;
    ScopedAggregateSignalBatch& operator=(
        const ScopedAggregateSignalBatch&) = delete;

    ~ScopedAggregateSignalBatch() noexcept
    {
        // A logical whole write owns an independent proxy publication frame.
        // Discard any unfinished inner frame, then reveal the paused outer
        // operation even if an exception escaped the inner callback path.
        batch_ = { };
        if (suspended_) {
            batch_ = std::move(*suspended_);
        }
    }

private:
    AggregateSignalBatch& batch_;
    std::optional<AggregateSignalBatch> suspended_;
};

template <typename Owner>
void require_container_signal_role_materialized(
    Owner& owner, ContainerObjectId object)
{
    if (owner.region_forwarding_role_journal_nonempty_components == 0U) {
        return;
    }
    for (std::size_t depth = 0U;
         depth < owner.container_objects.size();
         ++depth) {
        if (object >= owner.container_objects.size()) {
            return;
        }
        const bool has_signal_alias
            = (object < owner.container_signal_aliases.size()
                && owner.container_signal_aliases[object])
            || (object < owner.container_aggregate_signal_aliases.size()
                && owner.container_aggregate_signal_aliases[object])
            || (object < owner.container_element_signal_aliases.size()
                && std::ranges::any_of(
                    owner.container_element_signal_aliases[object],
                    [](const auto& alias) { return alias.has_value(); }));
        if (has_signal_alias) {
            owner.require_all_region_forwarding_role_journals_flushed();
            return;
        }
        const auto& container = owner.container_objects[object];
        if (!container.slice_alias) {
            return;
        }
        object = container.slice_alias->object;
    }
}

class ScopedContainerAliasWriteFrame final {
public:
    explicit ScopedContainerAliasWriteFrame(ContainerAliasWriteBatch& batch)
        : batch_(batch)
        , depth_(batch.frames.size())
    {
    }

    ScopedContainerAliasWriteFrame(
        const ScopedContainerAliasWriteFrame&) = delete;
    ScopedContainerAliasWriteFrame& operator=(
        const ScopedContainerAliasWriteFrame&) = delete;

    ~ScopedContainerAliasWriteFrame() noexcept
    {
        // take_container_alias_write_batch normally pops this frame. If an
        // unexpected exception interrupts capture or publication, do not
        // leave a stale nested frame to absorb a later unrelated write.
        if (depth_ != 0U && batch_.frames.size() == depth_) {
            batch_.frames.pop_back();
        }
    }

private:
    ContainerAliasWriteBatch& batch_;
    std::size_t depth_;
};

template <typename Value, typename Callback>
void visit_container_packed_leaves(Value& value, Callback&& callback)
{
    if (value.type.aggregate_value) {
        for (auto& member : value.nested_elements) {
            visit_container_packed_leaves(member, callback);
        }
        return;
    }
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        for (auto& element : value.elements) {
            callback(element);
        }
        return;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        for (auto& element : value.nested_elements) {
            visit_container_packed_leaves(element, callback);
        }
        return;
    case ContainerElementKind::String:
        throw std::logic_error {
            "string container values cannot use a packed signal alias"
        };
    }
}

template <typename Value>
void copy_container_value_in_place(
    Value& destination,
    const Value& source) noexcept
{
    if (destination.type != source.type) {
        std::terminate();
    }
    if (destination.keys.size() != source.keys.size()
        || destination.string_keys.size() != source.string_keys.size()) {
        std::terminate();
    }
    for (std::size_t index = 0U;
        index < destination.keys.size(); ++index) {
        if (destination.keys[index].width() != source.keys[index].width()) {
            std::terminate();
        }
        destination.keys[index] = source.keys[index];
    }
    for (std::size_t index = 0U;
        index < destination.string_keys.size(); ++index) {
        if (destination.string_keys[index].capacity()
            < source.string_keys[index].size()) {
            std::terminate();
        }
        destination.string_keys[index] = source.string_keys[index];
    }
    if (destination.type.aggregate_value) {
        if (destination.nested_elements.size()
            != source.nested_elements.size()) {
            std::terminate();
        }
        for (std::size_t index = 0U;
            index < destination.nested_elements.size(); ++index) {
            copy_container_value_in_place(
                destination.nested_elements[index],
                source.nested_elements[index]);
        }
        return;
    }
    switch (destination.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        if (destination.elements.size() != source.elements.size()) {
            std::terminate();
        }
        for (std::size_t index = 0U;
            index < destination.elements.size(); ++index) {
            if (destination.elements[index].width()
                    != source.elements[index].width()
                || destination.elements[index].is_logic9()
                    != source.elements[index].is_logic9()) {
                std::terminate();
            }
            destination.elements[index] = source.elements[index];
        }
        return;
    case ContainerElementKind::String:
        if (destination.string_elements.size()
            != source.string_elements.size()) {
            std::terminate();
        }
        for (std::size_t index = 0U;
            index < destination.string_elements.size(); ++index) {
            if (destination.string_elements[index].capacity()
                < source.string_elements[index].size()) {
                std::terminate();
            }
            destination.string_elements[index]
                = source.string_elements[index];
        }
        return;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        if (destination.nested_elements.size()
            != source.nested_elements.size()) {
            std::terminate();
        }
        for (std::size_t index = 0U;
            index < destination.nested_elements.size(); ++index) {
            copy_container_value_in_place(
                destination.nested_elements[index],
                source.nested_elements[index]);
        }
        return;
    }
}

template <typename Value>
bool same_container_storage_shape(
    const Value& left,
    const Value& right) noexcept
{
    if (left.type != right.type
        || left.elements.size() != right.elements.size()
        || left.string_elements.size() != right.string_elements.size()
        || left.nested_elements.size() != right.nested_elements.size()
        || left.keys.size() != right.keys.size()
        || left.string_keys.size() != right.string_keys.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.elements.size(); ++index) {
        if (left.elements[index].width() != right.elements[index].width()
            || left.elements[index].is_logic9()
                != right.elements[index].is_logic9()) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < left.keys.size(); ++index) {
        if (left.keys[index].width() != right.keys[index].width()
            || left.keys[index].is_logic9() != right.keys[index].is_logic9()) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < left.nested_elements.size(); ++index) {
        if (!same_container_storage_shape(
                left.nested_elements[index], right.nested_elements[index])) {
            return false;
        }
    }
    return true;
}

template <typename Value>
void copy_container_slice_in_place(
    Value& destination,
    const Value& source,
    const ContainerSliceAlias& alias) noexcept
{
    const auto descending = alias.selected_left >= alias.selected_right;
    const auto count = destination.type.index_left
            >= destination.type.index_right
        ? static_cast<std::uint64_t>(
              static_cast<std::int64_t>(destination.type.index_left)
              - destination.type.index_right)
            + 1U
        : static_cast<std::uint64_t>(
              static_cast<std::int64_t>(destination.type.index_right)
              - destination.type.index_left)
            + 1U;
    const bool count_matches
        = destination.type.aggregate_value
        ? count == destination.nested_elements.size()
        : destination.type.element_kind == ContainerElementKind::Packed
                || destination.type.element_kind
                    == ContainerElementKind::Scalar
            ? count == destination.elements.size()
            : destination.type.element_kind == ContainerElementKind::String
                ? count == destination.string_elements.size()
                : count == destination.nested_elements.size();
    if (!count_matches) {
        std::terminate();
    }
    if (destination.type.aggregate_value) {
        for (std::size_t ordinal = 0U;
            ordinal < static_cast<std::size_t>(count);
            ++ordinal) {
            const auto selected_index = static_cast<std::int32_t>(
                static_cast<std::int64_t>(alias.selected_left)
                + (descending
                        ? -static_cast<std::int64_t>(ordinal)
                        : static_cast<std::int64_t>(ordinal)));
            const auto source_ordinal = fixed_offset(source.type, selected_index);
            if (source_ordinal >= source.nested_elements.size()
                || ordinal >= destination.nested_elements.size()) {
                std::terminate();
            }
            copy_container_value_in_place(
                destination.nested_elements[ordinal],
                source.nested_elements[source_ordinal]);
        }
        return;
    }
    for (std::size_t ordinal = 0U;
        ordinal < static_cast<std::size_t>(count);
        ++ordinal) {
        const auto selected_index = static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                    ? -static_cast<std::int64_t>(ordinal)
                    : static_cast<std::int64_t>(ordinal)));
        const auto source_ordinal = fixed_offset(source.type, selected_index);
        switch (destination.type.element_kind) {
        case ContainerElementKind::Packed:
        case ContainerElementKind::Scalar:
            if (source_ordinal >= source.elements.size()
                || ordinal >= destination.elements.size()) {
                std::terminate();
            }
            destination.elements[ordinal] = source.elements[source_ordinal];
            break;
        case ContainerElementKind::String:
            if (source_ordinal >= source.string_elements.size()
                || ordinal >= destination.string_elements.size()
                || destination.string_elements[ordinal].capacity()
                    < source.string_elements[source_ordinal].size()) {
                std::terminate();
            }
            destination.string_elements[ordinal]
                = source.string_elements[source_ordinal];
            break;
        case ContainerElementKind::Container:
        case ContainerElementKind::Aggregate:
            if (source_ordinal >= source.nested_elements.size()
                || ordinal >= destination.nested_elements.size()) {
                std::terminate();
            }
            copy_container_value_in_place(
                destination.nested_elements[ordinal],
                source.nested_elements[source_ordinal]);
            break;
        }
    }
}

template <typename Value, typename Callback>
void visit_container_strings(Value& value, Callback&& callback)
{
    if (value.type.aggregate_value) {
        for (auto& member : value.nested_elements) {
            visit_container_strings(member, callback);
        }
        return;
    }
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        return;
    case ContainerElementKind::String:
        for (auto& element : value.string_elements) {
            callback(element);
        }
        for (auto& key : value.string_keys) {
            callback(key);
        }
        return;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        for (auto& element : value.nested_elements) {
            visit_container_strings(element, callback);
        }
        return;
    }
}

template <typename Value>
std::size_t maximum_container_string_length(const Value& value) noexcept
{
    std::size_t result { };
    if (value.type.aggregate_value) {
        for (const auto& member : value.nested_elements) {
            result = std::max(
                result, maximum_container_string_length(member));
        }
        return result;
    }
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        return 0U;
    case ContainerElementKind::String:
        for (const auto& element : value.string_elements) {
            result = std::max(result, element.size());
        }
        for (const auto& key : value.string_keys) {
            result = std::max(result, key.size());
        }
        return result;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        for (const auto& element : value.nested_elements) {
            result = std::max(
                result, maximum_container_string_length(element));
        }
        return result;
    }
    return result;
}

} // namespace

ContainerReferenceRefresh
Interpreter::Impl::make_container_reference_refresh(
    const ContainerObjectId object) const
{
    const auto& value = get_container_object(object).initial_value;
    const auto width = container_signal_bridge_width(value.type);
    if (!width) {
        throw std::logic_error {
            "a retained packed container alias has no fixed bridge width"
        };
    }
    ContainerReferenceRefresh result;
    result.object = object;
    std::size_t leaf_count { };
    visit_container_packed_leaves(
        value, [&leaf_count](const auto&) { ++leaf_count; });
    result.leaves.reserve(leaf_count);
    auto cursor = *width;
    visit_container_packed_leaves(value, [&](const auto& element) {
        if (element.width() > cursor) {
            throw std::logic_error {
                "retained container leaf exceeds its packed bridge width"
            };
        }
        cursor -= element.width();
        result.leaves.push_back({
            cursor, PackedLogic4(element.width(), Logic4::zero) });
    });
    if (cursor != 0U) {
        throw std::logic_error {
            "retained container leaves do not fill their packed bridge"
        };
    }
    return result;
}

bool Interpreter::Impl::container_value_reference_depends_on_signals(
    const ContainerObjectId object,
    const std::span<const SignalId> changed_signals) const noexcept
{
    auto selected = object;
    for (std::size_t depth = 0U; depth < container_objects.size(); ++depth) {
        if (selected >= container_objects.size()) {
            return false;
        }
        if (selected < container_signal_aliases.size()
            && container_signal_aliases[selected]
            && std::ranges::find(
                   changed_signals,
                   container_signal_aliases[selected]->signal)
                != changed_signals.end()) {
            return true;
        }
        if (selected < container_signal_aliases.size()
            && container_signal_aliases[selected]) {
            const auto signal = container_signal_aliases[selected]->signal;
            if (signal < signal_container_aggregate_aliases.size()
                && signal_container_aggregate_aliases[signal]) {
                const auto aggregate
                    = *signal_container_aggregate_aliases[signal];
                if (aggregate < container_element_signal_aliases.size()) {
                    for (const auto& alias :
                        container_element_signal_aliases[aggregate]) {
                        if (alias
                            && std::ranges::find(
                                   changed_signals, alias->signal)
                                != changed_signals.end()) {
                            return true;
                        }
                    }
                }
            }
        }
        if (selected < container_element_signal_aliases.size()) {
            for (const auto& alias : container_element_signal_aliases[selected]) {
                if (alias
                    && std::ranges::find(
                           changed_signals, alias->signal)
                        != changed_signals.end()) {
                    return true;
                }
            }
        }
        if (selected < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[selected]
            && std::ranges::find(
                   changed_signals,
                   container_aggregate_signal_aliases[selected]->signal)
                != changed_signals.end()) {
            return true;
        }
        const auto& container = container_objects[selected];
        if (!container.slice_alias) {
            return false;
        }
        selected = container.slice_alias->object;
    }
    return false;
}

PreparedContainerReferenceRefresh
Interpreter::Impl::prepare_container_value_reference_refresh(
    const std::span<const SignalId> changed_signals)
{
    PreparedContainerReferenceRefresh prepared;
    if (changed_signals.empty() || exposed_container_value_references.empty()) {
        return prepared;
    }
    prepared.flat_aliases.reserve(
        exposed_container_value_references.size());
    for (const auto object : exposed_container_value_references) {
        if (object >= container_signal_aliases.size()
            || !container_signal_aliases[object]
            || !container_signal_aliases[object]->readable
            || !container_value_reference_depends_on_signals(
                object, changed_signals)) {
            continue;
        }
        prepared.flat_aliases.push_back(
            make_container_reference_refresh(object));
    }
    return prepared;
}

void Interpreter::Impl::begin_container_value_reference_refresh(
    ActiveContainerReferenceRefresh& frame,
    const std::span<const SignalId> changed_signals,
    PreparedContainerReferenceRefresh& prepared) noexcept
{
    frame.signals = changed_signals;
    frame.prepared = &prepared;
    frame.previous = active_container_reference_refresh;
    frame.needs_refresh = true;
    active_container_reference_refresh = &frame;
}

void Interpreter::Impl::end_container_value_reference_refresh(
    ActiveContainerReferenceRefresh& frame) noexcept
{
    if (active_container_reference_refresh != &frame) {
        std::terminate();
    }
    active_container_reference_refresh = frame.previous;
}

void Interpreter::Impl::prepare_active_container_value_reference_refresh(
    const ContainerObjectId object)
{
    if (object >= container_signal_aliases.size()
        || !container_signal_aliases[object]
        || !container_signal_aliases[object]->readable) {
        return;
    }
    struct PendingReservation {
        ActiveContainerReferenceRefresh* frame { };
        ContainerReferenceRefresh refresh;
    };
    std::vector<PendingReservation> pending;
    for (auto* frame = active_container_reference_refresh;
        frame != nullptr; frame = frame->previous) {
        if (!frame->needs_refresh || frame->prepared == nullptr
            || !container_value_reference_depends_on_signals(
                object, frame->signals)) {
            continue;
        }
        auto& refreshes = frame->prepared->flat_aliases;
        const auto existing = std::ranges::lower_bound(
            refreshes, object, { }, &ContainerReferenceRefresh::object);
        if (existing != refreshes.end() && existing->object == object) {
            continue;
        }
        pending.push_back({ frame,
            make_container_reference_refresh(object) });
    }

    // Build every refresh and secure every destination before publishing an
    // entry into any active frame. A failure here leaves only invisible vector
    // capacity changes; no frame can observe a partial reservation set.
    for (const auto& reservation : pending) {
        auto& refreshes = reservation.frame->prepared->flat_aliases;
        if (refreshes.size() == refreshes.max_size()) {
            throw std::length_error {
                "too many nested container reference refreshes"
            };
        }
        refreshes.reserve(refreshes.size() + 1U);
    }

    // ContainerReferenceRefresh moves are noexcept and the capacity is now
    // available, so this commit phase does not allocate or unwind halfway
    // through the active frame stack.
    for (auto& reservation : pending) {
        auto& refreshes = reservation.frame->prepared->flat_aliases;
        const auto position = std::ranges::lower_bound(
            refreshes,
            object,
            { },
            &ContainerReferenceRefresh::object);
        refreshes.insert(position, std::move(reservation.refresh));
    }
}

void Interpreter::Impl::synchronize_container_value_references(
    const std::span<const SignalId> changed_signals,
    PreparedContainerReferenceRefresh& prepared) noexcept
{
    if (active_container_reference_refresh == nullptr
        || active_container_reference_refresh->prepared != &prepared
        || active_container_reference_refresh->signals.data()
            != changed_signals.data()
        || active_container_reference_refresh->signals.size()
            != changed_signals.size()) {
        std::terminate();
    }
    active_container_reference_refresh->needs_refresh = false;
    const auto fill_flat_alias = [&](
                                ContainerObjectId object,
                                ContainerReferenceRefresh& refresh) noexcept {
        const auto& alias = container_signal_aliases[object];
        if (!alias || !alias->readable
            || refresh.object != object
            || refresh.leaves.empty()) {
            std::terminate();
        }
        const auto signal = alias->signal;
        const bool aggregate_proxy
            = signal < signal_container_aggregate_aliases.size()
            && signal_container_aggregate_aliases[signal].has_value();
        const auto& packed = signals[signal].initial_value;
        const std::vector<std::optional<ContainerElementSignalAlias>>*
            aggregate_elements { };
        std::size_t aggregate_width { };
        std::size_t aggregate_count { };
        if (aggregate_proxy) {
            const auto aggregate
                = *signal_container_aggregate_aliases[signal];
            if (aggregate >= container_element_signal_aliases.size()) {
                std::terminate();
            }
            aggregate_elements = &container_element_signal_aliases[aggregate];
            aggregate_width
                = get_container_object(aggregate).initial_value.type.element_width;
            aggregate_count = aggregate_elements->size();
            if (aggregate_width == 0U || aggregate_count == 0U
                || aggregate_count
                    > std::numeric_limits<std::size_t>::max()
                        / aggregate_width
                || aggregate_width * aggregate_count != packed.width()) {
                std::terminate();
            }
        }
        for (auto& leaf : refresh.leaves) {
            if (!aggregate_proxy) {
                if (leaf.packed_offset > packed.width()
                    || leaf.value.width()
                        > packed.width() - leaf.packed_offset) {
                    std::terminate();
                }
                for (std::size_t bit = 0U;
                    bit < leaf.value.width(); ++bit) {
                    leaf.value.set(
                        bit, packed.get(leaf.packed_offset + bit));
                }
                continue;
            }
            const auto aggregate_width_total = packed.width();
            if (leaf.packed_offset > aggregate_width_total
                || leaf.value.width()
                    > aggregate_width_total - leaf.packed_offset) {
                std::terminate();
            }
            for (std::size_t bit = 0U; bit < leaf.value.width(); ++bit) {
                const auto packed_bit = leaf.packed_offset + bit;
                const auto ordinal = aggregate_count - 1U
                    - packed_bit / aggregate_width;
                if (ordinal >= aggregate_elements->size()
                    || !(*aggregate_elements)[ordinal]) {
                    std::terminate();
                }
                const auto source_signal
                    = (*aggregate_elements)[ordinal]->signal;
                leaf.value.set(
                    bit,
                    signals[source_signal].initial_value.get(
                        packed_bit % aggregate_width));
            }
        }
        std::size_t leaf_index { };
        invalidate_container_aggregate_extract(object);
        auto& destination = container_objects[object].initial_value;
        visit_container_packed_leaves(
            destination, [&](auto& element) noexcept {
                if (leaf_index >= refresh.leaves.size()
                    || element.width()
                        != refresh.leaves[leaf_index].value.width()) {
                    std::terminate();
                }
                element = std::move(refresh.leaves[leaf_index].value);
                ++leaf_index;
            });
        if (leaf_index != refresh.leaves.size()) {
            std::terminate();
        }
        container_materialized_revisions[object]
            = signal_value_revisions[alias->signal];
    };

    std::size_t prepared_index { };
    for (const auto object : exposed_container_value_references) {
        if (!container_value_reference_depends_on_signals(
                object, changed_signals)) {
            continue;
        }
        invalidate_container_aggregate_extract(object);
        auto& container = container_objects[object];
        if (container.slice_alias) {
            const auto& alias = *container.slice_alias;
            const auto& source = container_objects[alias.object].initial_value;
            copy_container_slice_in_place(
                container.initial_value, source, alias);
            continue;
        }

        if (object < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[object]) {
            auto& destination = container.initial_value;
            const auto& aliases = container_element_signal_aliases[object];
            if (destination.elements.size() != aliases.size()) {
                std::terminate();
            }
            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                if (!aliases[ordinal]) {
                    std::terminate();
                }
                destination.elements[ordinal]
                    = signals[aliases[ordinal]->signal].initial_value;
            }
            continue;
        }

        if (object < container_element_signal_aliases.size()
            && std::ranges::any_of(
                container_element_signal_aliases[object],
                [](const auto& alias) { return alias.has_value(); })) {
            auto& destination = container.initial_value;
            const auto& aliases = container_element_signal_aliases[object];
            for (std::size_t ordinal = 0U;
                ordinal < aliases.size(); ++ordinal) {
                if (aliases[ordinal]
                    && std::ranges::find(
                           changed_signals, aliases[ordinal]->signal)
                        != changed_signals.end()) {
                    if (ordinal >= destination.elements.size()) {
                        std::terminate();
                    }
                    destination.elements[ordinal]
                        = signals[aliases[ordinal]->signal].initial_value;
                }
            }
            continue;
        }

        if (object < container_signal_aliases.size()
            && container_signal_aliases[object]
            && container_signal_aliases[object]->readable) {
            while (prepared_index < prepared.flat_aliases.size()
                && prepared.flat_aliases[prepared_index].object < object) {
                // A getter can reserve an independent projection for every
                // active publication frame, then fail before it advertises
                // the reference. Such a reservation is intentionally inert;
                // skip it while consuming the still-exposed higher-ID views.
                ++prepared_index;
            }
            if (prepared_index < prepared.flat_aliases.size()
                && prepared.flat_aliases[prepared_index].object == object) {
                fill_flat_alias(
                    object, prepared.flat_aliases[prepared_index++]);
            } else {
                std::terminate();
            }
        }
    }
}

void Interpreter::Impl::synchronize_container_value_references_from_object(
    const ContainerObjectId object) noexcept
{
    if (object >= container_objects.size()) {
        std::terminate();
    }
    // IDs are topological for slice aliases: each child references an earlier
    // source. Updating in ID order therefore refreshes nested views without
    // replacing any ContainerValue or vector storage.
    for (const auto exposed : exposed_container_value_references) {
        if (exposed <= object || exposed >= container_objects.size()) {
            continue;
        }
        auto selected = exposed;
        bool descends_from_object { };
        for (std::size_t depth = 0U; depth < container_objects.size(); ++depth) {
            if (selected >= container_objects.size()) {
                std::terminate();
            }
            const auto& view = container_objects[selected];
            if (!view.slice_alias) {
                break;
            }
            if (view.slice_alias->object == object) {
                descends_from_object = true;
                break;
            }
            selected = view.slice_alias->object;
        }
        if (!descends_from_object) {
            continue;
        }
        const auto& view = container_objects[exposed];
        if (!view.slice_alias) {
            std::terminate();
        }
        const auto& source
            = container_objects[view.slice_alias->object].initial_value;
        copy_container_slice_in_place(
            container_objects[exposed].initial_value,
            source,
            *view.slice_alias);
    }
}

void Interpreter::Impl::prepare_container_value_reference_object_update(
    const ContainerObjectId object,
    const ContainerValue& replacement)
{
    if (object >= container_objects.size()
        || replacement.type != container_objects[object].initial_value.type) {
        throw std::invalid_argument {
            "retained container update has an incompatible object type"
        };
    }
    const auto maximum_string_length
        = maximum_container_string_length(replacement);
    const auto reserve_strings = [maximum_string_length](auto& value) {
        visit_container_strings(value, [&](std::string& text) {
            if (text.capacity() < maximum_string_length) {
                text.reserve(maximum_string_length);
            }
        });
    };
    reserve_strings(container_objects[object].initial_value);
    for (const auto exposed : exposed_container_value_references) {
        if (exposed <= object || exposed >= container_objects.size()) {
            continue;
        }
        auto selected = exposed;
        bool descends_from_object { };
        for (std::size_t depth = 0U; depth < container_objects.size(); ++depth) {
            const auto& view = container_objects[selected];
            if (!view.slice_alias) {
                break;
            }
            if (view.slice_alias->object == object) {
                descends_from_object = true;
                break;
            }
            selected = view.slice_alias->object;
        }
        if (descends_from_object) {
            reserve_strings(container_objects[exposed].initial_value);
        }
    }
}

const ContainerValue&
Interpreter::Impl::read_container_object_value(
    const ContainerObjectId id)
{
    require_container_signal_role_materialized(*this, id);
    auto& object = get_container_object(id);
    if (id < container_value_reference_exposed.size()
        && container_value_reference_exposed[id] != 0U) {
        return object.initial_value;
    }
    if (!object.slice_alias) {
        if (id < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[id]
            && container_alias_authority_active(id)) {
            // Leaf changes advance the container's aggregate revision; while
            // it is unchanged the extracted elements are already current.
            if (container_aggregate_extract_revisions.size()
                != container_objects.size()) {
                container_aggregate_extract_revisions.assign(
                    container_objects.size(), no_container_extract_revision);
            }
            const auto revision = container_aggregate_current_revisions.at(id);
            if (container_aggregate_extract_revisions[id] == revision) {
                return object.initial_value;
            }
            const auto proxy = container_aggregate_signal_aliases[id]->signal;
            const auto& packed = aggregate_signal_current_value(proxy);
            const auto width = object.initial_value.type.element_width;
            const auto count = object.initial_value.elements.size();
            for (std::size_t ordinal = 0U; ordinal < count; ++ordinal) {
                object.initial_value.elements[ordinal] = extract_value(
                    packed, (count - ordinal - 1U) * width, width);
            }
            container_aggregate_extract_revisions[id] = revision;
            return object.initial_value;
        }
        const auto& element_aliases = container_element_signal_aliases.at(id);
        if (std::ranges::any_of(
                element_aliases,
                [](const auto& alias) { return alias.has_value(); })) {
            for (std::size_t ordinal = 0U;
                ordinal < element_aliases.size(); ++ordinal) {
                const auto& alias = element_aliases[ordinal];
                if (!alias) {
                    continue;
                }
                if (!alias->readable) {
                    throw std::logic_error {
                        "a SimIR container element alias is unreadable"
                    };
                }
                object.initial_value.elements[ordinal]
                    = get_signal(alias->signal).initial_value;
            }
        }
        const auto& alias = container_signal_aliases.at(id);
        if (alias && alias->readable) {
            const auto revision = signal_value_revisions.at(alias->signal);
            if (container_materialized_revisions.at(id) != revision) {
                unpack_container_signal_value(
                    object.initial_value,
                    get_signal(alias->signal).initial_value);
                container_materialized_revisions[id] = revision;
            }
        }
        return object.initial_value;
    }
    const auto& alias = *object.slice_alias;
    const auto& source = read_container_object_value(alias.object);
    const auto count = object.initial_value.elements.size();
    const auto descending = alias.selected_left >= alias.selected_right;
    for (std::size_t ordinal = 0;
        ordinal < count;
        ++ordinal) {
        const auto selected_index = static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                    ? -static_cast<std::int64_t>(ordinal)
                    : static_cast<std::int64_t>(ordinal)));
        object.initial_value.elements[ordinal] = source.elements[fixed_offset(source.type, selected_index)];
    }
    return object.initial_value;
}

bool Interpreter::Impl::read_container_object_element(
    const ContainerObjectId id,
    const std::size_t ordinal,
    PackedLogic4& result)
{
    require_container_signal_role_materialized(*this, id);
    const auto& object = get_container_object(id);
    const auto& value = object.initial_value;
    if ((value.type.element_kind != ContainerElementKind::Packed
            && value.type.element_kind != ContainerElementKind::Scalar)
        || ordinal >= value.elements.size()) {
        return false;
    }
    if (id < container_value_reference_exposed.size()
        && container_value_reference_exposed[id] != 0U) {
        // As in read_container_object_value: the exposed value is current.
        result = value.elements[ordinal];
        return true;
    }
    if (object.slice_alias) {
        const auto& alias = *object.slice_alias;
        const auto descending = alias.selected_left >= alias.selected_right;
        const auto selected_index = static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                    ? -static_cast<std::int64_t>(ordinal)
                    : static_cast<std::int64_t>(ordinal)));
        const auto& source = get_container_object(alias.object).initial_value;
        return read_container_object_element(
            alias.object,
            fixed_offset(source.type, selected_index),
            result);
    }
    if (id < container_aggregate_signal_aliases.size()
        && container_aggregate_signal_aliases[id]
        && container_alias_authority_active(id)) {
        const auto proxy = container_aggregate_signal_aliases[id]->signal;
        const auto& packed = aggregate_signal_current_value(proxy);
        const auto width = value.type.element_width;
        const auto count = value.elements.size();
        result = extract_value(
            packed, (count - ordinal - 1U) * width, width);
        return true;
    }
    const auto& element_aliases = container_element_signal_aliases.at(id);
    if (ordinal < element_aliases.size() && element_aliases[ordinal]) {
        if (!element_aliases[ordinal]->readable) {
            return false;
        }
        result = get_signal(element_aliases[ordinal]->signal).initial_value;
        return true;
    }
    const auto& alias = container_signal_aliases.at(id);
    if (!alias || !alias->readable) {
        result = value.elements[ordinal];
        return true;
    }
    const auto& packed = get_signal(alias->signal).initial_value;
    const auto width = value.type.element_width;
    if (width == 0 || ordinal >= packed.width() / width
        || packed.width() % width != 0) {
        return false;
    }
    result = extract_value(
        packed,
        packed.width() - (ordinal + 1U) * width,
        width);
    return true;
}

bool Interpreter::Impl::can_stage_container_alias_deposit(
    const ContainerObjectId object) const
{
    if (object >= container_aggregate_signal_aliases.size()
        || !container_aggregate_signal_aliases[object]
        || !container_aggregate_signal_aliases[object]->writable
        || has_bidirectional_switches || !module_paths.empty()
        || !module_timing_checks.empty()) {
        return false;
    }
    const auto& aliases = container_element_signal_aliases.at(object);
    if (aliases.empty()) {
        return false;
    }
    return std::ranges::all_of(aliases, [&](const auto& alias) {
        if (!alias || !alias->readable || !alias->writable) {
            return false;
        }
        const auto& signal = signals.at(alias->signal);
        return signal.resolution == ResolutionKind::sv_wire
            && signal.value_kind == ValueKind::logic4
            && !signal.event_variable && !signal.has_charge_strength;
    });
}

std::map<SignalId, std::vector<Process::DriverRegion>>
Interpreter::Impl::expanded_driver_regions(
    const ProcessProgramView& process) const
{
    std::vector<Process::DriverRegion> requested_regions;
    if (process.driver_regions().empty()) {
        // Keep inference allocation-equivalent to the Process path: inspect
        // the stored/overridden operation and remap only its output ID.
        for (std::size_t index = 0U;
             index < process.operations().size(); ++index) {
            const auto& operation = process.operations()[index];
            if (const auto signal = output_signal(operation)) {
                requested_regions.push_back({
                    process.operations().signal(*signal), 0U, 0U, true });
            }
        }
    } else {
        requested_regions = process.driver_regions();
    }

    std::map<SignalId, std::vector<Process::DriverRegion>> expanded;
    for (const auto& region : requested_regions) {
        if (region.signal >= signals.size()) {
            throw std::invalid_argument {
                "process output references invalid signal"
            };
        }

        const auto signal_width
            = signals[region.signal].initial_value.width();
        if (!region.whole
            && (region.width == 0U
                || region.offset > signal_width
                || region.width > signal_width - region.offset)) {
            throw std::invalid_argument {
                "SimIR driver region is outside its signal"
            };
        }

        if (region.signal >= signal_container_aggregate_aliases.size()
            || !signal_container_aggregate_aliases[region.signal]
            || process.switch_bidirectional() || process.switch_source()
            || process.switch_target()) {
            expanded[region.signal].push_back(region);
            continue;
        }

        const auto object
            = *signal_container_aggregate_aliases[region.signal];
        const auto& aggregate_alias
            = container_aggregate_signal_aliases.at(object);
        if (!aggregate_alias || aggregate_alias->signal != region.signal) {
            throw std::logic_error {
                "aggregate signal reverse alias is inconsistent"
            };
        }
        if (!aggregate_alias->writable) {
            throw std::invalid_argument {
                "a process cannot drive an observation-only aggregate alias"
            };
        }

        if (!can_stage_container_alias_deposit(object)) {
            expanded[region.signal].push_back(region);
            continue;
        }

        const auto& container = get_container_object(object).initial_value;
        const auto& type = container.type;
        const auto& aliases = container_element_signal_aliases.at(object);
        const auto proxy_width = container_signal_bridge_width(type);
        if (!type.fixed || type.dimensions.empty()
            || type.element_kind != ContainerElementKind::Packed
            || type.element_width == 0U || aliases.empty()
            || aliases.size() != container.elements.size()
            || !proxy_width || *proxy_width != signal_width) {
            throw std::invalid_argument {
                "aggregate driver region has an unsupported container shape"
            };
        }
        for (const auto& alias : aliases) {
            if (!alias || !alias->readable || !alias->writable
                || alias->signal >= signals.size()
                || signals[alias->signal].initial_value.width()
                    != type.element_width) {
                throw std::invalid_argument {
                    "aggregate driver region has an incomplete leaf map"
                };
            }
        }

        const auto range_begin = region.whole
            ? std::size_t { 0U }
            : static_cast<std::size_t>(region.offset);
        const auto range_end = region.whole
            ? signal_width
            : range_begin + static_cast<std::size_t>(region.width);
        for (std::size_t ordinal = 0U;
            ordinal < aliases.size(); ++ordinal) {
            const auto lane_begin
                = (aliases.size() - ordinal - 1U) * type.element_width;
            const auto lane_end = lane_begin + type.element_width;
            const auto intersection_begin = std::max(range_begin, lane_begin);
            const auto intersection_end = std::min(range_end, lane_end);
            if (intersection_begin >= intersection_end) {
                continue;
            }

            const auto leaf_offset = intersection_begin - lane_begin;
            const auto leaf_width = intersection_end - intersection_begin;
            const bool whole_leaf
                = leaf_offset == 0U && leaf_width == type.element_width;
            expanded[aliases[ordinal]->signal].push_back({
                aliases[ordinal]->signal,
                whole_leaf ? 0U : static_cast<std::uint32_t>(leaf_offset),
                whole_leaf ? 0U : static_cast<std::uint32_t>(leaf_width),
                whole_leaf
            });
        }
    }
    return expanded;
}

void Interpreter::Impl::write_container_alias_deposit(
    const ContainerObjectId object,
    const std::vector<PackedLogic4>& values,
    const SignalChangeOrigin origin)
{
    publish_container_alias_family(object, values, origin, { });
}

void Interpreter::Impl::publish_container_alias_family(
    const ContainerObjectId object,
    const std::vector<PackedLogic4>& values,
    const SignalChangeOrigin origin,
    const std::span<ContainerAliasForceUpdate> force_updates,
    const std::span<const std::uint8_t> selected_leaves,
    const std::span<ContainerAliasDriverForceUpdate> driver_force_updates)
{
    struct PlannedLeaf {
        SignalId signal { };
        std::size_t ordinal { };
        PackedLogic4 stored_value;
        PackedLogic4 previous;
        PackedLogic4 current;
        std::size_t current_plane_word_offset { };
        std::size_t current_plane_word_count { };
        bool stored_changed { };
        bool current_changed { };
        bool selected { true };
    };

    const auto& aliases = container_element_signal_aliases.at(object);
    const bool forced_current = !force_updates.empty();
    const bool driver_force_publication = !driver_force_updates.empty();
    if (values.size() != aliases.size()
        || (forced_current && force_updates.size() != aliases.size())
        || (driver_force_publication
            && (driver_force_updates.size() != aliases.size()
                || forced_current))
        || (!selected_leaves.empty()
            && selected_leaves.size() != aliases.size())) {
        throw std::invalid_argument {
            "aggregate deposit does not match the element alias count"
        };
    }

    const auto proxy = container_aggregate_signal_aliases.at(object)->signal;
    std::vector<SignalId> family_signals;
    family_signals.reserve(aliases.size() + 1U);
    for (const auto& alias : aliases) {
        if (!alias) {
            throw std::logic_error {
                "aggregate alias family lost an element binding"
            };
        }
        family_signals.push_back(alias->signal);
    }
    family_signals.push_back(proxy);
    auto container_reference_refresh
        = prepare_container_value_reference_refresh(family_signals);
    ActiveContainerReferenceRefresh active_refresh;
    begin_container_value_reference_refresh(
        active_refresh, family_signals, container_reference_refresh);
    const auto finish_refresh = [this](
                                    ActiveContainerReferenceRefresh* frame) noexcept {
        end_container_value_reference_refresh(*frame);
    };
    const std::unique_ptr<ActiveContainerReferenceRefresh,
        decltype(finish_refresh)> refresh_scope(
        &active_refresh, finish_refresh);

    std::vector<PlannedLeaf> plan;
    plan.reserve(aliases.size());
    std::vector<std::uint64_t> current_plane_scratch;
    const auto maximum_plane_word_count
        = current_plane_scratch.max_size() / 2U;
    std::size_t total_plane_word_count { };
    bool any_stored_change { };
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        const auto signal = aliases[ordinal]->signal;
        const bool selected
            = (selected_leaves.empty() || selected_leaves[ordinal] != 0U)
            && (!forced_current || force_updates[ordinal].active)
            && (!driver_force_publication
                || driver_force_updates[ordinal].active);
        auto input = normalize_signal_value(signal, values[ordinal]);
        const bool stored_changed
            = selected && !forced_current
            && driven_values[signal] != input;
        auto previous = driver_force_publication
            ? signals[signal].initial_value : PackedLogic4 { };
        auto current = selected
            ? (driver_force_publication
                    ? driver_force_updates[ordinal].effective_current
                    : input)
            : driver_force_publication
                ? previous : signals[signal].initial_value;
        auto stored_value = selected ? input : driven_values[signal];
        const auto width
            = static_cast<std::size_t>(signals[signal].initial_value.width());
        const auto plane_words
            = width / 64U + (width % 64U != 0U ? 1U : 0U);
        if (plane_words > maximum_plane_word_count - total_plane_word_count) {
            throw std::length_error {
                "aggregate alias family exceeds direct plane scratch capacity"
            };
        }
        plan.push_back({
            signal, ordinal, std::move(stored_value), std::move(previous),
            std::move(current),
            total_plane_word_count, plane_words, stored_changed, false, selected });
        total_plane_word_count += plane_words;
        any_stored_change = any_stored_change || stored_changed;
    }
    // Keep this invocation's zero-normalized A/B plane words together. The
    // bit conversion below leaves unused tail bits zero, while local ownership
    // keeps synchronous observer reentry from sharing or overwriting scratch.
    current_plane_scratch.resize(total_plane_word_count * 2U);
    const auto current_plane_words
        = std::span<std::uint64_t> { current_plane_scratch };

    std::optional<PackedLogic4> next_stored_projection;
    if (any_stored_change) {
        next_stored_projection.emplace(
            aggregate_signal_stored_projection.at(proxy)->width(),
            Logic4::zero);
        const auto element_width
            = get_container_object(object).initial_value.type.element_width;
        for (const auto& leaf : plan) {
            const auto offset
                = (aliases.size() - leaf.ordinal - 1U) * element_width;
            next_stored_projection->insert_bits(leaf.stored_value, offset);
        }
    }

    // Driver-force publication has no stored-hook phase. Allocate both family
    // projections before installing the per-owner maps or resolved stored
    // values. Transaction callbacks may reenter and change unselected leaves;
    // the unique fixed-width projection buffers are updated in place afterward.
    std::optional<PackedLogic4> next_current_projection;
    std::optional<PackedLogic4> previous_projection;
    if (driver_force_publication) {
        const auto projection_width
            = aggregate_signal_current_projection.at(proxy)->width();
        next_current_projection.emplace(projection_width, Logic4::zero);
        previous_projection.emplace(projection_width, Logic4::zero);
        const auto element_width
            = get_container_object(object).initial_value.type.element_width;
        for (const auto& leaf : plan) {
            const auto offset
                = (aliases.size() - leaf.ordinal - 1U) * element_width;
            next_current_projection->insert_bits(leaf.current, offset);
            previous_projection->insert_bits(leaf.previous, offset);
        }
    }

    begin_container_alias_write_batch(object, false, true);
    ScopedContainerAliasWriteFrame container_frame(
        container_alias_write_batches.at(object));
    ScopedAggregateSignalBatch aggregate_frame(
        aggregate_signal_batches.at(proxy));
    begin_aggregate_signal_batch(proxy, origin);

    std::exception_ptr observer_failure;
    const auto capture_failure = [&observer_failure](auto&& callback) {
        try {
            callback();
        } catch (...) {
            if (!observer_failure) {
                observer_failure = std::current_exception();
            }
        }
    };
    const auto finish_batches = [&]() noexcept {
        capture_failure([&] {
            capture_container_alias_write_batch(object);
        });
        bool container_changed { };
        std::vector<ContainerAliasLeafObserver> leaves;
        capture_failure([&] {
            leaves = take_container_alias_write_batch(
                object, container_changed);
        });
        capture_failure([&] {
            finish_aggregate_signal_batch(proxy, true);
        });
        capture_failure([&] {
            finish_container_alias_write_batch(
                object, std::move(leaves), container_changed);
        });
    };

    try {
        if (forced_current || driver_force_publication) {
            if (started && systemverilog_region_kernel_enabled) {
                // Force masks and per-owner force maps participate in graph
                // admission. A family publication therefore requires a rebuilt
                // snapshot, even if none of its resolved values change.
                request_full_region_recertification();
            }
            for (const auto& alias : aliases) {
                if (alias
                    && alias->signal
                        < region_authoritative_component_by_signal.size()) {
                    demote_region_authoritative_slots(
                        region_authoritative_component_by_signal[
                            alias->signal], false);
                }
            }
            if (proxy < region_authoritative_component_by_signal.size()) {
                demote_region_authoritative_slots(
                    region_authoritative_component_by_signal[proxy], false);
            }
            invalidate_fused_static_cohorts();
        }
        prepare_region_authoritative_family_write(object);

        if (any_stored_change) {
            for (auto& leaf : plan) {
                if (leaf.stored_changed) {
                    driven_values[leaf.signal] = std::move(leaf.stored_value);
                }
            }

            auto& revision = container_aggregate_stored_revisions[object];
            for (const auto& leaf : plan) {
                if (!leaf.stored_changed) {
                    continue;
                }
                if (revision == std::numeric_limits<std::uint64_t>::max()) {
                    revision = 1U;
                    aggregate_signal_stored_projection_revisions[proxy] = 0U;
                } else {
                    ++revision;
                }
            }
            *aggregate_signal_stored_projection[proxy]
                = std::move(*next_stored_projection);
            aggregate_signal_stored_projection_revisions[proxy] = revision;

            auto& aggregate_batch = aggregate_signal_batches[proxy];
            aggregate_batch.stored_changed = true;
            aggregate_batch.origin = origin;
            aggregate_batch.stored_observer_notified
                = !driver_force_publication
                && static_cast<bool>(stored_signal_change_hook);
        }

        // The per-owner force maps and their resolved stored values form one
        // stored-state transition. The complete current projection buffers were
        // allocated above, so no later preparation allocation can leave these
        // maps/stored values installed without the matching current family.
        if (driver_force_publication) {
            for (const auto& leaf : plan) {
                auto& update = driver_force_updates[leaf.ordinal];
                if (!leaf.selected || !update.active) {
                    continue;
                }
                forced_driver_values[leaf.signal]
                    = std::move(update.values);
                forced_driver_masks[leaf.signal]
                    = std::move(update.masks);
                direct_single_driver_routes[leaf.signal] = { };
                direct_single_driver_processes[leaf.signal]
                    = std::numeric_limits<ProcessId>::max();
            }
        }

        if (!driver_force_publication) {
            for (const auto& leaf : plan) {
                if (leaf.stored_changed && stored_signal_change_hook) {
                    capture_failure([&] {
                        stored_signal_change_hook(
                            leaf.signal, scheduler.now());
                    });
                }
            }
            if (any_stored_change && stored_signal_change_hook) {
                capture_failure([&] {
                    stored_signal_change_hook(proxy, scheduler.now());
                });
            }
        }

        bool current_stage_ready { };
        try {
            // Capture the effective outer input after stored observers, as in
            // commit_resolved. Transaction traces may change force state later;
            // they must not replace this operation's already captured value.
            for (auto& leaf : plan) {
                if (!leaf.selected) {
                    continue;
                }
                if (!forced_current && !driver_force_publication) {
                    leaf.current = apply_force(
                        leaf.signal, std::move(leaf.current));
                }
                auto current_aval = current_plane_words.subspan(
                    leaf.current_plane_word_offset,
                    leaf.current_plane_word_count);
                auto current_bval = current_plane_words.subspan(
                    total_plane_word_count + leaf.current_plane_word_offset,
                    leaf.current_plane_word_count);
                const auto aval_words = leaf.current.aval_words();
                const auto bval_words = leaf.current.bval_words();
                if (!leaf.current.is_logic9()
                    && aval_words.size() == current_aval.size()
                    && bval_words.size() == current_bval.size()) {
                    std::ranges::copy(aval_words, current_aval.begin());
                    std::ranges::copy(bval_words, current_bval.begin());
                    const auto remaining_bits
                        = leaf.current.width() % 64U;
                    if (remaining_bits != 0U) {
                        const auto last_word_mask
                            = (UINT64_C(1) << remaining_bits) - 1U;
                        current_aval.back() &= last_word_mask;
                        current_bval.back() &= last_word_mask;
                    }
                    continue;
                }

                std::ranges::fill(current_aval, 0U);
                std::ranges::fill(current_bval, 0U);
                for (std::size_t bit = 0U;
                    bit < leaf.current.width(); ++bit) {
                    const auto logic = leaf.current.get(bit);
                    const auto word = bit / 64U;
                    const auto mask = UINT64_C(1) << (bit % 64U);
                    if (logic == Logic4::one || logic == Logic4::x) {
                        current_aval[word] |= mask;
                    }
                    if (logic == Logic4::x || logic == Logic4::z) {
                        current_bval[word] |= mask;
                    }
                }
            }

            // Other publication modes may have stored observers that change
            // force state. Their effective current values must therefore be
            // prepared after those observers but before force-mask install.
            // Driver-force mode prepared the same fixed-width buffers before
            // any family state was installed.
            if (!driver_force_publication) {
                const auto projection_width
                    = aggregate_signal_current_projection.at(proxy)->width();
                next_current_projection.emplace(
                    projection_width, Logic4::zero);
                previous_projection.emplace(
                    projection_width, Logic4::zero);
            }
            const auto element_width
                = get_container_object(object).initial_value.type.element_width;
            for (const auto& leaf : plan) {
                const auto offset
                    = (aliases.size() - leaf.ordinal - 1U) * element_width;
                next_current_projection->insert_bits(leaf.current, offset);
            }

            // Force masks become authoritative only after allocating current
            // and projection preparation completes. Installing all selected
            // masks invokes no observers; current publication follows traces.
            if (forced_current) {
                for (const auto& leaf : plan) {
                    auto& update = force_updates[leaf.ordinal];
                    if (!leaf.selected || !update.active) {
                        continue;
                    }
                    forced_values[leaf.signal] = std::move(update.value);
                    forced_masks[leaf.signal] = std::move(update.mask);
                    direct_single_driver_routes[leaf.signal] = { };
                    direct_single_driver_processes[leaf.signal]
                        = std::numeric_limits<ProcessId>::max();
                }
            }
            // A transaction precedes its current-value publication, including
            // synchronous trace reentry, just as in publish_normalized. All
            // selected transaction stamps describe this logical operation, while
            // current/LAST/event state still describes the preceding value.
            const auto transaction_stamp
                = std::pair { scheduler.now(), scheduler.delta() + 1U };
            bool any_transaction { };
            for (const auto& leaf : plan) {
                if (leaf.selected
                    && (!forced_current
                        || force_updates[leaf.ordinal].active)) {
                    signal_transactions.at(leaf.signal) = transaction_stamp;
                    any_transaction = true;
                }
            }
            if (any_transaction) {
                signal_transactions.at(proxy) = transaction_stamp;
            }
            for (const auto& leaf : plan) {
                if (!leaf.selected
                    || (forced_current
                        && !force_updates[leaf.ordinal].active)) {
                    continue;
                }
                capture_failure([&] {
                    note_signal_transaction(leaf.signal, true, origin);
                });
            }
            if (any_transaction) {
                // Leaf transactions normally defer the proxy to batch finish.
                // Emit it in this same pre-current phase, exactly once.
                aggregate_signal_batches[proxy].transaction_changed = false;
                capture_failure([&] {
                    note_signal_transaction(proxy, true, origin);
                });
            }

            // Reentry may have published another coherent family. LAST and the
            // change predicate must use that live value, while selected current
            // inputs remain the captured outer operation. Unselected leaves keep
            // their live current independently of stored state.
            for (auto& leaf : plan) {
                leaf.previous = signals[leaf.signal].initial_value;
                if (!leaf.selected) {
                    leaf.current = leaf.previous;
                }
                leaf.current_changed
                    = leaf.selected && leaf.previous != leaf.current;
                const auto offset
                    = (aliases.size() - leaf.ordinal - 1U) * element_width;
                previous_projection->insert_bits(leaf.previous, offset);
                next_current_projection->insert_bits(leaf.current, offset);
            }
            const bool any_current_change
                = std::ranges::any_of(plan, [](const PlannedLeaf& leaf) {
                      return leaf.current_changed;
                  });
            if (any_current_change) {
                aggregate_signal_batches[proxy].previous.emplace(
                    std::move(*previous_projection));
            }

            // Install every current value, LAST value and direct plane before
            // recording events or invoking current-value observers.
            for (auto& leaf : plan) {
                if (!leaf.current_changed) {
                    continue;
                }
                if (leaf.previous.width() <= 64U) {
                    const auto previous_word
                        = leaf.previous.unchecked_low_word();
                    direct_signal_last_aval[leaf.signal] = previous_word.aval;
                    direct_signal_last_bval[leaf.signal] = previous_word.bval;
                }
                signal_last_values[leaf.signal] = std::move(leaf.previous);
                signals[leaf.signal].initial_value = std::move(leaf.current);
            }
            for (const auto& leaf : plan) {
                if (!leaf.current_changed) {
                    continue;
                }
                auto& revision = signal_value_revisions[leaf.signal];
                if (revision == std::numeric_limits<std::uint64_t>::max()) {
                    revision = 1U;
                    std::ranges::fill(
                        container_materialized_revisions, std::nullopt);
                } else {
                    ++revision;
                }

                auto& family_revision
                    = container_aggregate_current_revisions[object];
                if (family_revision
                    == std::numeric_limits<std::uint64_t>::max()) {
                    family_revision = 1U;
                    aggregate_signal_current_projection_revisions[proxy] = 0U;
                    aggregate_signal_last_projection_revisions[proxy] = 0U;
                } else {
                    ++family_revision;
                }

                const auto width = signals[leaf.signal].initial_value.width();
                if (width <= 64U) {
                    const auto word
                        = signals[leaf.signal].initial_value.unchecked_low_word();
                    direct_signal_aval[leaf.signal] = word.aval;
                    direct_signal_bval[leaf.signal] = word.bval;
                }
                const auto offset = direct_wide_signal_offsets[leaf.signal];
                const auto current_aval = current_plane_words.subspan(
                    leaf.current_plane_word_offset,
                    leaf.current_plane_word_count);
                const auto current_bval = current_plane_words.subspan(
                    total_plane_word_count + leaf.current_plane_word_offset,
                    leaf.current_plane_word_count);
                std::ranges::copy(
                    current_aval,
                    direct_wide_signal_aval.begin() + offset);
                std::ranges::copy(
                    current_bval,
                    direct_wide_signal_bval.begin() + offset);
            }

            if (any_current_change) {
                aggregate_signal_batches[proxy].changed = true;
                *aggregate_signal_current_projection[proxy]
                    = std::move(*next_current_projection);
                const auto revision = container_aggregate_current_revisions[object];
                aggregate_signal_current_projection_revisions[proxy] = revision;
                aggregate_signal_last_projection_revisions[proxy] = revision;
            }
            synchronize_container_value_references(
                family_signals,
                container_reference_refresh);
            // Set only after every leaf, projection, and direct plane is ready
            // for event and current-observer callbacks.
            current_stage_ready = true;
        } catch (...) {
            const auto failure = std::current_exception();
            if (!observer_failure) {
                observer_failure = failure;
            }
        }

        if (current_stage_ready) {
            const auto transaction_delta = scheduler.delta() + 1U;
            const bool any_current_change
                = std::ranges::any_of(plan, [](const PlannedLeaf& leaf) {
                      return leaf.current_changed;
                  });
            if (any_current_change) {
                // Event traces and current observers see every selected event
                // stamp alongside the complete newly installed current family.
                for (const auto& leaf : plan) {
                    if (leaf.current_changed) {
                        stamp_signal_event(
                            leaf.signal, transaction_delta, origin);
                    }
                }
                stamp_signal_event(proxy, transaction_delta, origin);
                aggregate_signal_batches[proxy].event_prepared = true;
                capture_failure([&] {
                    capture_sampled_history_clock(
                        proxy, transaction_delta, origin);
                });
            }

            // Capture clocks after the complete current/LAST install and before
            // event traces or current observers can reenter this family.
            for (const auto& leaf : plan) {
                if (leaf.current_changed) {
                    capture_failure([&] {
                        capture_sampled_history_clock(
                            leaf.signal, transaction_delta, origin);
                    });
                }
            }

            for (const auto& leaf : plan) {
                capture_failure([&] {
                    if (leaf.current_changed) {
                        publish_value_change(
                            leaf.signal, true, origin, true);
                    }
                    if (leaf.stored_changed) {
                        publish_container_signal_aliases(
                            leaf.signal, origin);
                    } else if (forced_current && leaf.current_changed) {
                        publish_container_signal_aliases(
                            leaf.signal, origin, false);
                    }
                });
            }
            if (forced_current && any_current_change) {
                // Current-only mutations must wake readers of the logical
                // container even though no stored-value notification occurs.
                for (const auto process : container_dynamic_fanout.at(object)) {
                    capture_failure([&] {
                        queue_next_delta(process, origin);
                    });
                }
            }
        }
    } catch (...) {
        if (!observer_failure) {
            observer_failure = std::current_exception();
        }
    }

    finish_batches();
    if (observer_failure) {
        std::rethrow_exception(observer_failure);
    }
}

PreparedContainerAliasDriverFamily
Interpreter::Impl::prepare_container_alias_driver_family(
    const ContainerObjectId object,
    const ProcessId process,
    const std::vector<PackedLogic4>& values,
    const SignalChangeOrigin origin,
    const std::span<const std::uint8_t> selected_leaves)
{
    if (!can_stage_container_alias_deposit(object)) {
        throw std::logic_error {
            "whole-container driver writes require a complete, "
            "non-cascading element map"
        };
    }
    const auto& program = processes.program_view(process);
    if (program.switch_source()) {
        throw std::logic_error {
            "whole-container driver writes do not stage switch processes"
        };
    }

    const auto& aliases = container_element_signal_aliases.at(object);
    if (values.size() != aliases.size()
        || (!selected_leaves.empty()
            && selected_leaves.size() != aliases.size())) {
        throw std::invalid_argument {
            "aggregate driver write does not match the element alias count"
        };
    }

    PreparedContainerAliasDriverFamily prepared;
    prepared.object = object;
    prepared.process = process;
    prepared.origin = origin;
    prepared.leaves.reserve(aliases.size());
    std::vector<PackedLogic4> normalized_values;
    normalized_values.reserve(aliases.size());
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        const auto signal = aliases[ordinal]->signal;
        normalized_values.push_back(
            normalize_signal_value(signal, values[ordinal]));
    }

    prepare_region_authoritative_family_write(object);
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        if (!selected_leaves.empty() && selected_leaves[ordinal] == 0U) {
            continue;
        }
        const auto& alias = aliases[ordinal];
        const auto signal = alias->signal;
        demote_owned_driver(signal);
        materialize_direct_signal(signal);
    }

    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        const auto signal = aliases[ordinal]->signal;
        const bool selected = selected_leaves.empty()
            || selected_leaves[ordinal] != 0U;
        if (!selected) {
            prepared.leaves.push_back({
                signal, { }, false, false, false });
            continue;
        }
        auto replacement = driver_values.at(signal);
        auto* record = replacement.find(process);
        bool inserted { };
        if (record == nullptr) {
            inserted = replacement.insert_if_absent(DriverRecord {
                process,
                initial_driver_value(signal),
                program.drive_strength()
            });
            record = replacement.find(process);
        }
        if (record == nullptr) {
            throw std::logic_error {
                "prepared driver table did not retain its SimIR process"
            };
        }

        const bool changed = record->value != normalized_values[ordinal];
        record->value = std::move(normalized_values[ordinal]);
        prepared.leaves.push_back({
            signal, std::move(replacement), selected, inserted, changed
        });
    }
    return prepared;
}

void Interpreter::Impl::begin_container_alias_driver_family(
    PreparedContainerAliasDriverFamily& prepared)
{
    if (prepared.raw_phase_begun || prepared.installed) {
        throw std::logic_error {
            "container driver family raw phase already began"
        };
    }
    if (prepared.object >= container_aggregate_signal_aliases.size()
        || !container_aggregate_signal_aliases[prepared.object]) {
        throw std::logic_error {
            "container driver family has no aggregate signal proxy"
        };
    }

    const auto proxy
        = container_aggregate_signal_aliases[prepared.object]->signal;
    auto& batch = aggregate_signal_batches[proxy];
    if (batch.depth != 0U) {
        prepared.suspended_aggregate_batch.emplace(std::move(batch));
        batch = { };
    }
    try {
        begin_aggregate_signal_batch(proxy, prepared.origin);
    } catch (...) {
        batch = { };
        if (prepared.suspended_aggregate_batch) {
            batch = std::move(*prepared.suspended_aggregate_batch);
            prepared.suspended_aggregate_batch.reset();
        }
        throw;
    }
    prepared.aggregate_batch_open = true;
    prepared.raw_phase_begun = true;
}

void Interpreter::Impl::install_container_alias_driver_family(
    PreparedContainerAliasDriverFamily& prepared) noexcept
{
    prepare_region_authoritative_family_write(prepared.object);
    for (auto& leaf : prepared.leaves) {
        if (!leaf.selected) {
            continue;
        }
        driver_values[leaf.signal] = std::move(leaf.replacement);
    }
    for (const auto& leaf : prepared.leaves) {
        if (!leaf.selected || !leaf.inserted) {
            continue;
        }
        direct_single_driver_routes[leaf.signal] = { };
        direct_single_driver_processes[leaf.signal]
            = std::numeric_limits<ProcessId>::max();
    }
    prepared.installed = true;
}

void Interpreter::Impl::notify_container_alias_driver_family_raw(
    PreparedContainerAliasDriverFamily& prepared) noexcept
{
    if (!prepared.installed || !prepared.raw_phase_begun
        || prepared.raw_notifications_complete) {
        return;
    }

    for (const auto& leaf : prepared.leaves) {
        if (!leaf.selected || !leaf.changed) {
            continue;
        }
        if (driver_change_hook) {
            try {
                driver_change_hook(
                    prepared.process, leaf.signal, scheduler.now());
            } catch (...) {
                if (!prepared.raw_observer_failure) {
                    prepared.raw_observer_failure
                        = std::current_exception();
                }
            }
        }
        try {
            publish_aggregate_leaf_driver_change(
                prepared.process, leaf.signal);
        } catch (...) {
            if (!prepared.raw_observer_failure) {
                prepared.raw_observer_failure = std::current_exception();
            }
        }
    }
    prepared.raw_notifications_complete = true;
}

void Interpreter::Impl::finish_container_alias_driver_family_raw(
    PreparedContainerAliasDriverFamily& prepared) noexcept
{
    if (!prepared.installed || !prepared.raw_phase_begun
        || prepared.raw_phase_closed) {
        return;
    }
    notify_container_alias_driver_family_raw(prepared);

    if (prepared.aggregate_batch_open) {
        const auto proxy
            = container_aggregate_signal_aliases[prepared.object]->signal;
        try {
            finish_aggregate_signal_batch(proxy, true);
        } catch (...) {
            if (!prepared.raw_observer_failure) {
                prepared.raw_observer_failure = std::current_exception();
            }
        }

        auto& batch = aggregate_signal_batches[proxy];
        batch = { };
        if (prepared.suspended_aggregate_batch) {
            batch = std::move(*prepared.suspended_aggregate_batch);
            prepared.suspended_aggregate_batch.reset();
        }
        prepared.aggregate_batch_open = false;
    }
    prepared.raw_phase_closed = true;
}

void Interpreter::Impl::finalize_container_alias_driver_family(
    PreparedContainerAliasDriverFamily& prepared)
{
    if (!prepared.installed || !prepared.raw_phase_begun
        || !prepared.raw_phase_closed) {
        throw std::logic_error {
            "container driver family raw phase is incomplete"
        };
    }
    if (prepared.raw_observer_failure) {
        std::rethrow_exception(prepared.raw_observer_failure);
    }

    std::vector<PackedLogic4> resolved_values;
    std::vector<std::uint8_t> selected_leaves;
    resolved_values.reserve(prepared.leaves.size());
    selected_leaves.reserve(prepared.leaves.size());
    for (const auto& leaf : prepared.leaves) {
        selected_leaves.push_back(leaf.selected ? 1U : 0U);
        if (leaf.selected) {
            resolved_values.push_back(resolved_driver_value(leaf.signal));
        } else {
            resolved_values.push_back(driven_values.at(leaf.signal));
        }
    }
    publish_container_alias_family(
        prepared.object, resolved_values, prepared.origin, { },
        selected_leaves);
}

void Interpreter::Impl::write_container_alias_driver_family(
    const ContainerObjectId object,
    const ProcessId process,
    const std::vector<PackedLogic4>& values,
    const SignalChangeOrigin origin)
{
    auto prepared = prepare_container_alias_driver_family(
        object, process, values, origin);
    begin_container_alias_driver_family(prepared);
    install_container_alias_driver_family(prepared);
    notify_container_alias_driver_family_raw(prepared);
    finish_container_alias_driver_family_raw(prepared);
    finalize_container_alias_driver_family(prepared);
}

void Interpreter::Impl::write_container_object_value(
    const ContainerObjectId id,
    const ContainerValue& value,
    const std::optional<ProcessId> driver,
    const SignalChangeOrigin origin)
{
    require_container_signal_role_materialized(*this, id);
    validate_container_value(value);
    auto& object = get_container_object(id);
    invalidate_container_aggregate_extract(id);
    if (object.initial_value.type != value.type) {
        throw std::invalid_argument {
            "container object write type mismatch"
        };
    }
    if (!object.slice_alias) {
        const auto& element_aliases = container_element_signal_aliases.at(id);
        if (std::ranges::any_of(
                element_aliases,
                [](const auto& alias) { return alias.has_value(); })) {
            std::vector<PackedLogic4> prepared;
            prepared.reserve(value.elements.size());
            for (std::size_t ordinal = 0U;
                ordinal < value.elements.size(); ++ordinal) {
                const auto& alias = element_aliases[ordinal];
                const auto& element = value.elements[ordinal];
                if (!alias || !alias->writable || !alias->readable
                    || element.width() != value.type.element_width
                    || element.is_logic9()
                    || (value.type.two_state && has_unknown(element))) {
                    throw std::invalid_argument {
                        "container write cannot be translated to its element signals"
                    };
            }
            prepared.push_back(element);
        }
        const auto proxy = id < container_aggregate_signal_aliases.size()
                    && container_aggregate_signal_aliases[id]
                ? std::optional<SignalId> {
                    container_aggregate_signal_aliases[id]->signal }
                : std::nullopt;
        if (driver && proxy && can_stage_container_alias_deposit(id)
            && !processes.program_view(*driver).switch_source()) {
            write_container_alias_driver_family(
                id, *driver, prepared, origin);
            return;
        }
        if (!driver && proxy && can_stage_container_alias_deposit(id)) {
            write_container_alias_deposit(id, prepared, origin);
            return;
        }
        begin_container_alias_write_batch(id, driver.has_value());
            try {
                if (proxy) {
                    begin_aggregate_signal_batch(*proxy, origin);
                }
            } catch (...) {
                bool changed { };
                auto leaves
                    = take_container_alias_write_batch(id, changed);
                finish_container_alias_write_batch(
                    id, std::move(leaves), changed);
                throw;
            }
            const auto finish_batches = [&] {
                std::exception_ptr callback_failure;
                capture_container_alias_write_batch(id);
                bool container_changed { };
                auto leaves = take_container_alias_write_batch(
                    id, container_changed);
                if (proxy) {
                    try {
                        finish_aggregate_signal_batch(*proxy, true);
                    } catch (...) {
                        callback_failure = std::current_exception();
                    }
                }
                try {
                    finish_container_alias_write_batch(
                        id, std::move(leaves), container_changed);
                } catch (...) {
                    if (!callback_failure) {
                        callback_failure = std::current_exception();
                    }
                }
                if (callback_failure) {
                    std::rethrow_exception(callback_failure);
                }
            };
            try {
                for (std::size_t ordinal = 0U;
                    ordinal < prepared.size(); ++ordinal) {
                    const auto& alias = *element_aliases[ordinal];
                    if (driver) {
                        commit_driver(
                            *driver, alias.signal,
                            std::move(prepared[ordinal]), origin, false);
                    } else {
                        commit(
                            alias.signal, std::move(prepared[ordinal]), origin);
                    }
                }
            } catch (...) {
                const auto mutation_failure = std::current_exception();
                try {
                    finish_batches();
                } catch (...) {
                }
                std::rethrow_exception(mutation_failure);
            }
            finish_batches();
            return;
        }
        const bool changed = object.initial_value != value;
        const auto& alias = container_signal_aliases.at(id);
        if (alias && alias->writable
            && container_value_reference_exposed[id] != 0U) {
            ContainerValue next = value;
            prepare_container_value_reference_object_update(id, next);
            if (same_container_storage_shape(object.initial_value, next)) {
                copy_container_value_in_place(object.initial_value, next);
            } else {
                object.initial_value = std::move(next);
            }
            synchronize_container_value_references_from_object(id);
        } else if (container_value_reference_exposed[id] != 0U
            || std::ranges::any_of(
                exposed_container_value_references,
                [&](const auto exposed) {
                    auto selected = exposed;
                    for (std::size_t depth = 0U;
                        depth < container_objects.size(); ++depth) {
                        const auto& view = container_objects[selected];
                        if (!view.slice_alias) {
                            return false;
                        }
                        if (view.slice_alias->object == id) {
                            return true;
                        }
                        selected = view.slice_alias->object;
                    }
                    return false;
                })) {
            ContainerValue next = value;
            prepare_container_value_reference_object_update(id, next);
            if (same_container_storage_shape(object.initial_value, next)) {
                copy_container_value_in_place(object.initial_value, next);
            } else {
                object.initial_value = std::move(next);
            }
            synchronize_container_value_references_from_object(id);
        } else {
            object.initial_value = value;
        }
        if (alias && alias->writable) {
            const bool logic9 = get_signal(alias->signal).initial_value.is_logic9();
            auto packed = pack_container_signal_value(value, logic9);
            if (driver) {
                commit_driver(
                    *driver, alias->signal, std::move(packed), origin, false);
            } else {
                commit(alias->signal, std::move(packed), origin);
            }
            if (container_value_reference_exposed[id] == 0U) {
                container_materialized_revisions[id].reset();
            }
        }
        if (changed && (!alias || !alias->writable)) {
            const auto waiters = container_dynamic_fanout.at(id);
            for (const auto process : waiters) {
                queue_next_delta(process, origin);
            }
        }
        if (changed && (!alias || !alias->writable)
            && container_object_change_hook) {
            container_object_change_hook(id, scheduler.now());
        }
        return;
    }
    const auto alias = *object.slice_alias;
    auto replacement = read_container_object_value(alias.object);
    const auto descending = alias.selected_left >= alias.selected_right;
    for (std::size_t ordinal = 0;
        ordinal < value.elements.size();
        ++ordinal) {
        const auto selected_index = static_cast<std::int32_t>(
            static_cast<std::int64_t>(alias.selected_left)
            + (descending
                    ? -static_cast<std::int64_t>(ordinal)
                    : static_cast<std::int64_t>(ordinal)));
        replacement.elements[fixed_offset(replacement.type, selected_index)] = value.elements[ordinal];
    }
    write_container_object_value(
        alias.object, replacement, driver, origin);
    if (container_value_reference_exposed[id] == 0U) {
        object.initial_value = value;
    }
}

void Interpreter::Impl::write_container_object_element_value(
    const ContainerObjectId id,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const PackedLogic4& value,
    const ProcessId process,
    const InstructionIndex instruction,
    const SignalChangeOrigin origin)
{
    require_container_signal_role_materialized(*this, id);
    auto& object = get_container_object(id);
    invalidate_container_aggregate_extract(id);
    auto& target = object.initial_value;
    if ((target.type.element_kind != ContainerElementKind::Packed
            && target.type.element_kind != ContainerElementKind::Scalar)
        || value.width() != target.type.element_width
        || value.is_logic9()
        || (target.type.two_state && has_unknown(value))) {
        container_error(
            process, instruction,
            "container element write type mismatch");
    }
    if (target.type.associative) {
        if (target.type.string_indices) {
            container_error(
                process, instruction,
                "direct associative object element writes require an "
                "integral key");
        }
        auto replacement = target;
        const auto key = associative_key(
            process, instruction, replacement.type, index);
        const auto at = lower_key(replacement, key);
        if (at < replacement.keys.size()
            && key_equal(replacement.keys[at], key)) {
            replacement.elements[at] = value;
        } else {
            if (replacement.elements.size()
                >= maximum_container_elements(replacement.type)) {
                container_error(
                    process, instruction,
                    "associative array exceeds the per-container "
                    "owning-storage budget");
            }
            replacement.keys.insert(
                iterator_at(replacement.keys, at), key);
            replacement.elements.insert(
                iterator_at(replacement.elements, at), value);
        }
        write_container_object_value(id, replacement, process, origin);
        return;
    }
    if (object.slice_alias) {
        // Slice aliases need a coherent aggregate replacement. Ordinary
        // signal-backed element writes use the direct packed slice below and
        // must not materialize the whole container.
        (void)read_container_object_value(id);
        auto replacement = target;
        const auto selected = target.type.fixed
            ? linear_index
                ? known_index(
                      process, instruction, index, true,
                      "multidimensional linear index")
                : fixed_offset(
                      process, instruction, target.type, index)
            : known_index(
                  process, instruction, index, signed_index,
                  "container index");
        if (selected >= replacement.elements.size()) {
            container_error(
                process, instruction,
                "container index is out of range");
        }
        replacement.elements[selected] = value;
        write_container_object_value(id, replacement, process, origin);
        return;
    }
    const auto selected = target.type.fixed
        ? linear_index
            ? known_index(
                  process, instruction, index, true,
                  "multidimensional linear index")
            : fixed_offset(
                  process, instruction, target.type, index)
        : known_index(
              process, instruction, index, signed_index,
              "container index");
    if (selected >= target.elements.size()) {
        container_error(
            process, instruction,
            "container index is out of range");
    }
    const auto& element_aliases = container_element_signal_aliases.at(id);
    if (selected < element_aliases.size() && element_aliases[selected]) {
        const auto& alias = *element_aliases[selected];
        if (!alias.writable) {
            container_error(
                process, instruction,
                "container element has no writable signal alias");
        }
        commit_driver(process, alias.signal, value, origin);
        return;
    }
    const auto& alias = container_signal_aliases.at(id);
    const auto packed_offset = alias && alias->readable
        ? get_signal(alias->signal).initial_value.width()
            - (selected + 1U) * target.type.element_width
        : 0U;
    const auto materialized_revision
        = container_materialized_revisions.at(id);
    const auto signal_revision = alias
        ? signal_value_revisions.at(alias->signal)
        : 0U;
    const bool changed = alias && alias->readable
        ? extract_value(
              get_signal(alias->signal).initial_value,
              packed_offset,
              target.type.element_width)
            != value
        : target.elements[selected] != value;
    const bool stable_signal_backing
        = alias && alias->writable
        && container_value_reference_exposed[id] != 0U;
    target.elements[selected] = value;
    if (stable_signal_backing) {
        synchronize_container_value_references_from_object(id);
    }
    if (alias && alias->writable) {
        const auto& signal = get_signal(alias->signal).initial_value;
        const auto offset = signal.width()
            - (selected + 1U) * target.type.element_width;
        commit_slice(alias->signal, value, offset, origin);
        if (materialized_revision == signal_revision) {
            container_materialized_revisions[id]
                = signal_value_revisions.at(alias->signal);
        }
    }
    if (!changed) {
        return;
    }
    if (!alias || !alias->writable) {
        if (container_value_reference_exposed[id] != 0U) {
            synchronize_container_value_references_from_object(id);
        }
        const auto waiters = container_dynamic_fanout.at(id);
        for (const auto waiter : waiters) {
            queue_next_delta(waiter, origin);
        }
        if (container_object_change_hook) {
            container_object_change_hook(id, scheduler.now());
        }
    }
}

void Interpreter::Impl::write_container_object_dynamic_part_element_value(
    const ContainerObjectId id,
    const PackedLogic4& index,
    const bool signed_index,
    const bool linear_index,
    const PackedLogic4& value,
    const PackedLogic4& base,
    const DynamicPartIndex& selection,
    const ProcessId process,
    const InstructionIndex instruction,
    const SignalChangeOrigin origin)
{
    auto& object = get_container_object(id);
    invalidate_container_aggregate_extract(id);
    const auto& type = object.initial_value.type;
    if (!type.fixed || type.associative || object.slice_alias
        || (type.element_kind != ContainerElementKind::Packed
            && type.element_kind != ContainerElementKind::Scalar)
        || selection.width == 0U || value.width() != selection.width
        || base.width() != 32U || type.element_width == 0U
        || (type.two_state && has_unknown(value))) {
        container_error(
            process, instruction,
            "dynamic part-select container element write type mismatch");
    }
    if (selection.left < std::numeric_limits<std::int32_t>::min()
        || selection.left > std::numeric_limits<std::int32_t>::max()
        || selection.right < std::numeric_limits<std::int32_t>::min()
        || selection.right > std::numeric_limits<std::int32_t>::max()) {
        container_error(
            process, instruction,
            "dynamic part-select write bounds must fit signed 32-bit integers");
    }
    const auto lower = std::min(selection.left, selection.right);
    const auto upper = std::max(selection.left, selection.right);
    const auto range_width = static_cast<std::uint64_t>(upper - lower) + 1U;
    if (selection.base_offset > type.element_width
        || range_width > type.element_width - selection.base_offset) {
        container_error(
            process, instruction,
            "dynamic part-select write range is outside its packed target");
    }

    const auto selected = linear_index
        ? known_index(
              process, instruction, index, true,
              "multidimensional linear index")
        : fixed_offset(process, instruction, type, index);
    if (selected >= object.initial_value.elements.size()) {
        container_error(
            process, instruction,
            "container index is out of range");
    }

    const auto& element_aliases = container_element_signal_aliases.at(id);
    if (selected < element_aliases.size() && element_aliases[selected]) {
        const auto& alias = *element_aliases[selected];
        if (!alias.writable || !alias.readable) {
            container_error(
                process, instruction,
                "container element has no writable signal alias");
        }
        const auto write = dynamic_part_write_value(
            value, base, selection);
        if (!write) {
            return;
        }
        commit_driver_slice(
            process, alias.signal, write->value, write->offset, origin);
        return;
    }

    // Read only the selected element. A partial element-alias map may include
    // unrelated write-only aliases that cannot be materialized as a whole.
    PackedLogic4 current;
    if (!read_container_object_element(id, selected, current)) {
        container_error(
            process, instruction,
            "dynamic part-select container element cannot be read");
    }
    PackedLogic4 replacement;
    try {
        replacement = dynamic_part_insert_value(
            std::move(current), value, base, selection);
    } catch (const std::invalid_argument&) {
        container_error(
            process, instruction,
            "invalid dynamic part-select container element write");
    }
    write_container_object_element_value(
        id, index, signed_index, linear_index, replacement,
        process, instruction, origin);
}

PackedLogic4 default_container_element(
    const ContainerType& type)
{
    return PackedLogic4(type.element_width, Logic4::zero);
}

void resize_container_value(
    ContainerValue& target,
    const std::size_t size,
    const ContainerValue* initializer)
{
    validate_container_value(target);
    if (target.type.associative || target.type.fixed
        || aggregate_box(target.type)) {
        throw std::invalid_argument {
            "only a dynamic array or queue may be resized"
        };
    }
    if (size > maximum_container_elements(target.type)) {
        throw std::length_error {
            "dynamic array exceeds its owning-storage budget"
        };
    }
    if (initializer) {
        validate_container_value(*initializer);
        if (initializer->type != target.type) {
            throw std::invalid_argument {
                "dynamic-array initializer type mismatch"
            };
        }
    }
    const auto preserved = initializer ? container_value_size(*initializer) : 0;
    ContainerValue replacement;
    replacement.type = target.type;
    switch (target.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        replacement.elements.assign(size, initial_packed_element(target.type));
        break;
    case ContainerElementKind::String:
        replacement.string_elements.resize(size);
        break;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        replacement.nested_elements.reserve(size);
        for (std::size_t index = 0; index < size; ++index) {
            append_default_element(replacement);
        }
        break;
    }
    if (initializer) {
        copy_element_prefix(
            replacement, *initializer, std::min(size, preserved));
    }
    validate_container_value(replacement);
    target = std::move(replacement);
}

void select_container_value(
    ContainerValue& destination,
    const PackedLogic4& condition,
    const ContainerValue& when_true,
    const ContainerValue& when_false)
{
    validate_container_value(destination);
    validate_container_value(when_true);
    validate_container_value(when_false);
    if (destination.type != when_true.type
        || destination.type != when_false.type) {
        throw std::invalid_argument {
            "container conditional profiles differ"
        };
    }
    if (condition.width() != 1) {
        throw std::invalid_argument {
            "container conditional condition must be scalar"
        };
    }
    const auto state = condition.get(0);
    if (state == Logic4::one || state == Logic4::zero) {
        destination = state == Logic4::one ? when_true : when_false;
        return;
    }
    if (container_value_size(when_true)
            != container_value_size(when_false)
        || when_true.keys != when_false.keys) {
        destination = default_container_value(destination.type);
        return;
    }
    if (destination.type.element_kind != ContainerElementKind::Packed) {
        destination = when_true == when_false
            ? when_true
            : default_container_value(destination.type);
        return;
    }
    destination.keys = when_true.keys;
    destination.elements.clear();
    destination.elements.reserve(when_true.elements.size());
    for (std::size_t index = 0;
        index < when_true.elements.size(); ++index) {
        auto merged = conditional_value(
            condition,
            when_true.elements[index],
            when_false.elements[index]);
        if (destination.type.two_state) {
            auto coerced = PackedLogic4(
                destination.type.element_width, Logic4::zero);
            for (std::size_t bit = 0; bit < merged.width(); ++bit) {
                if (merged.get(bit) == Logic4::one) {
                    coerced.set(bit, Logic4::one);
                }
            }
            merged = std::move(coerced);
        }
        destination.elements.push_back(std::move(merged));
    }
}

PackedLogic4 compare_container_values(
    const ContainerValue& lhs,
    const ContainerValue& rhs,
    const bool case_equal)
{
    validate_container_value(lhs);
    validate_container_value(rhs);
    if (lhs.type != rhs.type) {
        throw std::invalid_argument {
            "container equality profiles differ"
        };
    }
    if (container_value_size(lhs) != container_value_size(rhs)
        || lhs.keys != rhs.keys) {
        return PackedLogic4(1, Logic4::zero);
    }
    if (lhs.type.element_kind == ContainerElementKind::String) {
        return PackedLogic4(
            1, lhs.string_elements == rhs.string_elements ? Logic4::one : Logic4::zero);
    }
    if (lhs.type.element_kind == ContainerElementKind::Container
        || lhs.type.element_kind == ContainerElementKind::Aggregate) {
        bool unknown { };
        for (std::size_t index = 0;
            index < lhs.nested_elements.size(); ++index) {
            const auto compared = compare_container_values(
                lhs.nested_elements[index], rhs.nested_elements[index], case_equal);
            const auto state = compared.get(0);
            if (state == Logic4::zero) {
                return PackedLogic4(1, Logic4::zero);
            }
            unknown |= state != Logic4::one;
        }
        return PackedLogic4(1, unknown ? Logic4::x : Logic4::one);
    }
    if (lhs.type.element_kind == ContainerElementKind::Scalar) {
        for (std::size_t index = 0; index < lhs.elements.size(); ++index) {
            const auto& left = lhs.elements[index];
            const auto& right = rhs.elements[index];
            bool equal { };
            if (lhs.type.scalar_kind == SystemVerilogScalarKind::Chandle
                || lhs.type.scalar_kind == SystemVerilogScalarKind::Time) {
                equal = left.low_word().aval == right.low_word().aval;
            } else {
                const auto left_scalar = decode_systemverilog_scalar_payload(
                    left, lhs.type.scalar_kind);
                const auto right_scalar = decode_systemverilog_scalar_payload(
                    right, rhs.type.scalar_kind);
                if (!left_scalar || !right_scalar) {
                    throw std::invalid_argument {
                        "SimIR scalar container payload is invalid"
                    };
                }
                const auto compared = systemverilog_scalar_compare(
                    SystemVerilogScalarComparison::Equal,
                    left_scalar.value, right_scalar.value);
                equal = compared && compared.value;
            }
            if (!equal)
                return PackedLogic4(1, Logic4::zero);
        }
        return PackedLogic4(1, Logic4::one);
    }
    bool unknown { };
    for (std::size_t element = 0;
        element < lhs.elements.size(); ++element) {
        const auto& left = lhs.elements[element];
        const auto& right = rhs.elements[element];
        for (std::size_t bit = 0; bit < lhs.type.element_width; ++bit) {
            const auto left_bit = left.get(bit);
            const auto right_bit = right.get(bit);
            if (case_equal) {
                if (left_bit != right_bit) {
                    return PackedLogic4(1, Logic4::zero);
                }
                continue;
            }
            const auto left_known = left_bit == Logic4::zero || left_bit == Logic4::one;
            const auto right_known = right_bit == Logic4::zero || right_bit == Logic4::one;
            if (left_known && right_known && left_bit != right_bit) {
                return PackedLogic4(1, Logic4::zero);
            }
            unknown |= !left_known || !right_known;
        }
    }
    return PackedLogic4(
        1, unknown ? Logic4::x : Logic4::one);
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ResizeContainer& operation)
{
    auto& target = get_container_register(process, operation.target);
    if ((target.type.queue && !operation.allow_queue)
        || target.type.associative || target.type.fixed) {
        container_error(
            process.id, process.pc,
            target.type.queue
                ? "new[size] cannot resize a queue"
                : target.type.associative
                ? "new[size] cannot resize an associative array"
                : "new[size] cannot resize a static array");
    }
    const auto size = known_index(
        process.id, process.pc,
        get_register(process, operation.size),
        false, "dynamic-array size");
    const ContainerValue* initializer { };
    if (operation.initializer) {
        const auto& source = read_container_register(
            process, *operation.initializer);
        if (source.type != target.type) {
            container_error(
                process.id, process.pc,
                "dynamic-array initializer type mismatch");
        }
        initializer = &source;
    }
    try {
        resize_container_value(target, size, initializer);
    } catch (const std::exception& error) {
        container_error(
            process.id, process.pc, error.what());
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CopyContainerRegister& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& source = read_container_register(process, operation.source);
    require_same_type(
        process.id, process.pc,
        destination.type, source.type);
    destination = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ConditionalContainerSelect& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& when_true = read_container_register(process, operation.when_true);
    const auto& when_false = read_container_register(process, operation.when_false);
    const auto& condition = get_register(process, operation.condition);
    try {
        select_container_value(
            destination, condition, when_true, when_false);
    } catch (const std::exception& error) {
        container_error(
            process.id, process.pc, error.what());
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CompareContainers& operation)
{
    try {
        get_register(process, operation.destination) = compare_container_values(
            read_container_register(process, operation.lhs),
            read_container_register(process, operation.rhs),
            operation.case_equal);
    } catch (const std::exception& error) {
        container_error(
            process.id, process.pc, error.what());
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ReadContainerObject& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& source = read_container_object_value(operation.object);
    require_same_type(
        process.id, process.pc,
        destination.type, source.type);
    destination = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const WriteContainerObject& operation)
{
    const auto& source = read_container_register(process, operation.source);
    const auto process_domain = process.program().scheduling_domain();
    const auto update_domain
        = process_domain == ProcessSchedulingDomain::systemverilog
        ? SignalUpdateDomain::systemverilog_active
        : SignalUpdateDomain::generic;
    const auto origin = capture_signal_change_origin(
        process.id, update_domain);
    try {
        write_container_object_value(
            operation.object, source, process.id, origin);
    } catch (const std::invalid_argument& error) {
        container_error(
            process.id, process.pc, error.what());
    }
    if (operation.transaction_signal) {
        stage_update(
            process.id,
            *operation.transaction_signal,
            PackedLogic4 { 1, Logic4::zero },
            update_domain);
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerSize& operation)
{
    const auto size = container_value_size(read_container_register(
        process, operation.source));
    get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, size, 0);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerReduction& operation)
{
    get_register(process, operation.destination) = reduce_container_value(
        read_container_register(process, operation.source),
        operation.operation, operation.transformation);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const OrderContainer& operation)
{
    auto& target = get_container_register(process, operation.target);
    order_container_value(
        target, operation.operation, operation.key,
        [&]() { return next_random(process); });
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const LocateContainer& operation)
{
    auto& destination = get_container_register(process, operation.destination);
    const auto& source = read_container_register(process, operation.source);
    locate_container_values(
        destination, source, operation.operation,
        operation.predicate, operation.transformation);
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerRead& operation)
{
    const auto& source = read_container_register(process, operation.source);
    if (source.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.id, process.pc, source.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(source, key);
            get_register(process, operation.destination) = at < source.string_keys.size()
                    && source.string_keys[at] == key
                ? source.elements[at]
                : default_container_element(source.type);
            ++process.pc;
            return;
        }
        const auto key = associative_key(
            process.id, process.pc, source.type,
            get_register(process, operation.index));
        const auto at = lower_key(source, key);
        get_register(process, operation.destination) = at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.elements[at]
            : default_container_element(source.type);
        ++process.pc;
        return;
    }
    if (source.type.fixed) {
        const auto& index_value = get_register(
            process, operation.index);
        if (operation.linear_index) {
            const auto index = index_value.known_signed_value();
            if (!index || *index < 0
                || static_cast<std::uint64_t>(*index)
                    >= source.elements.size()) {
                get_register(process, operation.destination) = PackedLogic4(
                    source.type.element_width,
                    source.type.two_state
                        ? Logic4::zero
                        : Logic4::x);
                ++process.pc;
                return;
            }
            get_register(process, operation.destination) = source.elements[static_cast<std::size_t>(*index)];
            ++process.pc;
            return;
        }
        const auto index = index_value.known_signed_value();
        const auto low = std::min(
            source.type.index_left, source.type.index_right);
        const auto high = std::max(
            source.type.index_left, source.type.index_right);
        if (!index || *index < low || *index > high) {
            get_register(process, operation.destination) = PackedLogic4(
                source.type.element_width,
                source.type.two_state
                    ? Logic4::zero
                    : Logic4::x);
            ++process.pc;
            return;
        }
        get_register(process, operation.destination) = source.elements[fixed_offset(source.type, static_cast<std::int32_t>(*index))];
        ++process.pc;
        return;
    }
    const auto index = known_index(
        process.id, process.pc,
        get_register(process, operation.index),
        operation.signed_index, "container index");
    if (index >= source.elements.size()) {
        container_error(
            process.id, process.pc,
            "container index is out of range");
    }
    get_register(process, operation.destination) = source.elements[index];
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerWrite& operation)
{
    auto& target = get_container_register(process, operation.target);
    const auto& source = get_register(process, operation.source);
    if (source.width() != target.type.element_width
        || source.is_logic9()
        || (target.type.two_state && has_unknown(source))) {
        container_error(
            process.id, process.pc,
            "container element write type mismatch");
    }
    if (target.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.id, process.pc, target.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(target, key);
            if (at < target.string_keys.size()
                && target.string_keys[at] == key) {
                target.elements[at] = source;
            } else {
                if (target.elements.size()
                    >= maximum_container_elements(target.type)) {
                    container_error(
                        process.id, process.pc,
                        "associative array exceeds the per-container "
                        "owning-storage budget");
                }
                target.string_keys.insert(
                    iterator_at(target.string_keys, at), key);
                target.elements.insert(
                    iterator_at(target.elements, at), source);
            }
            validate_container_value(target);
            ++process.pc;
            return;
        }
        const auto key = associative_key(
            process.id, process.pc, target.type,
            get_register(process, operation.index));
        const auto at = lower_key(target, key);
        if (at < target.keys.size()
            && key_equal(target.keys[at], key)) {
            target.elements[at] = source;
        } else {
            if (target.elements.size()
                >= maximum_container_elements(target.type)) {
                container_error(
                    process.id, process.pc,
                    "associative array exceeds the per-container "
                    "owning-storage budget");
            }
            target.keys.insert(iterator_at(target.keys, at), key);
            target.elements.insert(iterator_at(target.elements, at), source);
        }
        ++process.pc;
        return;
    }
    if (target.type.fixed) {
        if (operation.linear_index) {
            const auto index = known_index(
                process.id, process.pc,
                get_register(process, operation.index),
                true, "multidimensional linear index");
            if (index >= target.elements.size()) {
                container_error(
                    process.id, process.pc,
                    "multidimensional linear index is out of range");
            }
            target.elements[index] = source;
            ++process.pc;
            return;
        }
        target.elements[fixed_offset(
            process.id, process.pc, target.type,
            get_register(process, operation.index))] = source;
        ++process.pc;
        return;
    }
    const auto index = known_index(
        process.id, process.pc,
        get_register(process, operation.index),
        operation.signed_index, "container index");
    if (index >= target.elements.size()) {
        container_error(
            process.id, process.pc,
            "container index is out of range");
    }
    target.elements[index] = source;
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const WriteContainerObjectElement& operation)
{
    const auto index = get_register(process, operation.index);
    const auto value = get_register(process, operation.source);
    const auto base = operation.dynamic_part
        ? std::optional { get_register(
              process, operation.dynamic_part->base) }
        : std::nullopt;
    const auto process_domain = process.program().scheduling_domain();
    const auto update_domain
        = process_domain == ProcessSchedulingDomain::systemverilog
        ? operation.nonblocking
            ? SignalUpdateDomain::systemverilog_nba
            : SignalUpdateDomain::systemverilog_active
        : SignalUpdateDomain::generic;
    auto origin = capture_signal_change_origin(
        process.id, update_domain);
    if (update_domain == SignalUpdateDomain::generic
        && operation.nonblocking) {
        origin.phase = SchedulerPhase::update;
    }
    if (operation.nonblocking) {
        const auto publish = [this, object = operation.object, index,
                signed_index = operation.signed_index,
                linear_index = operation.linear_index, value,
                base, dynamic_part = operation.dynamic_part,
                driver = process.id,
                instruction = process.pc, origin](Scheduler&) {
                if (dynamic_part) {
                    if (!base) {
                        container_error(
                            driver, instruction,
                            "dynamic container element write is missing its captured base");
                    }
                    write_container_object_dynamic_part_element_value(
                        object, index, signed_index, linear_index, value,
                        *base, *dynamic_part, driver, instruction, origin);
                } else {
                    write_container_object_element_value(
                        object, index, signed_index, linear_index, value,
                        driver, instruction, origin);
                }
            };
        if (process_domain == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_systemverilog(
                origin.phase, process.id, publish);
        } else {
            scheduler.schedule(
                SchedulerPhase::update, process.id, publish);
        }
    } else if (operation.dynamic_part) {
        if (!base) {
            container_error(
                process.id, process.pc,
                "dynamic container element write is missing its captured base");
        }
        write_container_object_dynamic_part_element_value(
            operation.object, index, operation.signed_index,
            operation.linear_index, value, *base, *operation.dynamic_part,
            process.id, process.pc, origin);
    } else {
        write_container_object_element_value(
            operation.object, index, operation.signed_index,
            operation.linear_index, value,
            process.id, process.pc, origin);
    }
    if (operation.transaction_signal) {
        stage_update(
            process.id,
            *operation.transaction_signal,
            PackedLogic4 { 1, Logic4::zero },
            update_domain);
    }
    ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerStringRead& operation)
{
    const auto& source = read_container_register(process, operation.source);
    if (!operation.members.empty()) {
        if (source.type.element_kind != ContainerElementKind::Aggregate
            || source.type.associative || operation.string_index) {
            container_error(
                process.id, process.pc,
                "aggregate string member read requires an indexed aggregate container");
        }
        const auto at = source.type.fixed
            ? operation.linear_index
                ? known_index(
                      process.id, process.pc,
                      get_register(process, operation.index), true,
                      "multidimensional linear index")
                : fixed_offset(
                      process.id, process.pc, source.type,
                      get_register(process, operation.index))
            : known_index(
                  process.id, process.pc,
                  get_register(process, operation.index),
                  operation.signed_index, "container index");
        if (at >= source.nested_elements.size()) {
            container_error(
                process.id, process.pc,
                "aggregate container index is out of range");
        }
        const auto* selected = &source.nested_elements[at];
        for (const auto member : operation.members) {
            if (selected->type.element_kind != ContainerElementKind::Aggregate
                || member >= selected->nested_elements.size()) {
                container_error(
                    process.id, process.pc,
                    "aggregate string member read path is invalid");
            }
            selected = &selected->nested_elements[member];
        }
        if (selected->type.element_kind != ContainerElementKind::String
            || !selected->type.fixed
            || selected->string_elements.size() != 1U) {
            container_error(
                process.id, process.pc,
                "aggregate string member read requires a scalar string leaf");
        }
        get_string_register(process, operation.destination)
            = selected->string_elements.front();
        ++process.pc;
        return;
    }
    if (source.type.element_kind != ContainerElementKind::String) {
        container_error(
            process.id, process.pc,
            "string element read requires a string container");
    }
    if (source.type.associative) {
        if (operation.string_index) {
            const auto& key = associative_string_key(
                process.id, process.pc, source.type,
                get_string_register(process, operation.index));
            const auto at = lower_string_key(source, key);
            get_string_register(process, operation.destination) = at < source.string_keys.size()
                    && source.string_keys[at] == key
                ? source.string_elements[at]
                : std::string { };
            ++process.pc;
            return;
        }
        const auto key = associative_key(
            process.id, process.pc, source.type,
            get_register(process, operation.index));
        const auto at = lower_key(source, key);
        get_string_register(process, operation.destination) = at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.string_elements[at]
            : std::string { };
        ++process.pc;
        return;
    }
    const auto at = source.type.fixed
        ? operation.linear_index
            ? known_index(
                  process.id, process.pc,
                  get_register(process, operation.index), true,
                  "multidimensional linear index")
            : fixed_offset(
                  process.id, process.pc, source.type,
                  get_register(process, operation.index))
        : known_index(
              process.id, process.pc,
              get_register(process, operation.index),
              operation.signed_index, "container index");
    if (at >= source.string_elements.size()) {
        container_error(
            process.id, process.pc,
            "string container index is out of range");
    }
    get_string_register(process, operation.destination) = source.string_elements[at];
    ++process.pc;
}

} // namespace fsim::runtime
