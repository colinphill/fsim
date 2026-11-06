// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/run_state_image.hpp"

#include "fsim/support/payload_checksum.hpp"

#include <algorithm>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::string_view container_magic { "FSIMRSI1" };

[[nodiscard]] std::uint64_t hash_bytes(const std::string_view bytes) noexcept
{
    return support::Xxh64::digest(bytes);
}

} // namespace

void RunStateWriter::u8(const std::uint8_t value)
{
    out_.push_back(static_cast<char>(value));
}

void RunStateWriter::u32(const std::uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        out_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void RunStateWriter::u64(const std::uint64_t value)
{
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        out_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void RunStateWriter::text(const std::string_view value)
{
    u64(value.size());
    out_.append(value);
}

void RunStateWriter::words(const std::vector<std::uint64_t>& values)
{
    u64(values.size());
    for (const auto value : values) {
        u64(value);
    }
}

bool RunStateReader::u8(std::uint8_t& value)
{
    if (in_.empty()) {
        return false;
    }
    value = static_cast<std::uint8_t>(in_.front());
    in_.remove_prefix(1U);
    return true;
}

bool RunStateReader::u32(std::uint32_t& value)
{
    if (in_.size() < 4U) {
        return false;
    }
    value = 0U;
    for (unsigned index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in_[index]))
            << (8U * index);
    }
    in_.remove_prefix(4U);
    return true;
}

bool RunStateReader::u64(std::uint64_t& value)
{
    if (in_.size() < 8U) {
        return false;
    }
    value = 0U;
    for (unsigned index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(in_[index]))
            << (8U * index);
    }
    in_.remove_prefix(8U);
    return true;
}

bool RunStateReader::text(std::string& value)
{
    std::uint64_t size { };
    if (!u64(size) || size > in_.size()) {
        return false;
    }
    value.assign(in_.substr(0U, static_cast<std::size_t>(size)));
    in_.remove_prefix(static_cast<std::size_t>(size));
    return true;
}

bool RunStateReader::words(
    std::vector<std::uint64_t>& values, const std::uint64_t limit)
{
    std::uint64_t count { };
    // Each word takes eight bytes.
    if (!u64(count) || count > limit || count > in_.size() / 8U) {
        return false;
    }
    values.resize(static_cast<std::size_t>(count));
    for (auto& value : values) {
        if (!u64(value)) {
            return false;
        }
    }
    return true;
}

RunStateImage::RunStateImage(std::string identity, const RunStateImageKind kind)
    : identity_ { std::move(identity) }
    , kind_ { kind }
{
}

void RunStateImage::put(
    std::string name, const std::uint32_t version, std::string bytes)
{
    RunStateSection section;
    section.hash = hash_bytes(bytes);
    section.version = version;
    section.bytes = std::move(bytes);
    const auto place = std::ranges::lower_bound(sections_, name, { },
        &RunStateSection::name);
    section.name = std::move(name);
    if (place != sections_.end() && place->name == section.name) {
        *place = std::move(section);
    } else {
        sections_.insert(place, std::move(section));
    }
}

const RunStateSection* RunStateImage::find(const std::string_view name) const noexcept
{
    const auto place = std::ranges::lower_bound(sections_, name, { },
        [](const RunStateSection& section) -> std::string_view {
            return section.name;
        });
    return place != sections_.end() && place->name == name ? &*place : nullptr;
}

std::uint64_t RunStateImage::state_hash() const
{
    std::string bytes;
    RunStateWriter writer { bytes };
    writer.text(identity_);
    for (const auto& section : sections_) {
        writer.text(section.name);
        writer.u32(section.version);
        writer.u64(section.hash);
    }
    return hash_bytes(bytes);
}

RunStateImage RunStateImage::incremental_since(const RunStateImage& base) const
{
    RunStateImage increment { identity_, RunStateImageKind::incremental };
    increment.base_hash_ = base.state_hash();
    for (const auto& section : sections_) {
        const auto* const before = base.find(section.name);
        if (before == nullptr || before->hash != section.hash
            || before->version != section.version) {
            increment.sections_.push_back(section);
        }
    }
    for (const auto& section : base.sections_) {
        if (find(section.name) == nullptr) {
            increment.removed_.push_back(section.name);
        }
    }
    return increment;
}

RunStateImageError RunStateImage::apply(const RunStateImage& increment)
{
    if (kind_ != RunStateImageKind::full
        || increment.kind_ != RunStateImageKind::incremental) {
        return RunStateImageError::wrong_kind;
    }
    if (increment.identity_ != identity_) {
        return RunStateImageError::identity_mismatch;
    }
    if (increment.base_hash_ != state_hash()) {
        return RunStateImageError::base_mismatch;
    }
    for (const auto& name : increment.removed_) {
        std::erase_if(sections_,
            [&](const RunStateSection& section) { return section.name == name; });
    }
    for (const auto& section : increment.sections_) {
        put(section.name, section.version, section.bytes);
    }
    return RunStateImageError::none;
}

std::string RunStateImage::serialize() const
{
    std::string out;
    out.append(container_magic);
    RunStateWriter writer { out };
    writer.u32(format_version);
    writer.u8(static_cast<std::uint8_t>(kind_));
    writer.u64(base_hash_);
    writer.text(identity_);
    writer.u64(removed_.size());
    for (const auto& name : removed_) {
        writer.text(name);
    }
    writer.u64(sections_.size());
    for (const auto& section : sections_) {
        writer.text(section.name);
        writer.u32(section.version);
        writer.u64(section.hash);
        writer.text(section.bytes);
    }
    writer.u64(hash_bytes(out));
    return out;
}

std::optional<RunStateImage> RunStateImage::parse(
    const std::string_view bytes, RunStateImageError& error)
{
    error = RunStateImageError::malformed;
    if (bytes.size() < container_magic.size() + 8U
        || bytes.substr(0U, container_magic.size()) != container_magic) {
        return std::nullopt;
    }
    const auto body = bytes.substr(0U, bytes.size() - 8U);
    RunStateReader trailer { bytes.substr(bytes.size() - 8U) };
    std::uint64_t recorded { };
    if (!trailer.u64(recorded)) {
        return std::nullopt;
    }
    RunStateReader in { body.substr(container_magic.size()) };
    std::uint32_t version { };
    std::uint8_t kind_byte { };
    std::uint64_t base { };
    std::string made_for;
    if (!in.u32(version) || !in.u8(kind_byte) || !in.u64(base) || !in.text(made_for)
        || kind_byte > static_cast<std::uint8_t>(RunStateImageKind::incremental)) {
        return std::nullopt;
    }
    if (version > format_version) {
        error = RunStateImageError::unsupported_format;
        return std::nullopt;
    }
    if (hash_bytes(body) != recorded) {
        error = RunStateImageError::corrupt;
        return std::nullopt;
    }
    RunStateImage image { std::move(made_for),
        static_cast<RunStateImageKind>(kind_byte) };
    image.base_hash_ = base;
    std::uint64_t count { };
    if (!in.u64(count) || count > body.size()) {
        return std::nullopt;
    }
    for (std::uint64_t index = 0U; index < count; ++index) {
        std::string name;
        if (!in.text(name)) {
            return std::nullopt;
        }
        image.removed_.push_back(std::move(name));
    }
    if (!in.u64(count) || count > body.size()) {
        return std::nullopt;
    }
    for (std::uint64_t index = 0U; index < count; ++index) {
        RunStateSection section;
        if (!in.text(section.name) || !in.u32(section.version)
            || !in.u64(section.hash) || !in.text(section.bytes)) {
            return std::nullopt;
        }
        if (hash_bytes(section.bytes) != section.hash) {
            error = RunStateImageError::corrupt;
            return std::nullopt;
        }
        // Names are unique and in order; a writer made them so.
        if (!image.sections_.empty() && !(image.sections_.back().name < section.name)) {
            return std::nullopt;
        }
        image.sections_.push_back(std::move(section));
    }
    if (!in.done()) {
        return std::nullopt;
    }
    error = RunStateImageError::none;
    return image;
}

} // namespace fsim::runtime
