// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

/// The run-state image: the state of a run at a quiescent point (the end of
/// a time step), in named sections, each versioned and hashed with xxh64.
/// Save and reset write one (IEEE 1800-2023 D.8-D.10, 38.4 vpiReset;
/// IEEE 1076-2019 20.7-20.9); restart and reset read it back. A full image
/// holds every section. An incremental image holds only the sections whose
/// hash differs from a base image, plus the names of sections the base has
/// and it lacks, and names the base by its state hash ($incsave, D.10).
///
/// Section names used by the engine ("kernel.values", ...) are listed where
/// the section is written; the image itself does not interpret them.
enum class RunStateImageKind : std::uint8_t { full, incremental };

enum class RunStateImageError : std::uint8_t {
    none,
    /// Not an image of this format (magic, or truncated).
    malformed,
    /// A newer format version than this build reads.
    unsupported_format,
    /// A section's bytes do not match its recorded hash, or the trailer
    /// checksum fails.
    corrupt,
    /// The image was made for another design or build.
    identity_mismatch,
    /// apply(): the incremental image's base is not this image.
    base_mismatch,
    /// apply() on a non-full image, or with a non-incremental argument.
    wrong_kind
};

struct RunStateSection {
    std::string name;
    /// The writer's version of the section's own layout.
    std::uint32_t version { };
    std::uint64_t hash { };
    std::string bytes;
};

class RunStateImage final {
public:
    /// The format version of the container (not of the sections).
    static constexpr std::uint32_t format_version = 1U;

    explicit RunStateImage(std::string identity = { },
        RunStateImageKind kind = RunStateImageKind::full);

    [[nodiscard]] RunStateImageKind kind() const noexcept { return kind_; }
    /// What the image was made for: the design snapshot and kernel image
    /// digests, so a restore into another build is refused.
    [[nodiscard]] const std::string& identity() const noexcept { return identity_; }
    [[nodiscard]] const std::vector<RunStateSection>& sections() const noexcept
    {
        return sections_;
    }
    /// An incremental image: the state hash of the image it extends.
    [[nodiscard]] std::uint64_t base_hash() const noexcept { return base_hash_; }
    /// An incremental image: the sections the base has and this state lacks.
    [[nodiscard]] const std::vector<std::string>& removed() const noexcept
    {
        return removed_;
    }

    /// Adds a section, or replaces the one of that name; its hash is the
    /// xxh64 of its bytes.
    void put(std::string name, std::uint32_t version, std::string bytes);
    [[nodiscard]] const RunStateSection* find(std::string_view name) const noexcept;

    /// The hash of the whole state: the identity and, in name order, every
    /// section's name, version and hash. Equal states have equal hashes.
    [[nodiscard]] std::uint64_t state_hash() const;

    /// The incremental image from `base` to this image: the sections that
    /// are new or whose hash changed ($incsave).
    [[nodiscard]] RunStateImage incremental_since(const RunStateImage& base) const;
    /// This (full) image with `increment` applied; `increment` must name
    /// this image's state hash as its base.
    [[nodiscard]] RunStateImageError apply(const RunStateImage& increment);

    [[nodiscard]] std::string serialize() const;
    /// The image in `bytes`, or nullopt with `error` set.
    [[nodiscard]] static std::optional<RunStateImage> parse(
        std::string_view bytes, RunStateImageError& error);

private:
    std::string identity_;
    RunStateImageKind kind_ { RunStateImageKind::full };
    std::uint64_t base_hash_ { };
    std::vector<std::string> removed_;
    /// Sorted by name.
    std::vector<RunStateSection> sections_;
};

/// A section's bytes: fixed-width little-endian fields, so the same state
/// always hashes the same.
class RunStateWriter final {
public:
    explicit RunStateWriter(std::string& out) : out_ { out } { }
    void u8(std::uint8_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void text(std::string_view value);
    void words(const std::vector<std::uint64_t>& values);

private:
    std::string& out_;
};

class RunStateReader final {
public:
    explicit RunStateReader(std::string_view bytes) : in_ { bytes } { }
    [[nodiscard]] bool u8(std::uint8_t& value);
    [[nodiscard]] bool u32(std::uint32_t& value);
    [[nodiscard]] bool u64(std::uint64_t& value);
    [[nodiscard]] bool text(std::string& value);
    /// The count and words; false past the end or above `limit` words.
    [[nodiscard]] bool words(std::vector<std::uint64_t>& values,
        std::uint64_t limit = std::uint64_t { 1 } << 32U);
    [[nodiscard]] bool done() const noexcept { return in_.empty(); }

private:
    std::string_view in_;
};

} // namespace fsim::runtime
