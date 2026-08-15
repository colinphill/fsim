// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/trace_archive.hpp"

#include "fsim/runtime/fst_compression.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::app {
namespace {

    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;

    constexpr std::array<std::byte, 8> magic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' }, std::byte { 'M' },
        std::byte { 'T' }, std::byte { 'R' }, std::byte { 'C' }, std::byte { 'E' }
    };
    constexpr std::size_t envelope_size = magic.size() + 4U + 1U + 8U + 32U;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), { }, { } });
    }

    class Writer final {
    public:
        void raw(const std::span<const std::byte> bytes)
        {
            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        }

        void u8(const std::uint8_t value)
        {
            bytes_.push_back(std::byte { value });
        }

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

        [[nodiscard]] bool raw(const std::size_t count,
            std::span<const std::byte>& value)
        {
            if (count > bytes_.size() - offset_)
                return false;
            value = bytes_.subspan(offset_, count);
            offset_ += count;
            return true;
        }

        [[nodiscard]] bool u8(std::uint8_t& value)
        {
            std::span<const std::byte> bytes;
            if (!raw(1U, bytes))
                return false;
            value = std::to_integer<std::uint8_t>(bytes.front());
            return true;
        }

        [[nodiscard]] bool u32(std::uint32_t& value)
        {
            value = 0;
            for (unsigned shift = 0; shift < 32U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte))
                    return false;
                value |= static_cast<std::uint32_t>(byte) << shift;
            }
            return true;
        }

        [[nodiscard]] bool u64(std::uint64_t& value)
        {
            value = 0;
            for (unsigned shift = 0; shift < 64U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte))
                    return false;
                value |= static_cast<std::uint64_t>(byte) << shift;
            }
            return true;
        }

        [[nodiscard]] bool string(std::string& value, const std::size_t limit)
        {
            std::uint64_t count { };
            if (!u64(count) || count > limit
                || count > std::numeric_limits<std::size_t>::max())
                return false;
            std::span<const std::byte> bytes;
            if (!raw(static_cast<std::size_t>(count), bytes))
                return false;
            value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }

        [[nodiscard]] bool empty() const noexcept { return offset_ == bytes_.size(); }

    private:
        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    [[nodiscard]] bool valid_kind(const TraceArchiveKind kind) noexcept
    {
        return kind >= TraceArchiveKind::Object
            && kind <= TraceArchiveKind::Checkpoint;
    }

    [[nodiscard]] bool valid_surface(const TraceControlSurface value) noexcept
    {
        return value >= TraceControlSurface::ProjectCli
            && value <= TraceControlSurface::CppApi;
    }

    [[nodiscard]] bool valid_phase(const TraceControlPhase value) noexcept
    {
        return value >= TraceControlPhase::Compile
            && value <= TraceControlPhase::Simulate;
    }

    [[nodiscard]] bool valid_lifecycle(const TraceLifecycle value) noexcept
    {
        return value >= TraceLifecycle::Disabled
            && value <= TraceLifecycle::Failed;
    }

    [[nodiscard]] bool valid_format(const project::TraceFormat value) noexcept
    {
        return value >= project::TraceFormat::automatic
            && value <= project::TraceFormat::fst;
    }

    [[nodiscard]] bool valid_compression(
        const project::TraceCompression value) noexcept
    {
        return value >= project::TraceCompression::automatic
            && value <= project::TraceCompression::deterministic;
    }

    [[nodiscard]] bool valid_entry_kind(const TraceControlEntryKind value) noexcept
    {
        return value >= TraceControlEntryKind::Output
            && value <= TraceControlEntryKind::Lifecycle;
    }

    [[nodiscard]] bool contained_relative(const std::filesystem::path& path)
    {
        return !path.empty() && !path.is_absolute() && !path.has_root_name()
            && std::ranges::none_of(path, [](const auto& part) { return part == ".."; });
    }

    [[nodiscard]] std::filesystem::path relative_intent(
        const std::filesystem::path& output,
        const std::filesystem::path& producer_root)
    {
        auto candidate = output.lexically_normal();
        if (candidate.is_absolute() && !producer_root.empty())
            candidate = candidate.lexically_relative(producer_root.lexically_normal());
        if (!contained_relative(candidate))
            candidate = output.filename();
        return candidate.lexically_normal();
    }

    [[nodiscard]] std::string profile_identity(
        const TraceArchiveSnapshot& snapshot)
    {
        auto result = "trace-profile-v1:"
            + std::string { project::to_string(snapshot.effective_format) } + ":"
            + std::string { project::to_string(snapshot.effective_compression) };
        if (snapshot.effective_format == project::TraceFormat::fst) {
            result += ":";
            result += runtime::kFstContainerProfileIdentity;
            result += ":";
            result += snapshot.effective_compression
                    == project::TraceCompression::deterministic
                ? runtime::kFstCompressionBundleIdentity
                : runtime::kFstStoredBundleIdentity;
        }
        return result;
    }

    [[nodiscard]] std::string declaration_identity(
        const TraceArchiveSnapshot& snapshot)
    {
        support::Sha256 hash;
        hash.update("fsim-trace-declarations-v1");
        for (const auto& item : snapshot.selection) {
            hash.update("\0");
            hash.update(item);
        }
        for (const auto& entry : snapshot.report) {
            if (entry.kind == TraceControlEntryKind::Selection) {
                hash.update("\1");
                hash.update(entry.canonical_identity);
            }
        }
        return support::Sha256::hex(hash.finish());
    }

    [[nodiscard]] std::string semantic_identity(
        const TraceArchiveSnapshot& snapshot)
    {
        support::Sha256 hash;
        hash.update("fsim-trace-archive-profile-v1");
        hash.update(trace_control_surface_name(snapshot.surface));
        hash.update(trace_control_phase_name(snapshot.phase));
        hash.update(trace_lifecycle_name(snapshot.lifecycle));
        hash.update(project::to_string(snapshot.requested_format));
        hash.update(project::to_string(snapshot.effective_format));
        hash.update(project::to_string(snapshot.requested_compression));
        hash.update(project::to_string(snapshot.effective_compression));
        hash.update(support::path_to_utf8(snapshot.output_intent));
        hash.update(std::to_string(snapshot.generation));
        hash.update(snapshot.declaration_identity);
        hash.update(snapshot.profile_identity);
        return support::Sha256::hex(hash.finish());
    }

    [[nodiscard]] bool validate_snapshot(const TraceArchiveSnapshot& snapshot,
        const TraceArchiveLimits limits, std::vector<Diagnostic>& diagnostics)
    {
        if (snapshot.trace_schema != TraceControlApplication::schema_version
            || snapshot.profile_schema != TraceArchiveSnapshot::profile_version
            || !valid_surface(snapshot.surface) || !valid_phase(snapshot.phase)
            || !valid_lifecycle(snapshot.lifecycle)
            || !valid_format(snapshot.requested_format)
            || !valid_format(snapshot.effective_format)
            || !valid_compression(snapshot.requested_compression)
            || !valid_compression(snapshot.effective_compression)) {
            diagnose(diagnostics, "FSIM-TRACE-ARCHIVE-001",
                "trace archive profile is stale, unsupported or invalid");
            return false;
        }
        if (!contained_relative(snapshot.output_intent)
            || snapshot.selection.size() > limits.max_selection_count
            || snapshot.report.size() > limits.max_report_entries) {
            diagnose(diagnostics, "FSIM-TRACE-ARCHIVE-004",
                "trace archive output or collection exceeds relocation and resource limits");
            return false;
        }
        std::set<std::string_view> selections;
        std::set<std::string_view> declarations;
        std::size_t string_bytes = support::path_to_utf8(snapshot.output_intent).size();
        for (const auto& item : snapshot.selection) {
            string_bytes += item.size();
            if (item.empty() || !selections.insert(item).second) {
                diagnose(diagnostics, "FSIM-TRACE-ARCHIVE-001",
                    "trace archive selection is empty or duplicated");
                return false;
            }
        }
        for (const auto& entry : snapshot.report) {
            string_bytes += entry.name.size() + entry.value.size()
                + entry.canonical_identity.size();
            if (!valid_entry_kind(entry.kind) || entry.name.empty()
                || entry.canonical_identity.empty()
                || !declarations.insert(entry.canonical_identity).second) {
                diagnose(diagnostics, "FSIM-TRACE-ARCHIVE-001",
                    "trace archive declaration report is invalid or duplicated");
                return false;
            }
        }
        string_bytes += snapshot.semantic_identity.size()
            + snapshot.declaration_identity.size() + snapshot.profile_identity.size();
        if (string_bytes > limits.max_string_bytes
            || snapshot.profile_identity != profile_identity(snapshot)
            || snapshot.declaration_identity != declaration_identity(snapshot)
            || snapshot.semantic_identity != semantic_identity(snapshot)) {
            diagnose(diagnostics, "FSIM-TRACE-ARCHIVE-001",
                "trace archive identity or selected profile is stale or inconsistent");
            return false;
        }
        return true;
    }

    void write_snapshot(Writer& writer, const TraceArchiveSnapshot& snapshot)
    {
        writer.u32(snapshot.trace_schema);
        writer.u32(snapshot.profile_schema);
        writer.u8(static_cast<std::uint8_t>(snapshot.surface));
        writer.u8(static_cast<std::uint8_t>(snapshot.phase));
        writer.u8(static_cast<std::uint8_t>(snapshot.lifecycle));
        writer.u8(static_cast<std::uint8_t>(snapshot.requested_format));
        writer.u8(static_cast<std::uint8_t>(snapshot.effective_format));
        writer.u8(static_cast<std::uint8_t>(snapshot.requested_compression));
        writer.u8(static_cast<std::uint8_t>(snapshot.effective_compression));
        writer.string(support::path_to_utf8(snapshot.output_intent));
        writer.u64(snapshot.generation);
        writer.u64(snapshot.selection.size());
        for (const auto& item : snapshot.selection)
            writer.string(item);
        writer.u64(snapshot.report.size());
        for (const auto& entry : snapshot.report) {
            writer.u8(static_cast<std::uint8_t>(entry.kind));
            writer.string(entry.name);
            writer.string(entry.value);
            writer.string(entry.canonical_identity);
        }
        writer.string(snapshot.semantic_identity);
        writer.string(snapshot.declaration_identity);
        writer.string(snapshot.profile_identity);
    }

    [[nodiscard]] bool read_snapshot(Reader& reader, TraceArchiveSnapshot& snapshot,
        const TraceArchiveLimits limits)
    {
        std::uint8_t surface { };
        std::uint8_t phase { };
        std::uint8_t lifecycle { };
        std::uint8_t requested_format { };
        std::uint8_t effective_format { };
        std::uint8_t requested_compression { };
        std::uint8_t effective_compression { };
        std::string output;
        std::uint64_t selection_count { };
        std::uint64_t report_count { };
        if (!reader.u32(snapshot.trace_schema)
            || !reader.u32(snapshot.profile_schema) || !reader.u8(surface)
            || !reader.u8(phase) || !reader.u8(lifecycle)
            || !reader.u8(requested_format) || !reader.u8(effective_format)
            || !reader.u8(requested_compression)
            || !reader.u8(effective_compression)
            || !reader.string(output, limits.max_string_bytes)
            || !reader.u64(snapshot.generation) || !reader.u64(selection_count)
            || selection_count > limits.max_selection_count)
            return false;
        snapshot.surface = static_cast<TraceControlSurface>(surface);
        snapshot.phase = static_cast<TraceControlPhase>(phase);
        snapshot.lifecycle = static_cast<TraceLifecycle>(lifecycle);
        snapshot.requested_format = static_cast<project::TraceFormat>(requested_format);
        snapshot.effective_format = static_cast<project::TraceFormat>(effective_format);
        snapshot.requested_compression
            = static_cast<project::TraceCompression>(requested_compression);
        snapshot.effective_compression
            = static_cast<project::TraceCompression>(effective_compression);
        snapshot.output_intent = support::path_from_utf8(output);
        snapshot.selection.resize(static_cast<std::size_t>(selection_count));
        for (auto& item : snapshot.selection) {
            if (!reader.string(item, limits.max_string_bytes))
                return false;
        }
        if (!reader.u64(report_count) || report_count > limits.max_report_entries)
            return false;
        snapshot.report.resize(static_cast<std::size_t>(report_count));
        for (auto& entry : snapshot.report) {
            std::uint8_t kind { };
            if (!reader.u8(kind) || !reader.string(entry.name, limits.max_string_bytes)
                || !reader.string(entry.value, limits.max_string_bytes)
                || !reader.string(entry.canonical_identity, limits.max_string_bytes))
                return false;
            entry.kind = static_cast<TraceControlEntryKind>(kind);
        }
        return reader.string(snapshot.semantic_identity, limits.max_string_bytes)
            && reader.string(snapshot.declaration_identity, limits.max_string_bytes)
            && reader.string(snapshot.profile_identity, limits.max_string_bytes);
    }

    [[nodiscard]] std::string archive_identity(const TraceArchiveKind kind,
        const support::Sha256::Digest& digest)
    {
        return "trace-archive-v1:" + std::string { trace_archive_kind_name(kind) }
        + ":" + support::Sha256::hex(digest);
    }

} // namespace

bool TraceArchiveEncodeResult::ok() const noexcept
{
    return !archive.empty() && diagnostics.empty();
}

bool TraceArchiveDecodeResult::ok() const noexcept
{
    return !archive_identity.empty() && diagnostics.empty();
}

TraceArchiveSnapshot make_trace_archive_snapshot(
    const TraceControlApplication& application,
    const std::filesystem::path& producer_root)
{
    TraceArchiveSnapshot snapshot;
    const auto& request = application.request();
    const auto& status = application.status();
    snapshot.surface = request.surface;
    snapshot.phase = request.phase;
    snapshot.lifecycle = status.lifecycle;
    snapshot.requested_format = status.requested_format;
    snapshot.effective_format = status.effective_format;
    snapshot.requested_compression = status.requested_compression;
    snapshot.effective_compression = status.effective_compression;
    snapshot.output_intent = relative_intent(request.output, producer_root);
    snapshot.generation = status.generation;
    snapshot.selection = request.selection;
    snapshot.report.assign(application.report().begin(), application.report().end());
    snapshot.profile_identity = profile_identity(snapshot);
    snapshot.declaration_identity = declaration_identity(snapshot);
    snapshot.semantic_identity = semantic_identity(snapshot);
    return snapshot;
}

bool trace_archive_profiles_compatible(
    const TraceArchiveSnapshot& left,
    const TraceArchiveSnapshot& right) noexcept
{
    return left.profile_identity == right.profile_identity
        && left.declaration_identity == right.declaration_identity
        && left.output_intent == right.output_intent;
}

TraceControlResult restore_trace_archive_control(
    const TraceArchiveSnapshot& snapshot,
    const std::filesystem::path& consumer_root,
    const TraceControlLimits limits)
{
    TraceControlResult result;
    std::vector<Diagnostic> diagnostics;
    if (!validate_snapshot(snapshot, { }, diagnostics)) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }
    TraceControlRequest request;
    request.surface = snapshot.surface;
    request.phase = snapshot.phase;
    request.output = (consumer_root / snapshot.output_intent).lexically_normal();
    request.format = snapshot.requested_format;
    request.compression = snapshot.requested_compression;
    request.selection = snapshot.selection;
    request.lifecycle = snapshot.lifecycle == TraceLifecycle::Disabled
        ? TraceLifecycle::Disabled
        : TraceLifecycle::Configured;
    request.generation = snapshot.generation;
    request.report_limit = limits.max_report_entries;
    result = apply_trace_control(std::move(request), limits);
    if (!result.ok())
        return result;
    const auto& restored = result.application->status();
    if (restored.effective_format != snapshot.effective_format
        || restored.effective_compression != snapshot.effective_compression) {
        result.application.reset();
        diagnose(result.diagnostics, "FSIM-TRACE-ARCHIVE-003",
            "trace archive format or profile cannot be replayed by this consumer");
    }
    return result;
}

TraceArchiveEncodeResult encode_trace_archive(
    const TraceArchiveSnapshot& snapshot, const TraceArchiveKind kind,
    const TraceArchiveLimits limits)
{
    TraceArchiveEncodeResult result;
    if (!valid_kind(kind)
        || !validate_snapshot(snapshot, limits, result.diagnostics))
        return result;
    Writer payload_writer;
    write_snapshot(payload_writer, snapshot);
    auto payload = payload_writer.take();
    const auto digest = support::Sha256::digest(std::span { payload });
    Writer writer;
    writer.raw(magic);
    writer.u32(TraceArchiveSnapshot::schema_version);
    writer.u8(static_cast<std::uint8_t>(kind));
    writer.u64(payload.size());
    writer.raw(std::as_bytes(std::span { digest }));
    writer.raw(payload);
    result.archive = writer.take();
    if (result.archive.size() > limits.max_archive_bytes) {
        result.archive.clear();
        diagnose(result.diagnostics, "FSIM-TRACE-ARCHIVE-004",
            "trace archive exceeds its configured byte limit");
        return result;
    }
    result.archive_identity = archive_identity(kind, digest);
    return result;
}

TraceArchiveDecodeResult decode_trace_archive(
    const std::span<const std::byte> archive,
    const TraceArchiveKind expected_kind,
    const project::TraceFormat expected_format,
    const std::string_view expected_profile,
    const TraceArchiveLimits limits)
{
    TraceArchiveDecodeResult result;
    if (!valid_kind(expected_kind) || !valid_format(expected_format)
        || archive.size() > limits.max_archive_bytes
        || archive.size() < envelope_size) {
        diagnose(result.diagnostics, "FSIM-TRACE-ARCHIVE-002",
            "trace archive envelope or decode expectation is invalid");
        return result;
    }
    Reader reader { archive };
    std::span<const std::byte> found_magic;
    std::uint32_t schema { };
    std::uint8_t kind { };
    std::uint64_t payload_size { };
    std::span<const std::byte> expected_digest;
    if (!reader.raw(magic.size(), found_magic)
        || !std::ranges::equal(found_magic, magic) || !reader.u32(schema)
        || schema != TraceArchiveSnapshot::schema_version || !reader.u8(kind)
        || kind != static_cast<std::uint8_t>(expected_kind)
        || !reader.u64(payload_size) || !reader.raw(32U, expected_digest)
        || payload_size != archive.size() - envelope_size) {
        diagnose(result.diagnostics, "FSIM-TRACE-ARCHIVE-002",
            "trace archive is stale, future, cross-kind or malformed");
        return result;
    }
    std::span<const std::byte> payload;
    const auto digest = support::Sha256::digest(archive.last(
        static_cast<std::size_t>(payload_size)));
    if (!reader.raw(static_cast<std::size_t>(payload_size), payload)
        || !reader.empty()
        || !std::ranges::equal(expected_digest, std::as_bytes(std::span { digest }))) {
        diagnose(result.diagnostics, "FSIM-TRACE-ARCHIVE-002",
            "trace archive payload is truncated, trailing or corrupt");
        return result;
    }
    Reader payload_reader { payload };
    if (!read_snapshot(payload_reader, result.snapshot, limits)
        || !payload_reader.empty()) {
        diagnose(result.diagnostics, "FSIM-TRACE-ARCHIVE-002",
            "trace archive payload is malformed or exceeds decode limits");
        return result;
    }
    if (!validate_snapshot(result.snapshot, limits, result.diagnostics))
        return result;
    if ((expected_format != project::TraceFormat::automatic
            && result.snapshot.effective_format != expected_format)
        || (!expected_profile.empty()
            && result.snapshot.profile_identity != expected_profile)) {
        diagnose(result.diagnostics, "FSIM-TRACE-ARCHIVE-003",
            "trace archive format or profile does not match the replay request");
        return result;
    }
    result.archive_identity = archive_identity(expected_kind, digest);
    return result;
}

std::string trace_archive_hex(const std::span<const std::byte> archive)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(archive.size() * 2U);
    for (const auto byte : archive) {
        const auto value = std::to_integer<std::uint8_t>(byte);
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

std::vector<std::byte> trace_archive_from_hex(const std::string_view encoded)
{
    const auto nibble = [](const char value) -> std::uint8_t {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f')
            return static_cast<std::uint8_t>(value - 'a' + 10);
        return 0xffU;
    };
    if ((encoded.size() & 1U) != 0U)
        return { };
    std::vector<std::byte> result;
    result.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        const auto high = nibble(encoded[index]);
        const auto low = nibble(encoded[index + 1U]);
        if (high == 0xffU || low == 0xffU)
            return { };
        result.push_back(std::byte { static_cast<std::uint8_t>(
            (high << 4U) | low) });
    }
    return result;
}

std::string_view trace_archive_kind_name(const TraceArchiveKind kind) noexcept
{
    switch (kind) {
    case TraceArchiveKind::Object:
        return "object";
    case TraceArchiveKind::Design:
        return "design";
    case TraceArchiveKind::Library:
        return "library";
    case TraceArchiveKind::NativeCache:
        return "native-cache";
    case TraceArchiveKind::Checkpoint:
        return "checkpoint";
    }
    return "invalid";
}

} // namespace fsim::app
