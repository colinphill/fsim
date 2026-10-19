// SPDX-License-Identifier: Apache-2.0

#include "fsim/semantic/sva_plan.hpp"

#include <charconv>

namespace fsim::semantic::sva {
namespace {

constexpr std::string_view version { "sva1" };

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
        }
    }
    out.put(static_cast<std::int64_t>(plan.properties.size()));
    for (const auto& property : plan.properties) {
        out.put(static_cast<std::int64_t>(property.kind));
        out.put((property.strong ? 1 : 0) | (property.overlapped ? 2 : 0)
            | (property.synchronous ? 4 : 0));
        out.put(property.sequence);
        out.put(property.guard);
        out.put(property.lo);
        out.put(property.hi);
        out.put(static_cast<std::int64_t>(property.children.size()));
        for (const auto child : property.children) {
            out.put(child);
        }
    }
    return std::move(out).finish();
}

std::optional<Plan> decode(const std::string_view text)
{
    if (!text.starts_with(version)) {
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
                    static_cast<std::int64_t>(Op::match))
                || !in.get_unsigned(instruction.a, maximum_count)
                || !in.get_unsigned(instruction.b, maximum_count)
                || !in.get_unsigned(instruction.c, maximum_count)
                || !in.get(instruction.lo) || !in.get(instruction.hi)) {
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
            || !in.get_unsigned(children, maximum_count)) {
            return std::nullopt;
        }
        property.strong = (flags & 1) != 0;
        property.overlapped = (flags & 2) != 0;
        property.synchronous = (flags & 4) != 0;
        property.children.resize(children);
        for (auto& child : property.children) {
            if (!in.get_unsigned(child, maximum_count)) {
                return std::nullopt;
            }
        }
    }
    if (!in.done() || plan.root >= plan.properties.size()) {
        return std::nullopt;
    }
    // Every reference stays inside the plan.
    for (const auto& property : plan.properties) {
        if (property.guard >= plan.guards && property.guard != 0U) {
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
            const bool guarded = instruction.op == Op::check
                || instruction.op == Op::throughout_begin;
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
                    && (instruction.b > size || instruction.c > size))) {
                return std::nullopt;
            }
        }
    }
    return plan;
}

} // namespace fsim::semantic::sva
