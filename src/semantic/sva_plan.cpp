// SPDX-License-Identifier: Apache-2.0

#include "fsim/semantic/sva_plan.hpp"

#include <charconv>

namespace fsim::semantic::sva {
namespace {

// "sva5" adds the local variables (IEEE 1800-2023 16.10) after the property
// clocks: the locals' widths, the expressions' targets, the local sets and
// each property's initialization assignments, and the number of subroutine
// calls attached to sequences (16.11); "sva4" adds the clock of each property after the guards' spans; "sva3"
// added the source span of each guard after the (possibly empty) clock guard
// list; "sva2" added the source position of each check and property; "sva1"
// text (artifacts written before) still decodes, without positions.
constexpr std::string_view version { "sva5" };
constexpr std::string_view version_clocks { "sva4" };
constexpr std::string_view version_spans { "sva3" };
constexpr std::string_view version_positions { "sva2" };
constexpr std::string_view version_without_positions { "sva1" };

class Writer {
public:
    void put(const std::int64_t value)
    {
        text_ += ' ';
        text_ += std::to_string(value);
    }
    void put(const Bound& bound)
    {
        put(static_cast<std::int64_t>(bound.mode));
        put(bound.value);
        put(bound.offset);
    }
    [[nodiscard]] std::string finish() &&
    {
        return std::string { version } + text_;
    }

private:
    std::string text_;
};

class Reader {
public:
    explicit Reader(const std::string_view text)
        : text_(text)
    {
    }

    [[nodiscard]] bool get(std::int64_t& value)
    {
        while (position_ < text_.size() && text_[position_] == ' ') {
            ++position_;
        }
        const auto* begin = text_.data() + position_;
        const auto* end = text_.data() + text_.size();
        const auto [next, error] = std::from_chars(begin, end, value);
        if (error != std::errc { } || next == begin) {
            return false;
        }
        position_ = static_cast<std::size_t>(next - text_.data());
        return true;
    }
    template <typename Value>
    [[nodiscard]] bool get_unsigned(Value& value, const std::int64_t limit)
    {
        std::int64_t raw { };
        if (!get(raw) || raw < 0 || raw > limit) {
            return false;
        }
        value = static_cast<Value>(raw);
        return true;
    }
    [[nodiscard]] bool get(Bound& bound)
    {
        std::int64_t mode { };
        if (!get(mode) || mode < 0
            || mode > static_cast<std::int64_t>(Bound::Mode::unbounded)) {
            return false;
        }
        bound.mode = static_cast<Bound::Mode>(mode);
        return get(bound.value) && get(bound.offset);
    }
    [[nodiscard]] bool done() const
    {
        return position_ == text_.size();
    }

private:
    std::string_view text_;
    std::size_t position_ { };
};

constexpr std::int64_t maximum_count = 1'000'000;

} // namespace

std::string encode(const Plan& plan)
{
    Writer out;
    out.put(plan.guards);
    out.put(plan.bounds);
    out.put(plan.every_match ? 1 : 0);
    out.put(plan.root);
    out.put(static_cast<std::int64_t>(plan.sequences.size()));
    for (const auto& sequence : plan.sequences) {
        out.put(sequence.counters);
        out.put(static_cast<std::int64_t>(sequence.code.size()));
        for (const auto& instruction : sequence.code) {
            out.put(static_cast<std::int64_t>(instruction.op));
            out.put(instruction.a);
            out.put(instruction.b);
            out.put(instruction.c);
            out.put(instruction.lo);
            out.put(instruction.hi);
            out.put(instruction.line);
            out.put(instruction.column);
        }
    }
    out.put(static_cast<std::int64_t>(plan.properties.size()));
    for (const auto& property : plan.properties) {
        out.put(static_cast<std::int64_t>(property.kind));
        out.put((property.strong ? 1 : 0) | (property.overlapped ? 2 : 0)
            | (property.synchronous ? 4 : 0)
            | (property.guard_expression ? 8 : 0));
        out.put(property.sequence);
        out.put(property.guard);
        out.put(property.lo);
        out.put(property.hi);
        out.put(property.line);
        out.put(property.column);
        out.put(static_cast<std::int64_t>(property.children.size()));
        for (const auto child : property.children) {
            out.put(child);
        }
    }
    // The clock guards (none for a singly clocked plan), then one span per
    // guard.
    out.put(static_cast<std::int64_t>(plan.clock_guards.size()));
    for (const auto guard : plan.clock_guards) {
        out.put(guard);
    }
    for (std::uint32_t guard = 0U; guard < plan.guards; ++guard) {
        const auto span = guard < plan.guard_spans.size()
            ? plan.guard_spans[guard] : GuardSpan { };
        out.put(span.line);
        out.put(span.column);
    }
    for (const auto& property : plan.properties) {
        out.put(property.clock);
    }
    out.put(static_cast<std::int64_t>(plan.locals.size()));
    for (const auto& local : plan.locals) {
        out.put(local.width);
    }
    out.put(static_cast<std::int64_t>(plan.expressions.size()));
    for (const auto& expression : plan.expressions) {
        out.put(expression.target);
    }
    out.put(static_cast<std::int64_t>(plan.local_sets.size()));
    for (const auto& set : plan.local_sets) {
        out.put(static_cast<std::int64_t>(set.size()));
        for (const auto local : set) {
            out.put(local);
        }
    }
    for (const auto& property : plan.properties) {
        out.put(static_cast<std::int64_t>(property.initializers.size()));
        for (const auto initializer : property.initializers) {
            out.put(initializer);
        }
    }
    out.put(static_cast<std::int64_t>(plan.calls));
    return std::move(out).finish();
}

std::optional<Plan> decode(const std::string_view text)
{
    const bool locals = text.starts_with(version);
    const bool property_clocks = locals || text.starts_with(version_clocks);
    const bool spans = property_clocks || text.starts_with(version_spans);
    const bool positions = spans || text.starts_with(version_positions);
    if (!positions && !text.starts_with(version_without_positions)) {
        return std::nullopt;
    }
    Reader in { text.substr(version.size()) };
    Plan plan;
    std::int64_t every_match { };
    std::size_t sequences { };
    if (!in.get_unsigned(plan.guards, maximum_count)
        || !in.get_unsigned(plan.bounds, maximum_count)
        || !in.get(every_match) || !in.get_unsigned(plan.root, maximum_count)
        || !in.get_unsigned(sequences, maximum_count)) {
        return std::nullopt;
    }
    plan.every_match = every_match != 0;
    plan.sequences.resize(sequences);
    for (auto& sequence : plan.sequences) {
        std::size_t size { };
        if (!in.get_unsigned(sequence.counters, maximum_count)
            || !in.get_unsigned(size, maximum_count)) {
            return std::nullopt;
        }
        sequence.code.resize(size);
        for (auto& instruction : sequence.code) {
            if (!in.get_unsigned(instruction.op,
                    static_cast<std::int64_t>(Op::call))
                || !in.get_unsigned(instruction.a, maximum_count)
                || !in.get_unsigned(instruction.b, maximum_count)
                || !in.get_unsigned(instruction.c, maximum_count)
                || !in.get(instruction.lo) || !in.get(instruction.hi)
                || (positions
                    && (!in.get_unsigned(instruction.line, maximum_count)
                        || !in.get_unsigned(instruction.column, maximum_count)))) {
                return std::nullopt;
            }
        }
    }
    std::size_t properties { };
    if (!in.get_unsigned(properties, maximum_count)) {
        return std::nullopt;
    }
    plan.properties.resize(properties);
    for (auto& property : plan.properties) {
        std::int64_t flags { };
        std::size_t children { };
        if (!in.get_unsigned(property.kind,
                static_cast<std::int64_t>(PropertyKind::vacuous))
            || !in.get(flags)
            || !in.get_unsigned(property.sequence, maximum_count)
            || !in.get_unsigned(property.guard, maximum_count)
            || !in.get(property.lo) || !in.get(property.hi)
            || (positions
                && (!in.get_unsigned(property.line, maximum_count)
                    || !in.get_unsigned(property.column, maximum_count)))
            || !in.get_unsigned(children, maximum_count)) {
            return std::nullopt;
        }
        property.strong = (flags & 1) != 0;
        property.overlapped = (flags & 2) != 0;
        property.synchronous = (flags & 4) != 0;
        property.guard_expression = (flags & 8) != 0;
        property.children.resize(children);
        for (auto& child : property.children) {
            if (!in.get_unsigned(child, maximum_count)) {
                return std::nullopt;
            }
        }
    }
    if (!in.done()) {
        // "sva3" always writes the clock count (0 for a singly clocked
        // plan without clock tick guards, 1 for one whose monitor also
        // evaluates between its clock's ticks); the older texts only for a
        // multiclocked plan.
        std::size_t clocks { };
        if (!in.get_unsigned(clocks, maximum_count)
            || (!spans && clocks < 2U)) {
            return std::nullopt;
        }
        plan.clock_guards.resize(clocks);
        for (auto& guard : plan.clock_guards) {
            if (!in.get_unsigned(guard, maximum_count)
                || guard >= plan.guards) {
                return std::nullopt;
            }
        }
    }
    if (spans) {
        plan.guard_spans.resize(plan.guards);
        for (auto& span : plan.guard_spans) {
            if (!in.get_unsigned(span.line, maximum_count)
                || !in.get_unsigned(span.column, maximum_count)) {
                return std::nullopt;
            }
        }
    }
    if (property_clocks) {
        // Each property's clock is one of the plan's clocks (clock 0 for a
        // singly clocked plan).
        const auto limit = static_cast<std::int64_t>(
            plan.clock_guards.empty() ? 0U : plan.clock_guards.size() - 1U);
        for (auto& property : plan.properties) {
            if (!in.get_unsigned(property.clock, limit)) {
                return std::nullopt;
            }
        }
    }
    if (locals) {
        std::size_t count { };
        if (!in.get_unsigned(count, maximum_count)) {
            return std::nullopt;
        }
        plan.locals.resize(count);
        for (auto& local : plan.locals) {
            if (!in.get_unsigned(local.width, maximum_count)) {
                return std::nullopt;
            }
        }
        if (!in.get_unsigned(count, maximum_count)) {
            return std::nullopt;
        }
        plan.expressions.resize(count);
        for (auto& expression : plan.expressions) {
            if (!in.get_unsigned(expression.target,
                    static_cast<std::int64_t>(plan.locals.size()))) {
                return std::nullopt;
            }
        }
        if (!in.get_unsigned(count, maximum_count)) {
            return std::nullopt;
        }
        plan.local_sets.resize(count);
        for (auto& set : plan.local_sets) {
            std::size_t size { };
            if (!in.get_unsigned(size, maximum_count)) {
                return std::nullopt;
            }
            set.resize(size);
            for (auto& local : set) {
                if (!in.get_unsigned(local, maximum_count)
                    || local >= plan.locals.size()) {
                    return std::nullopt;
                }
            }
        }
        for (auto& property : plan.properties) {
            if (!in.get_unsigned(count, maximum_count)) {
                return std::nullopt;
            }
            property.initializers.resize(count);
            for (auto& initializer : property.initializers) {
                if (!in.get_unsigned(initializer, maximum_count)
                    || initializer >= plan.expressions.size()
                    || plan.expressions[initializer].target == 0U) {
                    return std::nullopt;
                }
            }
        }
        if (!in.get_unsigned(plan.calls, maximum_count)) {
            return std::nullopt;
        }
    }
    if (!in.done() || plan.root >= plan.properties.size()) {
        return std::nullopt;
    }
    // Every reference stays inside the plan.
    for (const auto& property : plan.properties) {
        if (property.guard_expression
                ? property.guard >= plan.expressions.size()
                    || plan.expressions[property.guard].target != 0U
                : property.guard >= plan.guards && property.guard != 0U) {
            return std::nullopt;
        }
        if ((property.kind == PropertyKind::sequence
                || property.kind == PropertyKind::implication
                || property.kind == PropertyKind::followed_by)
            && property.sequence >= plan.sequences.size()) {
            return std::nullopt;
        }
        for (const auto child : property.children) {
            if (child >= plan.properties.size()) {
                return std::nullopt;
            }
        }
    }
    for (const auto& sequence : plan.sequences) {
        for (const auto& instruction : sequence.code) {
            const auto size = sequence.code.size();
            const bool expression = instruction.op == Op::check_expression
                || (instruction.op == Op::throughout_begin && instruction.c == 1U);
            const bool guarded = !expression
                && (instruction.op == Op::check
                    || instruction.op == Op::throughout_begin);
            const auto check_target = [&](const std::uint32_t index,
                                          const std::uint32_t target) {
                return index < plan.expressions.size()
                    && plan.expressions[index].target == target;
            };
            if ((expression && !check_target(instruction.a, 0U))
                || (instruction.op == Op::assign
                    && (instruction.a >= plan.locals.size()
                        || !check_target(instruction.b, instruction.a + 1U)
                        || instruction.c > 1U))
                || (instruction.op == Op::call && instruction.a >= plan.calls)
                || (instruction.op == Op::join_end
                    && (instruction.b > plan.local_sets.size()
                        || instruction.c > plan.local_sets.size()))) {
                return std::nullopt;
            }
            const bool counted = instruction.op == Op::counter_set
                || instruction.op == Op::counter_branch
                || instruction.op == Op::counter_increment;
            if ((guarded && instruction.a >= plan.guards)
                || (counted && instruction.a >= sequence.counters)
                || ((instruction.op == Op::fork || instruction.op == Op::jump)
                    && instruction.a > size)
                || (instruction.op == Op::counter_branch
                    && instruction.b > size)
                || (instruction.op == Op::join_begin
                    && (instruction.b > size || instruction.c > size))
                || (instruction.op == Op::clock
                    && (instruction.a >= plan.clock_guards.size()
                        || instruction.lo.mode != Bound::Mode::literal
                        || instruction.lo.value + instruction.lo.offset < 0
                        || instruction.lo.value + instruction.lo.offset
                            > 1))) {
                return std::nullopt;
            }
        }
    }
    return plan;
}

} // namespace fsim::semantic::sva
