// SPDX-License-Identifier: Apache-2.0
//
// The flat attempt storage of a concurrent assertion directive; see
// include/fsim/runtime/sva_storage.hpp for the layout.

#include "fsim/runtime/sva_storage.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace fsim::runtime::sva {

using namespace layout;

namespace {

[[nodiscard]] bool power_of_two_or_zero(const std::size_t value) noexcept
{
    return value == 0U || std::has_single_bit(value);
}

} // namespace

std::size_t AttemptStorage::grown(
    const std::size_t capacity, const std::size_t needed) noexcept
{
    return std::bit_ceil(std::max<std::size_t>({ 8U, capacity * 2U, needed }));
}

AttemptStorage::AttemptStorage(const std::size_t clocks)
    : words_(header_words, 0)
{
    words_[h_magic] = magic;
    Caps initial;
    initial.clocks = clocks;
    initial.attempts = 4U;
    initial.outcomes = 4U;
    initial.started = 4U;
    initial.events = 0U;
    initial.requests = 0U;
    initial.heap = 128U;
    relayout(initial);
    for (std::size_t clock = 0; clock < clocks; ++clock) {
        clock_ticks(clock) = -1;
        clock_time(clock) = std::numeric_limits<std::int64_t>::min();
        clock_ticked(clock) = 0;
    }
}

AttemptStorage::Caps AttemptStorage::caps() const noexcept
{
    Caps result;
    result.clocks = static_cast<std::size_t>(get(h_clocks_count));
    result.attempts = static_cast<std::size_t>(get(h_attempts_cap));
    result.outcomes = static_cast<std::size_t>(get(h_outcomes_cap));
    result.started = static_cast<std::size_t>(get(h_started_cap));
    result.events = static_cast<std::size_t>(get(h_events_cap));
    result.requests = static_cast<std::size_t>(get(h_requests_cap));
    result.heap = static_cast<std::size_t>(get(h_heap_cap));
    result.scratch = static_cast<std::size_t>(get(h_scratch_cap));
    result.memo = static_cast<std::size_t>(get(h_memo_cap));
    result.calls = static_cast<std::size_t>(get(h_calls_cap));
    result.bounds = get(h_bounds_off) == 0
        ? 0U
        : static_cast<std::size_t>(words_[static_cast<std::size_t>(get(h_bounds_off))]) + 1U;
    return result;
}

std::size_t AttemptStorage::total_words(const Caps& caps) noexcept
{
    return header_words + 3U * caps.clocks + caps.attempts * attempt_stride
        + caps.outcomes * outcome_stride + caps.started * started_stride
        + caps.events * event_stride + caps.requests * request_stride
        + 2U * caps.heap + caps.scratch + caps.memo + caps.calls + caps.bounds;
}

void AttemptStorage::relayout(const Caps& next)
{
    const auto old = std::move(words_);
    const auto field = [&](const Header header) {
        return static_cast<std::size_t>(old[header]);
    };
    std::vector<std::int64_t> block(total_words(next), 0);
    std::copy_n(old.begin(), header_words, block.begin());
    std::size_t at = header_words;
    const auto place = [&](const std::size_t size) {
        const auto offset = at;
        at += size;
        return offset;
    };
    const auto clocks_off = place(3U * next.clocks);
    const auto attempts_off = place(next.attempts * attempt_stride);
    const auto outcomes_off = place(next.outcomes * outcome_stride);
    const auto started_off = place(next.started * started_stride);
    const auto events_off = place(next.events * event_stride);
    const auto requests_off = place(next.requests * request_stride);
    const auto heap_off = place(next.heap);
    const auto heap_alt_off = place(next.heap);
    const auto scratch_off = place(next.scratch);
    const auto memo_off = place(next.memo);
    const auto calls_off = place(next.calls);
    const auto bounds_off = next.bounds == 0U ? std::size_t { 0 } : place(next.bounds);

    // Tables keep their rows; the clocks keep their words.
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_clocks_off)),
        3U * std::min(field(h_clocks_count), next.clocks),
        block.begin() + static_cast<std::ptrdiff_t>(clocks_off));
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_attempts_off)),
        field(h_attempts_count) * attempt_stride,
        block.begin() + static_cast<std::ptrdiff_t>(attempts_off));
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_requests_off)),
        field(h_requests_count) * request_stride,
        block.begin() + static_cast<std::ptrdiff_t>(requests_off));
    // A ring is rewritten oldest first from slot 0.
    const auto copy_ring = [&](const Header off, const Header head,
                               const Header count, const Header cap,
                               const std::size_t stride,
                               const std::size_t destination) {
        const auto capacity = field(cap);
        for (std::size_t index = 0; index < field(count); ++index) {
            const auto slot = (field(head) + index) & (capacity - 1U);
            std::copy_n(
                old.begin() + static_cast<std::ptrdiff_t>(
                    field(off) + slot * stride),
                stride,
                block.begin()
                    + static_cast<std::ptrdiff_t>(destination + index * stride));
        }
    };
    copy_ring(h_outcomes_off, h_outcomes_head, h_outcomes_count,
        h_outcomes_cap, outcome_stride, outcomes_off);
    copy_ring(h_started_off, h_started_head, h_started_count, h_started_cap,
        started_stride, started_off);
    copy_ring(h_events_off, h_events_head, h_events_count, h_events_cap,
        event_stride, events_off);
    // The current and the alternate heap keep the words written so far.
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_heap_off)),
        field(h_heap_used),
        block.begin() + static_cast<std::ptrdiff_t>(heap_off));
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_heap_alt_off)),
        field(h_heap_alt_used),
        block.begin() + static_cast<std::ptrdiff_t>(heap_alt_off));
    if (next.bounds != 0U && field(h_bounds_off) != 0U) {
        std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_bounds_off)),
            std::min<std::size_t>(next.bounds, static_cast<std::size_t>(old[field(h_bounds_off)]) + 1U),
            block.begin() + static_cast<std::ptrdiff_t>(bounds_off));
    }
    // The scratch words in use (a sequence step in generated code grows the
    // block while it works).
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_scratch_off)),
        std::min(field(h_scratch_used), next.scratch),
        block.begin() + static_cast<std::ptrdiff_t>(scratch_off));
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_memo_off)),
        std::min(field(h_memo_used), next.memo),
        block.begin() + static_cast<std::ptrdiff_t>(memo_off));
    std::copy_n(old.begin() + static_cast<std::ptrdiff_t>(field(h_calls_off)),
        std::min(field(h_calls_used), next.calls),
        block.begin() + static_cast<std::ptrdiff_t>(calls_off));

    const auto put = [&](const Header header, const std::size_t value) {
        block[header] = static_cast<std::int64_t>(value);
    };
    put(h_clocks_count, next.clocks);
    put(h_clocks_off, clocks_off);
    put(h_attempts_off, attempts_off);
    put(h_attempts_cap, next.attempts);
    put(h_outcomes_off, outcomes_off);
    put(h_outcomes_head, 0U);
    put(h_outcomes_cap, next.outcomes);
    put(h_started_off, started_off);
    put(h_started_head, 0U);
    put(h_started_cap, next.started);
    put(h_events_off, events_off);
    put(h_events_head, 0U);
    put(h_events_cap, next.events);
    put(h_requests_off, requests_off);
    put(h_requests_cap, next.requests);
    put(h_heap_off, heap_off);
    put(h_heap_alt_off, heap_alt_off);
    put(h_heap_cap, next.heap);
    put(h_scratch_off, scratch_off);
    put(h_scratch_cap, next.scratch);
    put(h_memo_off, memo_off);
    put(h_memo_cap, next.memo);
    put(h_calls_off, calls_off);
    put(h_calls_cap, next.calls);
    put(h_bounds_off, bounds_off);
    put(h_total_words, block.size());
    words_ = std::move(block);
    publish(words_.data());
}

AttemptStorage::~AttemptStorage()
{
    publish(nullptr);
}

AttemptStorage::AttemptStorage(AttemptStorage&& other) noexcept
    : words_(std::move(other.words_))
    , slot_(std::move(other.slot_))
{
    // The moved-from storage publishes nowhere any more.
    other.slot_ = BlockSlot { };
    publish(words_.data());
}

AttemptStorage& AttemptStorage::operator=(AttemptStorage&& other) noexcept
{
    if (this != &other) {
        publish(nullptr);
        words_ = std::move(other.words_);
        slot_ = std::move(other.slot_);
        other.slot_ = BlockSlot { };
        publish(words_.data());
    }
    return *this;
}

void AttemptStorage::bind_slot(BlockSlot slot) noexcept
{
    publish(nullptr);
    slot_ = std::move(slot);
    publish(words_.data());
}

void AttemptStorage::publish(std::int64_t* const base) noexcept
{
    if (const auto owner = slot_.owner.lock();
        owner && slot_.index < owner->size()) {
        (*owner)[slot_.index] = base;
    }
}

std::optional<AttemptStorage> AttemptStorage::adopt(
    std::vector<std::int64_t> words)
{
    if (words.size() < header_words || words[h_magic] != magic
        || words[h_total_words] != static_cast<std::int64_t>(words.size())) {
        return std::nullopt;
    }
    const auto total = words.size();
    const auto field = [&](const Header header) -> std::optional<std::size_t> {
        const auto value = words[header];
        if (value < 0 || static_cast<std::uint64_t>(value) > total) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(value);
    };
    // A region [offset, offset + count * stride) lies inside the block.
    const auto region = [&](const Header off, const Header cap,
                            const std::size_t stride) {
        const auto offset = field(off);
        const auto capacity = field(cap);
        return offset && capacity && *offset >= header_words
            && *capacity <= total
            && *offset + *capacity * stride <= total;
    };
    const auto ring = [&](const Header off, const Header head,
                          const Header count, const Header cap,
                          const std::size_t stride) {
        const auto capacity = field(cap);
        const auto used = field(count);
        const auto first = field(head);
        return region(off, cap, stride) && used && first
            && power_of_two_or_zero(*capacity) && *used <= *capacity
            && (*capacity == 0U ? *first == 0U : *first < *capacity);
    };
    const auto clocks = field(h_clocks_count);
    const auto clocks_off = field(h_clocks_off);
    const auto attempts = field(h_attempts_count);
    const auto attempts_cap = field(h_attempts_cap);
    const auto requests = field(h_requests_count);
    const auto requests_cap = field(h_requests_cap);
    const auto heap_cap = field(h_heap_cap);
    const auto heap_used = field(h_heap_used);
    const auto heap_alt_used = field(h_heap_alt_used);
    const auto heap_off = field(h_heap_off);
    const auto heap_alt_off = field(h_heap_alt_off);
    if (!clocks || !clocks_off || !attempts || !attempts_cap || !requests
        || !requests_cap || !heap_cap || !heap_used || !heap_alt_used
        || !heap_off || !heap_alt_off || *clocks_off < header_words
        || *clocks_off + 3U * *clocks > total
        || !region(h_attempts_off, h_attempts_cap, attempt_stride)
        || *attempts > *attempts_cap
        || !region(h_requests_off, h_requests_cap, request_stride)
        || *requests > *requests_cap
        || !ring(h_outcomes_off, h_outcomes_head, h_outcomes_count,
            h_outcomes_cap, outcome_stride)
        || !ring(h_started_off, h_started_head, h_started_count,
            h_started_cap, started_stride)
        || !ring(h_events_off, h_events_head, h_events_count, h_events_cap,
            event_stride)
        || *heap_off < header_words || *heap_alt_off < header_words
        || *heap_off + *heap_cap > total || *heap_alt_off + *heap_cap > total
        || *heap_used > *heap_cap || *heap_alt_used > *heap_cap) {
        return std::nullopt;
    }
    const auto scratch_off = field(h_scratch_off);
    const auto scratch_cap = field(h_scratch_cap);
    const auto scratch_used = field(h_scratch_used);
    if (!scratch_off || !scratch_cap || !scratch_used
        || *scratch_off < header_words || *scratch_off + *scratch_cap > total
        || *scratch_used > *scratch_cap) {
        return std::nullopt;
    }
    const auto memo_off = field(h_memo_off);
    const auto memo_cap = field(h_memo_cap);
    const auto memo_used = field(h_memo_used);
    const auto memo_stride = field(h_memo_stride);
    const auto memo_pending = field(h_memo_pending);
    if (!memo_off || !memo_cap || !memo_used || !memo_stride || !memo_pending
        || *memo_off < header_words || *memo_off + *memo_cap > total
        || *memo_used > *memo_cap
        || (*memo_stride == 0U ? *memo_used != 0U
                               : *memo_used % *memo_stride != 0U
                    || *memo_pending > *memo_used / *memo_stride)) {
        return std::nullopt;
    }
    const auto calls_off = field(h_calls_off);
    const auto calls_cap = field(h_calls_cap);
    const auto calls_used = field(h_calls_used);
    const auto calls_taken = field(h_calls_taken);
    if (!calls_off || !calls_cap || !calls_used || !calls_taken
        || *calls_off < header_words || *calls_off + *calls_cap > total
        || *calls_used > *calls_cap || *calls_taken > *calls_used) {
        return std::nullopt;
    }
    // Each row's record lies inside the used heap.
    const auto table = static_cast<std::size_t>(words[h_attempts_off]);
    for (std::size_t index = 0; index < *attempts; ++index) {
        const auto blob = words[table + index * attempt_stride + a_blob];
        const auto length = words[table + index * attempt_stride + a_length];
        if (blob < 0 || length < 0
            || static_cast<std::uint64_t>(blob)
                    + static_cast<std::uint64_t>(length)
                > *heap_used) {
            return std::nullopt;
        }
    }
    const auto bounds_off = field(h_bounds_off);
    if (!bounds_off
        || (*bounds_off != 0U
            && (*bounds_off < header_words || *bounds_off >= total
                || words[*bounds_off] < 0
                || static_cast<std::uint64_t>(words[*bounds_off]) + 1U
                        > total - *bounds_off))) {
        return std::nullopt;
    }
    AttemptStorage result;
    result.words_ = std::move(words);
    return result;
}

StoredAttempt AttemptStorage::attempt(const std::size_t index) const noexcept
{
    const auto* row = &words_[static_cast<std::size_t>(get(h_attempts_off))
        + index * attempt_stride];
    return { row[a_started], row[a_blob], row[a_length], row[a_serial],
        row[a_flags] };
}

void AttemptStorage::set_attempt(
    const std::size_t index, const StoredAttempt& attempt) noexcept
{
    auto* row = &words_[static_cast<std::size_t>(get(h_attempts_off))
        + index * attempt_stride];
    row[a_started] = attempt.started;
    row[a_blob] = attempt.blob;
    row[a_length] = attempt.length;
    row[a_serial] = attempt.serial;
    row[a_flags] = attempt.flags;
}

void AttemptStorage::push_attempt(const StoredAttempt& attempt)
{
    if (get(h_attempts_count) == get(h_attempts_cap)) {
        auto next = caps();
        next.attempts = grown(next.attempts, next.attempts + 1U);
        relayout(next);
    }
    const auto index = attempt_count();
    set(h_attempts_count, static_cast<std::int64_t>(index + 1U));
    set_attempt(index, attempt);
}

std::size_t AttemptStorage::erase_attempts_started_at(
    const std::int64_t start) noexcept
{
    const auto count = attempt_count();
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto row = attempt(index);
        if (row.started != start) {
            set_attempt(kept++, row);
        }
    }
    truncate_attempts(kept);
    return count - kept;
}

std::span<const std::int64_t> AttemptStorage::record(
    const StoredAttempt& attempt) const noexcept
{
    return { &words_[static_cast<std::size_t>(get(h_heap_off))
                 + static_cast<std::size_t>(attempt.blob)],
        static_cast<std::size_t>(attempt.length) };
}

std::int64_t AttemptStorage::append_record(
    const std::span<const std::int64_t> words)
{
    const auto used = static_cast<std::size_t>(get(h_heap_alt_used));
    if (used + words.size() > static_cast<std::size_t>(get(h_heap_cap))) {
        auto next = caps();
        next.heap = grown(next.heap, used + words.size());
        relayout(next);
    }
    std::ranges::copy(words,
        words_.begin()
            + static_cast<std::ptrdiff_t>(get(h_heap_alt_off))
            + static_cast<std::ptrdiff_t>(used));
    set(h_heap_alt_used, static_cast<std::int64_t>(used + words.size()));
    return static_cast<std::int64_t>(used);
}

void AttemptStorage::end_pass() noexcept
{
    const auto current = get(h_heap_off);
    set(h_heap_off, get(h_heap_alt_off));
    set(h_heap_alt_off, current);
    set(h_heap_used, get(h_heap_alt_used));
    set(h_heap_alt_used, 0);
}

void AttemptStorage::clear_attempts() noexcept
{
    truncate_attempts(0);
    set(h_heap_used, 0);
    set(h_heap_alt_used, 0);
}

void AttemptStorage::ring_push(const Header off, const Header head,
    const Header count, const Header cap, std::size_t Caps::*const field,
    const std::size_t stride, const std::span<const std::int64_t> record)
{
    if (get(count) == get(cap)) {
        auto next = caps();
        next.*field = grown(next.*field, next.*field + 1U);
        relayout(next);
    }
    const auto capacity = static_cast<std::size_t>(get(cap));
    const auto slot = (static_cast<std::size_t>(get(head))
                          + static_cast<std::size_t>(get(count)))
        & (capacity - 1U);
    std::ranges::copy(record,
        words_.begin()
            + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(get(off)) + slot * stride));
    set(count, get(count) + 1);
}

bool AttemptStorage::ring_pop(const Header off, const Header head,
    const Header count, const Header cap, const std::size_t stride,
    const std::span<std::int64_t> out) noexcept
{
    if (get(count) == 0) {
        return false;
    }
    const auto capacity = static_cast<std::size_t>(get(cap));
    const auto first = static_cast<std::size_t>(get(head));
    std::copy_n(words_.begin()
            + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(get(off)) + first * stride),
        stride, out.begin());
    set(head, static_cast<std::int64_t>((first + 1U) & (capacity - 1U)));
    set(count, get(count) - 1);
    return true;
}

void AttemptStorage::push_outcome(const StoredOutcome& outcome)
{
    const std::array<std::int64_t, outcome_stride> record { outcome.outcome,
        outcome.started, outcome.fail_line, outcome.fail_column };
    ring_push(h_outcomes_off, h_outcomes_head, h_outcomes_count,
        h_outcomes_cap, &Caps::outcomes, outcome_stride, record);
}

std::optional<StoredOutcome> AttemptStorage::pop_outcome() noexcept
{
    std::array<std::int64_t, outcome_stride> record { };
    if (!ring_pop(h_outcomes_off, h_outcomes_head, h_outcomes_count,
            h_outcomes_cap, outcome_stride, record)) {
        return std::nullopt;
    }
    return StoredOutcome { record[0], record[1], record[2], record[3] };
}

void AttemptStorage::push_started(const std::int64_t start)
{
    const std::array<std::int64_t, started_stride> record { start };
    ring_push(h_started_off, h_started_head, h_started_count, h_started_cap,
        &Caps::started, started_stride, record);
}

std::vector<std::int64_t> AttemptStorage::take_started()
{
    std::vector<std::int64_t> result;
    std::array<std::int64_t, started_stride> record { };
    while (ring_pop(h_started_off, h_started_head, h_started_count,
        h_started_cap, started_stride, record)) {
        result.push_back(record[0]);
    }
    return result;
}

void AttemptStorage::push_event(const StepRecord& record)
{
    std::array<std::int64_t, event_stride> words { };
    words[e_failure] = record.failure ? 1 : 0;
    words[e_attempt_start] = record.attempt_start;
    words[e_time] = record.time;
    words[e_state_from] = record.state_from;
    words[e_state_to] = record.state_to;
    words[e_guard] = record.guard;
    words[e_sequence] = record.sequence;
    words[e_tick] = record.tick;
    words[e_guard_line] = record.guard_line;
    words[e_guard_column] = record.guard_column;
    ring_push(h_events_off, h_events_head, h_events_count, h_events_cap,
        &Caps::events, event_stride, words);
}

std::vector<StepRecord> AttemptStorage::take_events()
{
    std::vector<StepRecord> result;
    std::array<std::int64_t, event_stride> words { };
    while (ring_pop(h_events_off, h_events_head, h_events_count,
        h_events_cap, event_stride, words)) {
        result.push_back({ words[e_failure] != 0, words[e_attempt_start],
            words[e_time], words[e_state_from], words[e_state_to],
            words[e_guard], words[e_sequence], words[e_tick],
            words[e_guard_line], words[e_guard_column] });
    }
    return result;
}

std::int64_t AttemptStorage::request_start(
    const std::size_t index) const noexcept
{
    return words_[static_cast<std::size_t>(get(h_requests_off))
        + index * request_stride];
}

void AttemptStorage::push_request(
    const std::int64_t start, const std::int64_t mode)
{
    if (get(h_requests_count) == get(h_requests_cap)) {
        auto next = caps();
        next.requests = grown(next.requests, next.requests + 1U);
        relayout(next);
    }
    const auto index = request_count();
    auto* row = &words_[static_cast<std::size_t>(get(h_requests_off))
        + index * request_stride];
    row[0] = start;
    row[1] = mode;
    set(h_requests_count, static_cast<std::int64_t>(index + 1U));
}

bool AttemptStorage::erase_requests(const std::int64_t start) noexcept
{
    const auto count = request_count();
    auto* rows = &words_[static_cast<std::size_t>(get(h_requests_off))];
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (rows[index * request_stride] != start) {
            rows[kept * request_stride] = rows[index * request_stride];
            rows[kept * request_stride + 1U] = rows[index * request_stride + 1U];
            ++kept;
        }
    }
    set(h_requests_count, static_cast<std::int64_t>(kept));
    return kept != count;
}

void AttemptStorage::expire_requests(const std::int64_t time) noexcept
{
    const auto count = request_count();
    auto* rows = &words_[static_cast<std::size_t>(get(h_requests_off))];
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (rows[index * request_stride] >= time) {
            rows[kept * request_stride] = rows[index * request_stride];
            rows[kept * request_stride + 1U] = rows[index * request_stride + 1U];
            ++kept;
        }
    }
    set(h_requests_count, static_cast<std::int64_t>(kept));
}

void AttemptStorage::set_bounds(const std::vector<std::int64_t>& bounds)
{
    auto next = caps();
    next.bounds = bounds.size() + 1U;
    relayout(next);
    auto* region = &words_[static_cast<std::size_t>(get(h_bounds_off))];
    region[0] = static_cast<std::int64_t>(bounds.size());
    std::ranges::copy(bounds, region + 1);
}

void AttemptStorage::reserve_region(
    const std::size_t region, const std::size_t needed)
{
    auto next = caps();
    std::size_t* field = nullptr;
    switch (region) {
    case grow_attempts:
        field = &next.attempts;
        break;
    case grow_outcomes:
        field = &next.outcomes;
        break;
    case grow_started:
        field = &next.started;
        break;
    case grow_events:
        field = &next.events;
        break;
    case grow_requests:
        field = &next.requests;
        break;
    case grow_heap:
        field = &next.heap;
        break;
    case grow_scratch:
        field = &next.scratch;
        break;
    case grow_memo:
        field = &next.memo;
        break;
    case grow_calls:
        field = &next.calls;
        break;
    default:
        return;
    }
    if (*field >= needed) {
        return;
    }
    *field = grown(*field, needed);
    relayout(next);
}

void AttemptStorage::push_memo(const std::span<const std::int64_t> entry)
{
    const auto used = static_cast<std::size_t>(get(h_memo_used));
    reserve_region(grow_memo, used + entry.size());
    std::ranges::copy(entry,
        words_.begin()
            + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(get(h_memo_off)) + used));
    set(h_memo_used, static_cast<std::int64_t>(used + entry.size()));
}

void AttemptStorage::push_call(const std::span<const std::int64_t> entry)
{
    const auto used = static_cast<std::size_t>(get(h_calls_used));
    reserve_region(grow_calls, used + entry.size());
    std::ranges::copy(entry,
        words_.begin()
            + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(get(h_calls_off)) + used));
    set(h_calls_used, static_cast<std::int64_t>(used + entry.size()));
}

void AttemptStorage::restore(std::vector<std::int64_t> words) noexcept
{
    words_ = std::move(words);
    publish(words_.data());
}

void AttemptStorage::refresh_steps_active() noexcept
{
    std::int64_t active = get(h_requests_count);
    for (std::size_t index = 0; index < attempt_count(); ++index) {
        if ((attempt(index).flags & attempt_stepping) != 0) {
            ++active;
        }
    }
    set(h_steps_active, active);
}

} // namespace fsim::runtime::sva
