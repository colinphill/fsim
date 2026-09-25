// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/hierarchy_path.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using fsim::semantic::HierarchyPathId;
using fsim::semantic::HierarchyPathTable;

static_assert(!std::is_copy_constructible_v<HierarchyPathTable::Builder>);
static_assert(std::is_move_constructible_v<HierarchyPathTable::Builder>);
static_assert(std::is_constructible_v<
    HierarchyPathTable::Builder, const HierarchyPathTable&>);
static_assert(std::is_copy_constructible_v<HierarchyPathTable>);
static_assert(std::is_move_constructible_v<HierarchyPathTable>);

void test_intern_find_and_duplicate_identity()
{
    HierarchyPathTable::Builder builder;
    const auto signal = builder.intern("top.core.signal");
    const auto repeated = builder.intern("top.core.signal");

    assert(signal.valid());
    assert(signal == repeated);
    assert(builder.size() == 1U);
    assert(builder.find("top.core.signal") == signal);
    assert(builder.contains("top.core.signal"));
    assert(!builder.find("top.core.missing"));
    assert(!builder.contains("top.core.missing"));
    assert(builder.size() == 1U);
}

void test_builder_views_survive_growth_and_freeze()
{
    HierarchyPathTable::Builder builder;
    const auto root = builder.intern("top");
    const auto root_view = builder.view(root);

    for (std::size_t index = 0; index < 4096U; ++index) {
        const auto path = "top.child." + std::to_string(index);
        static_cast<void>(builder.intern(path));
    }

    assert(root_view == "top");
    assert(builder.view(root) == "top");
    auto table = std::move(builder).freeze();
    assert(table.view(root) == "top");
    assert(table.size() == 4097U);
}

void test_frozen_tables_share_owned_views_across_copy_and_move()
{
    HierarchyPathId signal;
    std::string_view retained_view;
    HierarchyPathTable copy;
    {
        HierarchyPathTable::Builder builder;
        signal = builder.intern("top.child.signal");
        auto table = std::move(builder).freeze();
        retained_view = table.view(signal);
        copy = table;
        table = HierarchyPathTable { };
    }

    assert(retained_view == "top.child.signal");
    assert(copy.view(signal) == retained_view);
    assert(copy.find("top.child.signal") == signal);
    assert(copy.contains("top.child.signal"));
    assert(!copy.find("top.child.missing"));
    auto moved = std::move(copy);
    assert(moved.view(signal) == "top.child.signal");
    assert(copy.size() == 0U);
    assert(retained_view == "top.child.signal");
}

void test_builder_extends_frozen_table_without_renumbering()
{
    HierarchyPathTable::Builder initial;
    const auto root = initial.intern("top");
    const auto child = initial.intern("top.child");
    auto original = std::move(initial).freeze();

    HierarchyPathTable::Builder extension { original };
    assert(extension.size() == original.size());
    assert(extension.find("top") == root);
    assert(extension.find("top.child") == child);
    const auto signal = extension.intern("top.child.signal");
    assert(signal.value() == original.size());
    assert(extension.intern("top") == root);

    auto extended = std::move(extension).freeze();
    assert(extended.view(root) == "top");
    assert(extended.view(child) == "top.child");
    assert(extended.view(signal) == "top.child.signal");
    assert(original.size() == 2U);
    assert(!original.find("top.child.signal"));
}

void test_builder_is_consumed_and_moved_from_builder_cannot_mutate()
{
    HierarchyPathTable::Builder source;
    const auto path = source.intern("top.source");
    HierarchyPathTable::Builder builder { std::move(source) };

    assert(builder.view(path) == "top.source");
    assert(source.size() == 0U);
    bool moved_from_rejected = false;
    try {
        static_cast<void>(source.intern("top.invalid"));
    } catch (const std::logic_error&) {
        moved_from_rejected = true;
    }
    assert(moved_from_rejected);

    auto table = std::move(builder).freeze();
    assert(table.view(path) == "top.source");
    assert(builder.size() == 0U);
    bool frozen_rejected = false;
    try {
        static_cast<void>(builder.intern("top.late"));
    } catch (const std::logic_error&) {
        frozen_rejected = true;
    }
    assert(frozen_rejected);
}

void test_invalid_handles_are_rejected()
{
    HierarchyPathTable::Builder builder;
    static_cast<void>(builder.intern("top.valid"));
    auto table = std::move(builder).freeze();

    bool invalid_rejected = false;
    try {
        static_cast<void>(table.view(HierarchyPathId { }));
    } catch (const std::out_of_range&) {
        invalid_rejected = true;
    }
    assert(invalid_rejected);

    HierarchyPathTable::Builder other_builder;
    static_cast<void>(other_builder.intern("other.only"));
    const auto other_id = other_builder.intern("other.second");
    auto other = std::move(other_builder).freeze();
    bool out_of_range_rejected = false;
    try {
        static_cast<void>(table.view(other_id));
    } catch (const std::out_of_range&) {
        out_of_range_rejected = true;
    }
    assert(out_of_range_rejected);
    assert(other.view(other_id) == "other.second");
}

} // namespace

int main()
{
    test_intern_find_and_duplicate_identity();
    test_builder_views_survive_growth_and_freeze();
    test_frozen_tables_share_owned_views_across_copy_and_move();
    test_builder_extends_frozen_table_without_renumbering();
    test_builder_is_consumed_and_moved_from_builder_cannot_mutate();
    test_invalid_handles_are_rejected();
    std::cout << "semantic hierarchy path tests passed\n";
}
