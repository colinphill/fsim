// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_effective_archive.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>

namespace fsim::app {
namespace {

    constexpr std::array<std::byte, 8> magic { std::byte { 'F' },
        std::byte { 'S' }, std::byte { 'D' }, std::byte { 'F' },
        std::byte { 'E' }, std::byte { 'F' }, std::byte { 'F' },
        std::byte { 0 } };

    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), { }, { } });
    }

    std::uint64_t checksum(const std::span<const std::byte> bytes) noexcept
    {
        std::uint64_t result = UINT64_C(14695981039346656037);
        for (const auto byte : bytes) {
            result ^= std::to_integer<std::uint8_t>(byte);
            result *= UINT64_C(1099511628211);
        }
        return result;
    }

    class Writer final {
    public:
        void raw(const std::span<const std::byte> bytes)
        {
            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        }
        void u8(const std::uint8_t value) { bytes_.push_back(std::byte { value }); }
        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0; shift < 32U; shift += 8U)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0; shift < 64U; shift += 8U)
                u8(static_cast<std::uint8_t>(value >> shift));
        }
        void string(const std::string_view value)
        {
            u64(value.size());
            raw(std::as_bytes(std::span { value }));
        }
        [[nodiscard]] std::vector<std::byte> take() { return std::move(bytes_); }

    private:
        std::vector<std::byte> bytes_;
    };

    class Reader final {
    public:
        explicit Reader(const std::span<const std::byte> bytes)
            : bytes_(bytes)
        {
        }

        bool raw(const std::size_t count, std::span<const std::byte>& result)
        {
            if (count > bytes_.size() - position_)
                return false;
            result = bytes_.subspan(position_, count);
            position_ += count;
            return true;
        }
        bool u8(std::uint8_t& result)
        {
            std::span<const std::byte> bytes;
            if (!raw(1U, bytes))
                return false;
            result = std::to_integer<std::uint8_t>(bytes.front());
            return true;
        }
        bool u32(std::uint32_t& result)
        {
            result = 0U;
            for (unsigned shift = 0; shift < 32U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte))
                    return false;
                result |= static_cast<std::uint32_t>(byte) << shift;
            }
            return true;
        }
        bool u64(std::uint64_t& result)
        {
            result = 0U;
            for (unsigned shift = 0; shift < 64U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte))
                    return false;
                result |= static_cast<std::uint64_t>(byte) << shift;
            }
            return true;
        }
        bool string(std::string& result, const std::size_t limit)
        {
            std::uint64_t count { };
            if (!u64(count) || count > limit
                || count > std::numeric_limits<std::size_t>::max())
                return false;
            std::span<const std::byte> bytes;
            if (!raw(static_cast<std::size_t>(count), bytes))
                return false;
            result.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }
        [[nodiscard]] bool empty() const noexcept
        {
            return position_ == bytes_.size();
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t position_ { };
    };

    bool portable_identity(const std::string_view value) noexcept
    {
        if (value.empty() || value.front() == '/' || value.front() == '\\'
            || (value.size() >= 2U && value[1] == ':'))
            return false;
        std::size_t begin = 0U;
        while (begin <= value.size()) {
            const auto end = value.find_first_of("/\\", begin);
            const auto component = value.substr(begin,
                end == std::string_view::npos ? value.size() - begin : end - begin);
            if (component == "..")
                return false;
            if (end == std::string_view::npos)
                break;
            begin = end + 1U;
        }
        return true;
    }

    bool valid_kind(const SdfEffectiveArchiveKind kind) noexcept
    {
        return kind <= SdfEffectiveArchiveKind::Checkpoint;
    }

    bool validate_snapshot(const SdfEffectiveArchiveSnapshot& snapshot,
        const std::string_view producer, const SdfEffectiveArchiveLimits& limits,
        std::vector<Diagnostic>& diagnostics)
    {
        if (snapshot.generation == 0U || snapshot.control_identity.empty()
            || snapshot.effective_identity.empty()
            || snapshot.policy_identity.empty() || producer.empty()
            || snapshot.selection > SdfDelaySelection::Maximum
            || snapshot.pending_event_policy
                != SdfPendingEventPolicy::PreserveScheduledTiming
            || snapshot.timing_check_state_policy
                != SdfTimingCheckStatePolicy::PreserveHistory) {
            diagnose(diagnostics, "FSIM-SDF-EFFECTIVE-001",
                "effective SDF archive requires complete identities, generation and supported policies");
            return false;
        }
        if (snapshot.records.empty()) {
            diagnose(diagnostics, "FSIM-SDF-EFFECTIVE-001",
                "effective SDF archive requires at least one timing record");
            return false;
        }
        if (snapshot.records.size() > limits.max_records
            || limits.max_records == 0U || limits.max_values == 0U
            || limits.max_string_bytes == 0U || limits.max_archive_bytes == 0U
            || limits.max_identity_bytes == 0U) {
            diagnose(diagnostics, "FSIM-SDF-EFFECTIVE-004",
                "effective SDF archive exceeds a configured record or resource limit");
            return false;
        }
        std::size_t values { };
        std::size_t strings = snapshot.control_identity.size()
            + snapshot.effective_identity.size() + snapshot.policy_identity.size()
            + producer.size();
        std::set<std::pair<SdfEffectiveValueKind, std::string_view>> targets;
        for (const auto& record : snapshot.records) {
            if (record.kind > SdfEffectiveValueKind::TimingCheckLimit
                || record.target_identity.empty() || record.source_identity.empty()
                || record.root.empty() || record.cell_pattern.empty()
                || record.policy_identity.empty()
                || record.provenance_identity.empty()
                || record.original_values.empty()
                || record.original_values.size() != record.effective_values.size()
                || !portable_identity(record.source_identity)) {
                diagnose(diagnostics, "FSIM-SDF-EFFECTIVE-001",
                    "effective SDF record is incomplete, nonportable or has mismatched exact values");
                return false;
            }
            if (!targets.emplace(record.kind, record.target_identity).second) {
                diagnose(diagnostics, "FSIM-SDF-EFFECTIVE-003",
                    "effective SDF archive repeats a timing target identity");
                return false;
            }
            if (record.original_values.size() > limits.max_values - values) {
                diagnose(diagnostics, "FSIM-SDF-EFFECTIVE-004",
                    "effective SDF archive exceeds its exact-value limit");
                return false;
            }
            values += record.original_values.size();
            const auto added = record.target_identity.size()
                + record.source_identity.size() + record.root.size()
                + record.cell_pattern.size() + record.policy_identity.size()
                + record.provenance_identity.size();
            if (added > limits.max_string_bytes
                || strings > limits.max_string_bytes - added) {
                diagnose(diagnostics, "FSIM-SDF-EFFECTIVE-004",
                    "effective SDF archive exceeds its owned-string limit");
                return false;
            }
            strings += added;
        }
        return true;
    }

    void write_record(Writer& writer, const SdfEffectiveValueRecord& record)
    {
        writer.u8(static_cast<std::uint8_t>(record.kind));
        writer.string(record.target_identity);
        writer.string(record.source_identity);
        writer.string(record.root);
        writer.string(record.cell_pattern);
        writer.u64(record.file_precedence);
        writer.u64(record.cell_precedence);
        writer.string(record.policy_identity);
        writer.string(record.provenance_identity);
        writer.u64(record.original_values.size());
        for (std::size_t index = 0; index < record.original_values.size(); ++index) {
            writer.u64(std::bit_cast<std::uint64_t>(record.original_values[index]));
            writer.u64(std::bit_cast<std::uint64_t>(record.effective_values[index]));
        }
    }

    bool read_record(Reader& reader, SdfEffectiveValueRecord& record,
        const SdfEffectiveArchiveLimits& limits)
    {
        std::uint8_t kind { };
        std::uint64_t count { };
        if (!reader.u8(kind)
            || kind > static_cast<std::uint8_t>(SdfEffectiveValueKind::TimingCheckLimit)
            || !reader.string(record.target_identity, limits.max_string_bytes)
            || !reader.string(record.source_identity, limits.max_string_bytes)
            || !reader.string(record.root, limits.max_string_bytes)
            || !reader.string(record.cell_pattern, limits.max_string_bytes)
            || !reader.u64(record.file_precedence)
            || !reader.u64(record.cell_precedence)
            || !reader.string(record.policy_identity, limits.max_string_bytes)
            || !reader.string(record.provenance_identity, limits.max_string_bytes)
            || !reader.u64(count) || count > limits.max_values
            || count > std::numeric_limits<std::size_t>::max())
            return false;
        record.kind = static_cast<SdfEffectiveValueKind>(kind);
        record.original_values.resize(static_cast<std::size_t>(count));
        record.effective_values.resize(static_cast<std::size_t>(count));
        for (std::size_t index = 0; index < record.original_values.size(); ++index) {
            std::uint64_t original { };
            std::uint64_t effective { };
            if (!reader.u64(original) || !reader.u64(effective))
                return false;
            record.original_values[index] = std::bit_cast<std::int64_t>(original);
            record.effective_values[index] = std::bit_cast<std::int64_t>(effective);
        }
        return true;
    }

    std::string archive_identity(const SdfEffectiveArchiveKind kind,
        const std::string_view producer, const std::uint64_t generation,
        const std::uint64_t digest)
    {
        return "sdf-effective-v1:" + std::string { sdf_effective_archive_kind_name(kind) }
        + ":" + std::string { producer } + ":" + std::to_string(generation)
            + ":" + std::to_string(digest);
    }

} // namespace

bool SdfEffectiveArchiveEncodeResult::ok() const noexcept
{
    return !archive.empty() && diagnostics.empty();
}

bool SdfEffectiveArchiveDecodeResult::ok() const noexcept
{
    return snapshot.generation != 0U && diagnostics.empty();
}

std::string_view sdf_effective_archive_kind_name(
    const SdfEffectiveArchiveKind kind) noexcept
{
    switch (kind) {
    case SdfEffectiveArchiveKind::Object:
        return "object";
    case SdfEffectiveArchiveKind::Design:
        return "design";
    case SdfEffectiveArchiveKind::Library:
        return "library";
    case SdfEffectiveArchiveKind::NativeCache:
        return "native-cache";
    case SdfEffectiveArchiveKind::Checkpoint:
        return "checkpoint";
    }
    return "invalid";
}

SdfEffectiveArchiveEncodeResult encode_sdf_effective_archive(
    SdfEffectiveArchiveSnapshot snapshot, const SdfEffectiveArchiveKind kind,
    const std::string_view producer_identity,
    const SdfEffectiveArchiveLimits limits)
{
    SdfEffectiveArchiveEncodeResult result;
    if (!valid_kind(kind)) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-001",
            "effective SDF archive kind is invalid");
        return result;
    }
    if (!validate_snapshot(snapshot, producer_identity, limits,
            result.diagnostics))
        return result;
    std::ranges::sort(snapshot.records, [](const auto& left, const auto& right) {
        return std::tie(left.kind, left.target_identity, left.source_identity)
            < std::tie(right.kind, right.target_identity, right.source_identity);
    });
    Writer payload;
    payload.string(producer_identity);
    payload.string(snapshot.control_identity);
    payload.string(snapshot.effective_identity);
    payload.u64(snapshot.generation);
    payload.u8(static_cast<std::uint8_t>(snapshot.selection));
    payload.u8(static_cast<std::uint8_t>(snapshot.pending_event_policy));
    payload.u8(static_cast<std::uint8_t>(snapshot.timing_check_state_policy));
    payload.string(snapshot.policy_identity);
    payload.u64(snapshot.records.size());
    for (const auto& record : snapshot.records)
        write_record(payload, record);
    auto payload_bytes = payload.take();
    const auto digest = checksum(payload_bytes);
    Writer archive;
    archive.raw(magic);
    archive.u32(SdfEffectiveArchiveSnapshot::schema_version);
    archive.u8(static_cast<std::uint8_t>(kind));
    archive.u64(payload_bytes.size());
    archive.u64(digest);
    archive.raw(payload_bytes);
    result.archive = archive.take();
    result.archive_identity = archive_identity(
        kind, producer_identity, snapshot.generation, digest);
    if (result.archive.size() > limits.max_archive_bytes
        || result.archive_identity.size() > limits.max_identity_bytes) {
        result.archive.clear();
        result.archive_identity.clear();
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-004",
            "effective SDF archive or identity exceeds its configured limit");
    }
    return result;
}

SdfEffectiveArchiveDecodeResult decode_sdf_effective_archive(
    const std::span<const std::byte> archive,
    const SdfEffectiveArchiveKind expected_kind,
    const std::string_view expected_producer_identity,
    const std::string_view expected_policy_identity,
    const SdfEffectiveArchiveLimits limits)
{
    SdfEffectiveArchiveDecodeResult result;
    if (!valid_kind(expected_kind) || expected_producer_identity.empty()
        || expected_policy_identity.empty() || archive.size() > limits.max_archive_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-001",
            "effective SDF archive decode requires valid expectations and limits");
        return result;
    }
    Reader reader { archive };
    std::span<const std::byte> found_magic;
    std::uint32_t schema { };
    std::uint8_t kind { };
    std::uint64_t payload_size { };
    std::uint64_t expected_checksum { };
    if (!reader.raw(magic.size(), found_magic)
        || !std::ranges::equal(found_magic, magic) || !reader.u32(schema)
        || schema != SdfEffectiveArchiveSnapshot::schema_version
        || !reader.u8(kind)
        || kind != static_cast<std::uint8_t>(expected_kind)
        || !reader.u64(payload_size) || !reader.u64(expected_checksum)
        || payload_size != archive.size() - 29U) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-002",
            "effective SDF archive has a stale, unsupported or malformed envelope");
        return result;
    }
    std::span<const std::byte> payload;
    if (!reader.raw(static_cast<std::size_t>(payload_size), payload)
        || !reader.empty() || checksum(payload) != expected_checksum) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-002",
            "effective SDF archive payload is truncated or corrupt");
        return result;
    }
    Reader payload_reader { payload };
    std::string producer;
    std::uint8_t selection { };
    std::uint8_t pending { };
    std::uint8_t checks { };
    std::uint64_t record_count { };
    if (!payload_reader.string(producer, limits.max_string_bytes)
        || !payload_reader.string(result.snapshot.control_identity,
            limits.max_string_bytes)
        || !payload_reader.string(result.snapshot.effective_identity,
            limits.max_string_bytes)
        || !payload_reader.u64(result.snapshot.generation)
        || !payload_reader.u8(selection) || !payload_reader.u8(pending)
        || !payload_reader.u8(checks)
        || !payload_reader.string(result.snapshot.policy_identity,
            limits.max_string_bytes)
        || !payload_reader.u64(record_count) || record_count > limits.max_records
        || record_count > std::numeric_limits<std::size_t>::max()) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-002",
            "effective SDF archive payload is malformed or exceeds decode limits");
        return result;
    }
    result.snapshot.selection = static_cast<SdfDelaySelection>(selection);
    result.snapshot.pending_event_policy
        = static_cast<SdfPendingEventPolicy>(pending);
    result.snapshot.timing_check_state_policy
        = static_cast<SdfTimingCheckStatePolicy>(checks);
    result.snapshot.records.resize(static_cast<std::size_t>(record_count));
    for (auto& record : result.snapshot.records) {
        if (!read_record(payload_reader, record, limits)) {
            diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-002",
                "effective SDF archive timing record is malformed");
            return result;
        }
    }
    if (!payload_reader.empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-002",
            "effective SDF archive contains trailing payload data");
        return result;
    }
    if (producer != expected_producer_identity
        || result.snapshot.policy_identity != expected_policy_identity) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-003",
            "effective SDF archive producer or policy does not match the consumer");
        return result;
    }
    if (!validate_snapshot(result.snapshot, producer, limits,
            result.diagnostics))
        return result;
    result.archive_identity = archive_identity(expected_kind, producer,
        result.snapshot.generation, expected_checksum);
    if (result.archive_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-EFFECTIVE-004",
            "effective SDF archive identity exceeds its configured limit");
        result.archive_identity.clear();
    }
    return result;
}

} // namespace fsim::app
