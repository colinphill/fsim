// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/run_state_archive.hpp"

#include <span>

namespace fsim::runtime {

void RunStateArchive::fail(std::string message)
{
    if (error_.empty()) {
        error_ = message.empty() ? std::string { "run-state section is malformed" }
                                 : std::move(message);
    }
}

void RunStateArchive::bytes(const void* const data, const std::size_t size)
{
    if (out_ != nullptr && size != 0U) {
        out_->append(static_cast<const char*>(data), size);
    }
}

void RunStateArchive::read_bytes(void* const data, const std::size_t size)
{
    if (!ok()) {
        return;
    }
    if (size > in_.size()) {
        fail("run-state section ends early");
        return;
    }
    if (size != 0U) {
        std::memcpy(data, in_.data(), size);
    }
    in_.remove_prefix(size);
}

std::uint64_t RunStateArchive::count(const std::uint64_t size, const bool bounded)
{
    std::uint64_t value = size;
    if (saving()) {
        bytes(&value, sizeof(value));
        return value;
    }
    read_bytes(&value, sizeof(value));
    // Every element takes at least one byte, so a count past the bytes left
    // is a corrupt section, not an allocation to attempt.
    if (bounded && ok() && value > in_.size()) {
        fail("run-state count exceeds the section");
        return 0U;
    }
    return ok() ? value : 0U;
}

// Width plus the raw planes (two for four-state, four for Logic9), the layout
// of the kernel image.
void RunStateArchive::packed(PackedLogic4& value)
{
    std::uint8_t logic9 = value.is_logic9() ? 1U : 0U;
    std::uint64_t width = value.width();
    io(logic9);
    io(width);
    if (!ok()) {
        return;
    }
    if (saving()) {
        if (logic9 != 0U) {
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                const auto words = value.logic9_plane_words(plane);
                bytes(words.data(), words.size_bytes());
            }
        } else {
            const auto aval = value.aval_words();
            const auto bval = value.bval_words();
            bytes(aval.data(), aval.size_bytes());
            bytes(bval.data(), bval.size_bytes());
        }
        return;
    }
    if (logic9 > 1U) {
        fail("bad packed value flag");
        return;
    }
    if (width == 0U) {
        value = logic9 != 0U ? PackedLogic4::from_logic9_msb_string({ })
                             : PackedLogic4::from_msb_string({ });
        return;
    }
    const std::uint64_t planes = logic9 != 0U ? 4U : 2U;
    const std::uint64_t words = (width + 63U) / 64U;
    // The bytes must exist before any width-sized allocation.
    if (width > in_.size() * 8U || words > in_.size() / 8U / planes) {
        fail("packed value exceeds the section");
        return;
    }
    std::vector<std::uint64_t> storage(static_cast<std::size_t>(words * planes));
    read_bytes(storage.data(), storage.size() * sizeof(std::uint64_t));
    if (!ok()) {
        return;
    }
    const auto plane = [&](const std::uint64_t index) {
        return std::span<const std::uint64_t> { storage.data() + index * words,
            static_cast<std::size_t>(words) };
    };
    value = logic9 != 0U
        ? PackedLogic4::from_logic9_word_planes(static_cast<std::size_t>(width),
              plane(0U), plane(1U), plane(2U), plane(3U))
        : PackedLogic4::from_word_planes(
              static_cast<std::size_t>(width), plane(0U), plane(1U));
}

} // namespace fsim::runtime
