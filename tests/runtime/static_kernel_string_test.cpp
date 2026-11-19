// SPDX-License-Identifier: Apache-2.0
//
// The compiled kernel's string register (KernelString over the fsim-defined
// StaticKernelString header that generated code reads and writes): empty
// and short strings in the local bytes, growth, assignment (also from the
// string's own bytes), the terminating zero, copies and moves, and arrays
// with the header's stride.
#include "simir_static_kernel_native.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fsim::runtime::simir::KernelString;
using fsim::runtime::simir::StaticKernelString;
using fsim::runtime::simir::static_kernel_string_local_capacity;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

/// The header invariants generated code relies on.
void require_header(const KernelString& value, const std::string_view expected,
    const std::string_view message)
{
    const auto* header = value.header();
    require(header->data != nullptr, message);
    require(header->size == expected.size(), message);
    require(header->capacity >= header->size, message);
    require(std::string_view { header->data, static_cast<std::size_t>(header->size) }
            == expected,
        message);
    require(header->data[header->size] == '\0', message);
    const bool local = header->data == header->local;
    require(local == (header->capacity == static_kernel_string_local_capacity), message);
    require(value.view() == expected && value.size() == expected.size()
            && value.empty() == expected.empty(),
        message);
}

void test_layout()
{
    static_assert(offsetof(StaticKernelString, data) == 0U);
    static_assert(offsetof(StaticKernelString, size) == 8U);
    static_assert(offsetof(StaticKernelString, capacity) == 16U);
    static_assert(offsetof(StaticKernelString, local) == 24U);
    static_assert(sizeof(StaticKernelString) == 40U);
    static_assert(sizeof(KernelString) == sizeof(StaticKernelString));
    static_assert(static_kernel_string_local_capacity == 15U);
    // An array of registers is an array of headers.
    std::vector<KernelString> file(3U);
    file[1] = std::string_view { "one" };
    const auto* first = file[0].header();
    const auto* second = reinterpret_cast<const StaticKernelString*>(
        reinterpret_cast<const char*>(first) + sizeof(StaticKernelString));
    require(second == file[1].header(), "registers have the header's stride");
    require(&KernelString::of(file[1].header()) == &file[1],
        "a header names its owner");
}

void test_empty()
{
    const KernelString empty;
    require_header(empty, "", "an empty string is a local zero byte");
    require(empty.header()->data == empty.header()->local, "empty is local");
    KernelString cleared { std::string_view { "a string longer than the local bytes" } };
    cleared.clear();
    require_header(cleared, "", "a cleared string is empty and keeps its zero byte");
    require(cleared.capacity() > static_kernel_string_local_capacity,
        "clearing keeps the allocation");
}

void test_assign_and_grow()
{
    KernelString value;
    value = std::string_view { "short" };
    require_header(value, "short", "a short string stays local");
    const std::string fifteen(15U, 'x');
    value = fifteen;
    require_header(value, fifteen, "fifteen bytes are local");
    require(value.header()->data == value.header()->local, "fifteen bytes fit locally");
    const std::string sixteen(16U, 'y');
    value = sixteen;
    require_header(value, sixteen, "sixteen bytes are allocated");
    require(value.header()->data != value.header()->local, "sixteen bytes are not local");
    // Growth keeps the bytes and doubles at least.
    const auto before = value.capacity();
    value.reserve(before + 1U);
    require_header(value, sixteen, "reserve keeps the bytes");
    require(value.capacity() >= 2U * before, "reserve grows geometrically");
    const auto grown = value.capacity();
    value.reserve(3U);
    require(value.capacity() == grown, "reserve never shrinks");
    // A shorter value reuses the allocation.
    value = std::string_view { "tiny" };
    require_header(value, "tiny", "a shorter value in the allocation");
    require(value.capacity() == grown, "assignment keeps a large enough allocation");
    // Assignment from the string's own bytes.
    value = std::string_view { "abcdefghijklmnopqrstuvwxyz" };
    value.assign(value.view().substr(3U, 5U));
    require_header(value, "defgh", "assignment from its own bytes");
    value.append(value.view());
    require_header(value, "defghdefgh", "append of its own bytes");
    std::string long_text(100U, 'z');
    value.append(long_text);
    require_header(value, "defghdefgh" + long_text, "append that grows");
    value.append(value.view());
    require_header(value, "defghdefgh" + long_text + "defghdefgh" + long_text,
        "append of its own bytes that grows");
    value.append(std::string_view { });
    require(value.size() == 220U, "an empty append changes nothing");
}

void test_copy_and_move()
{
    const KernelString text { std::string_view { "a string longer than the local bytes" } };
    KernelString copy { text };
    require_header(copy, text.view(), "a copy has the bytes");
    require(copy.data() != text.data(), "a copy has its own bytes");
    require(copy == text && copy == std::string_view { text.view() }, "copies compare equal");

    KernelString moved { std::move(copy) };
    require_header(moved, text.view(), "a move takes the bytes");
    require_header(copy, "", "a moved-from string is empty");

    KernelString local { std::string_view { "local" } };
    KernelString taken { std::move(local) };
    require_header(taken, "local", "a short move copies the local bytes");
    require(taken.header()->data == taken.header()->local, "a short move stays local");
    require_header(local, "", "a moved-from short string is empty");

    KernelString target { std::string_view { "another string longer than local" } };
    target = std::move(moved);
    require_header(target, text.view(), "move assignment takes the bytes");
    require_header(moved, "", "move assignment empties the source");
    const auto& alias = target;
    target = alias;
    require_header(target, text.view(), "self assignment keeps the bytes");

    // A vector of registers that reallocates: every element keeps its value
    // and a short one points at its own local bytes.
    std::vector<KernelString> file;
    for (int index = 0; index < 40; ++index) {
        file.emplace_back(std::string(static_cast<std::size_t>(index), 'a'));
    }
    for (int index = 0; index < 40; ++index) {
        require_header(file[static_cast<std::size_t>(index)],
            std::string(static_cast<std::size_t>(index), 'a'),
            "a reallocated register keeps its value");
    }
    require(KernelString { std::string_view { "ab" } } != std::string_view { "abc" },
        "different strings differ");
}

/// What generated code does: reserve through the owner, then write the bytes,
/// the size and the terminating zero in the header.
void test_header_writes()
{
    KernelString value;
    auto* header = value.header();
    const std::string_view text { "written by generated code, longer than local" };
    KernelString::of(header).reserve(text.size());
    require(header->capacity >= text.size(), "reserve through the header's owner");
    std::memcpy(header->data, text.data(), text.size());
    header->size = text.size();
    header->data[header->size] = '\0';
    require_header(value, text, "bytes written in the header");
    KernelString copy { value };
    require_header(copy, text, "a copy of header-written bytes");
}

} // namespace

int main()
{
    test_layout();
    test_empty();
    test_assign_and_grow();
    test_copy_and_move();
    test_header_writes();
    return 0;
}
