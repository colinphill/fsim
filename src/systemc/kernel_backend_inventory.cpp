// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_inventory.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 4> kMagic { std::byte { 'F' },
        std::byte { 'S' }, std::byte { 'C' }, std::byte { 'I' } };
    constexpr std::size_t kHeaderBytes = 48U;
    constexpr std::size_t kEntryBytes = 52U;

    bool report_error(diagnostic::Engine& diagnostics,
        const SystemCKernelInventoryCode code, const std::string_view message)
    {
        diagnostics.error(systemc_kernel_inventory_diagnostic_code(code),
            std::string { message });
        return false;
    }

    template <typename Enum>
    bool in_closed_range(const Enum value, const Enum first, const Enum last) noexcept
    {
        const auto raw = static_cast<std::uint8_t>(value);
        return raw >= static_cast<std::uint8_t>(first)
            && raw <= static_cast<std::uint8_t>(last);
    }

    bool valid_limits(const SystemCKernelInventoryLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_channels == 0U || limits.max_path_bytes == 0U
            || limits.max_type_name_bytes == 0U
            || limits.max_encoded_bytes < kHeaderBytes) {
            return report_error(diagnostics,
                SystemCKernelInventoryCode::resource,
                "SystemC channel inventory limits are invalid");
        }
        return true;
    }

    bool signal_kind(const SystemCKernelChannelKind kind) noexcept
    {
        return kind >= SystemCKernelChannelKind::signal
            && kind <= SystemCKernelChannelKind::resolved_vector;
    }

    bool known_value_kind(const SystemCKernelValueKind kind) noexcept
    {
        return in_closed_range(kind, SystemCKernelValueKind::bit2,
            SystemCKernelValueKind::time);
    }

    bool known_descriptor_enums(
        const SystemCKernelChannelDescriptor& descriptor) noexcept
    {
        return in_closed_range(descriptor.kind, SystemCKernelChannelKind::signal,
                   SystemCKernelChannelKind::custom)
            && in_closed_range(descriptor.writer, SystemCKernelWriterPolicy::none,
                SystemCKernelWriterPolicy::unchecked)
            && in_closed_range(descriptor.update_owner,
                SystemCKernelUpdateOwner::signal_kernel,
                SystemCKernelUpdateOwner::custom)
            && in_closed_range(descriptor.observation,
                SystemCKernelObservationMode::value_changed,
                SystemCKernelObservationMode::unsupported);
    }

    class Writer {
    public:
        explicit Writer(const std::size_t reserve) { bytes_.reserve(reserve); }

        void u8(const std::uint8_t value) { bytes_.push_back(std::byte { value }); }
        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }
        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }
        template <typename Domain>
        void id(const SystemCBackendId<Domain> value)
        {
            u64(value.high);
            u64(value.low);
        }
        void raw(const std::span<const std::byte> bytes)
        {
            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        }
        void text(const std::string_view value)
        {
            raw(std::as_bytes(std::span { value.data(), value.size() }));
        }
        [[nodiscard]] std::vector<std::byte> take() &&
        {
            return std::move(bytes_);
        }

    private:
        std::vector<std::byte> bytes_;
    };

    class Reader {
    public:
        explicit Reader(const std::span<const std::byte> bytes)
            : bytes_ { bytes }
        {
        }

        bool u8(std::uint8_t& value)
        {
            if (offset_ >= bytes_.size()) {
                return false;
            }
            value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
            return true;
        }
        bool u32(std::uint32_t& value)
        {
            value = 0U;
            for (unsigned shift = 0U; shift < 32U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte)) {
                    return false;
                }
                value |= static_cast<std::uint32_t>(byte) << shift;
            }
            return true;
        }
        bool u64(std::uint64_t& value)
        {
            value = 0U;
            for (unsigned shift = 0U; shift < 64U; shift += 8U) {
                std::uint8_t byte { };
                if (!u8(byte)) {
                    return false;
                }
                value |= static_cast<std::uint64_t>(byte) << shift;
            }
            return true;
        }
        template <typename Domain>
        bool id(SystemCBackendId<Domain>& value)
        {
            return u64(value.high) && u64(value.low);
        }
        bool raw(const std::size_t size, std::span<const std::byte>& result)
        {
            if (size > remaining()) {
                return false;
            }
            result = bytes_.subspan(offset_, size);
            offset_ += size;
            return true;
        }
        bool text(const std::size_t size, std::string& result)
        {
            std::span<const std::byte> source;
            if (!raw(size, source)) {
                return false;
            }
            result.assign(
                reinterpret_cast<const char*>(source.data()), source.size());
            return true;
        }
        [[nodiscard]] std::size_t remaining() const noexcept
        {
            return bytes_.size() - offset_;
        }

    private:
        std::span<const std::byte> bytes_;
        std::size_t offset_ { };
    };

    std::optional<std::size_t> encoded_size(
        const SystemCKernelChannelInventorySnapshot& snapshot)
    {
        std::size_t result = kHeaderBytes;
        for (const auto& channel : snapshot.channels) {
            const auto& descriptor = channel.descriptor;
            if (descriptor.canonical_path.size()
                    > std::numeric_limits<std::uint32_t>::max()
                || descriptor.type_name.size()
                    > std::numeric_limits<std::uint32_t>::max()
                || descriptor.canonical_path.size()
                    > std::numeric_limits<std::size_t>::max() - kEntryBytes
                || descriptor.type_name.size()
                    > std::numeric_limits<std::size_t>::max() - kEntryBytes
                        - descriptor.canonical_path.size()
                || result > std::numeric_limits<std::size_t>::max()
                        - kEntryBytes - descriptor.canonical_path.size()
                        - descriptor.type_name.size()) {
                return std::nullopt;
            }
            result += kEntryBytes + descriptor.canonical_path.size()
                + descriptor.type_name.size();
        }
        return result;
    }

    bool read_entry(Reader& reader, SystemCKernelChannelInventoryEntry& entry)
    {
        std::uint32_t path_size { };
        std::uint32_t type_size { };
        std::uint8_t kind { };
        std::uint8_t writer { };
        std::uint8_t update { };
        std::uint8_t observation { };
        std::uint8_t flags { };
        std::uint8_t value_kind { };
        std::uint8_t is_signed { };
        std::uint8_t reserved { };
        std::uint32_t width { };
        if (!reader.id(entry.object) || !reader.id(entry.channel)
            || !reader.u32(path_size) || !reader.u32(type_size)
            || !reader.u8(kind) || !reader.u8(writer) || !reader.u8(update)
            || !reader.u8(observation) || !reader.u8(flags)
            || !reader.u8(value_kind) || !reader.u8(is_signed)
            || !reader.u8(reserved) || !reader.u32(width) || reserved != 0U
            || (flags & 0xfcU) != 0U || is_signed > 1U
            || !reader.text(path_size, entry.descriptor.canonical_path)
            || !reader.text(type_size, entry.descriptor.type_name)) {
            return false;
        }
        entry.descriptor.kind = static_cast<SystemCKernelChannelKind>(kind);
        entry.descriptor.writer = static_cast<SystemCKernelWriterPolicy>(writer);
        entry.descriptor.update_owner
            = static_cast<SystemCKernelUpdateOwner>(update);
        entry.descriptor.observation
            = static_cast<SystemCKernelObservationMode>(observation);
        entry.descriptor.supported = (flags & 2U) != 0U;
        if ((flags & 1U) != 0U) {
            entry.descriptor.value = SystemCKernelChannelValueProfile {
                static_cast<SystemCKernelValueKind>(value_kind), width,
                is_signed != 0U
            };
        } else if (value_kind != 0U || width != 0U || is_signed != 0U) {
            return false;
        }
        return true;
    }

} // namespace

SystemCKernelChannelInventory::SystemCKernelChannelInventory(
    const SystemCIslandId island, const SystemCHierarchyId hierarchy,
    SystemCKernelInventoryLimits limits)
    : limits_ { std::move(limits) }
{
    snapshot_.island = island;
    snapshot_.hierarchy = hierarchy;
}

bool SystemCKernelChannelInventory::register_channel(
    SystemCKernelChannelDescriptor descriptor,
    diagnostic::Engine& diagnostics)
{
    if (frozen_) {
        return report_error(diagnostics, SystemCKernelInventoryCode::lifecycle,
            "SystemC channel inventory is already frozen");
    }
    if (!validate_systemc_kernel_channel_descriptor(
            descriptor, limits_, diagnostics)) {
        return false;
    }
    if (snapshot_.channels.size() >= limits_.max_channels) {
        return report_error(diagnostics, SystemCKernelInventoryCode::resource,
            "SystemC channel inventory exceeds its governed channel limit");
    }
    if (std::ranges::any_of(snapshot_.channels, [&](const auto& current) {
            return current.descriptor.canonical_path
                == descriptor.canonical_path;
        })) {
        return report_error(diagnostics, SystemCKernelInventoryCode::metadata,
            "SystemC channel inventory path is duplicated");
    }
    const SystemCKernelProtocolLimits protocol_limits;
    const auto object = make_systemc_object_id(snapshot_.hierarchy,
        descriptor.canonical_path, protocol_limits, diagnostics);
    const auto channel = object
        ? make_systemc_endpoint_id(
              *object, "channel", protocol_limits, diagnostics)
        : std::nullopt;
    if (!object || !channel) {
        return false;
    }
    snapshot_.channels.push_back(
        { *object, *channel, std::move(descriptor) });
    return true;
}

bool SystemCKernelChannelInventory::freeze(diagnostic::Engine& diagnostics)
{
    if (frozen_) {
        return report_error(diagnostics, SystemCKernelInventoryCode::lifecycle,
            "SystemC channel inventory cannot be frozen twice");
    }
    std::ranges::sort(snapshot_.channels, { }, [](const auto& entry) {
        return entry.descriptor.canonical_path;
    });
    if (!validate_systemc_kernel_channel_inventory(
            snapshot_, limits_, diagnostics)) {
        return false;
    }
    frozen_ = true;
    return true;
}

bool SystemCKernelChannelInventory::validate_live_snapshot(
    const std::span<const SystemCKernelChannelDescriptor> live,
    diagnostic::Engine& diagnostics) const
{
    if (!frozen_) {
        return report_error(diagnostics, SystemCKernelInventoryCode::lifecycle,
            "SystemC channel inventory must be frozen before validation");
    }
    auto ordered = std::vector<SystemCKernelChannelDescriptor> {
        live.begin(), live.end()
    };
    std::ranges::sort(ordered, { }, &SystemCKernelChannelDescriptor::canonical_path);
    if (ordered.size() != snapshot_.channels.size()
        || !std::ranges::equal(ordered, snapshot_.channels,
            std::ranges::equal_to { }, std::identity { },
            &SystemCKernelChannelInventoryEntry::descriptor)) {
        return report_error(diagnostics, SystemCKernelInventoryCode::lifecycle,
            "A frozen SystemC channel disappeared or changed after binding");
    }
    return true;
}

bool SystemCKernelChannelInventory::frozen() const noexcept { return frozen_; }

const SystemCKernelChannelInventorySnapshot&
SystemCKernelChannelInventory::snapshot() const noexcept
{
    return snapshot_;
}

bool validate_systemc_kernel_channel_descriptor(
    const SystemCKernelChannelDescriptor& descriptor,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics)) {
        return false;
    }
    if (descriptor.canonical_path.empty() || descriptor.type_name.empty()
        || descriptor.canonical_path.size() > limits.max_path_bytes
        || descriptor.type_name.size() > limits.max_type_name_bytes
        || !known_descriptor_enums(descriptor)
        || (signal_kind(descriptor.kind)
            != descriptor.value.has_value())) {
        return report_error(diagnostics, SystemCKernelInventoryCode::metadata,
            "SystemC channel inventory metadata is incomplete or unsupported");
    }
    const auto custom = descriptor.kind == SystemCKernelChannelKind::custom
        && descriptor.update_owner == SystemCKernelUpdateOwner::custom
        && descriptor.observation == SystemCKernelObservationMode::unsupported
        && !descriptor.value
        && descriptor.writer == SystemCKernelWriterPolicy::none;
    if (!descriptor.supported) {
        if (!custom) {
            return report_error(diagnostics,
                SystemCKernelInventoryCode::unsupported,
                "Unsupported SystemC channel metadata must remain explicitly visible");
        }
        return true;
    }
    if (custom) {
        return report_error(diagnostics, SystemCKernelInventoryCode::unsupported,
            "Custom SystemC channel claims support without an observation adapter");
    }
    if (descriptor.value
        && (descriptor.value->width == 0U
            || !known_value_kind(descriptor.value->kind)
            || descriptor.writer == SystemCKernelWriterPolicy::none)) {
        return report_error(diagnostics, SystemCKernelInventoryCode::metadata,
            "SystemC channel value domain or writer policy is invalid");
    }
    if (!descriptor.value && descriptor.writer != SystemCKernelWriterPolicy::none) {
        return report_error(diagnostics, SystemCKernelInventoryCode::metadata,
            "SystemC primitive channel unexpectedly declares a writer policy");
    }
    return true;
}

bool validate_systemc_kernel_channel_inventory(
    const SystemCKernelChannelInventorySnapshot& snapshot,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || !snapshot.island.valid()
        || !snapshot.hierarchy.valid()
        || snapshot.channels.size() > limits.max_channels) {
        return report_error(diagnostics, SystemCKernelInventoryCode::metadata,
            "SystemC channel inventory identity or cardinality is invalid");
    }
    std::string_view previous;
    const SystemCKernelProtocolLimits protocol_limits;
    for (const auto& channel : snapshot.channels) {
        const auto& descriptor = channel.descriptor;
        const auto expected_object = make_systemc_object_id(snapshot.hierarchy,
            descriptor.canonical_path, protocol_limits, diagnostics);
        const auto expected_channel = expected_object
            ? make_systemc_endpoint_id(
                  *expected_object, "channel", protocol_limits, diagnostics)
            : std::nullopt;
        if (!validate_systemc_kernel_channel_descriptor(
                descriptor, limits, diagnostics)
            || !expected_object || !expected_channel
            || channel.object != *expected_object
            || channel.channel != *expected_channel
            || (!previous.empty()
                && previous >= descriptor.canonical_path)) {
            return report_error(diagnostics, SystemCKernelInventoryCode::metadata,
                "SystemC channel inventory entries are noncanonical or unordered");
        }
        previous = descriptor.canonical_path;
    }
    return true;
}

std::optional<std::vector<std::byte>>
serialize_systemc_kernel_channel_inventory(
    const SystemCKernelChannelInventorySnapshot& snapshot,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_channel_inventory(
            snapshot, limits, diagnostics)) {
        return std::nullopt;
    }
    const auto size = encoded_size(snapshot);
    if (!size || *size > limits.max_encoded_bytes
        || snapshot.channels.size()
            > std::numeric_limits<std::uint32_t>::max()) {
        report_error(diagnostics, SystemCKernelInventoryCode::resource,
            "SystemC channel inventory encoding exceeds its governed limit");
        return std::nullopt;
    }
    Writer writer { *size };
    writer.raw(kMagic);
    writer.u32(kSystemCKernelInventoryVersion);
    writer.id(snapshot.island);
    writer.id(snapshot.hierarchy);
    writer.u32(static_cast<std::uint32_t>(snapshot.channels.size()));
    writer.u32(0U);
    for (const auto& channel : snapshot.channels) {
        const auto& descriptor = channel.descriptor;
        writer.id(channel.object);
        writer.id(channel.channel);
        writer.u32(static_cast<std::uint32_t>(descriptor.canonical_path.size()));
        writer.u32(static_cast<std::uint32_t>(descriptor.type_name.size()));
        writer.u8(static_cast<std::uint8_t>(descriptor.kind));
        writer.u8(static_cast<std::uint8_t>(descriptor.writer));
        writer.u8(static_cast<std::uint8_t>(descriptor.update_owner));
        writer.u8(static_cast<std::uint8_t>(descriptor.observation));
        writer.u8(static_cast<std::uint8_t>((descriptor.value ? 1U : 0U)
            | (descriptor.supported ? 2U : 0U)));
        writer.u8(static_cast<std::uint8_t>(descriptor.value
                ? descriptor.value->kind
                : SystemCKernelValueKind { }));
        writer.u8(descriptor.value && descriptor.value->is_signed ? 1U : 0U);
        writer.u8(0U);
        writer.u32(descriptor.value ? descriptor.value->width : 0U);
        writer.text(descriptor.canonical_path);
        writer.text(descriptor.type_name);
    }
    return std::move(writer).take();
}

std::optional<SystemCKernelChannelInventorySnapshot>
deserialize_systemc_kernel_channel_inventory(
    const std::span<const std::byte> bytes,
    const SystemCKernelInventoryLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || bytes.size() < kHeaderBytes
        || bytes.size() > limits.max_encoded_bytes) {
        report_error(diagnostics, SystemCKernelInventoryCode::resource,
            "SystemC channel inventory bytes are truncated or over budget");
        return std::nullopt;
    }
    Reader reader { bytes };
    std::span<const std::byte> magic;
    std::uint32_t version { };
    std::uint32_t count { };
    std::uint32_t reserved { };
    SystemCKernelChannelInventorySnapshot snapshot;
    if (!reader.raw(kMagic.size(), magic) || !std::ranges::equal(magic, kMagic)
        || !reader.u32(version) || !reader.id(snapshot.island)
        || !reader.id(snapshot.hierarchy) || !reader.u32(count)
        || !reader.u32(reserved) || version != kSystemCKernelInventoryVersion
        || reserved != 0U || count > limits.max_channels
        || count > reader.remaining() / kEntryBytes) {
        report_error(diagnostics, SystemCKernelInventoryCode::metadata,
            "SystemC channel inventory header is malformed or noncanonical");
        return std::nullopt;
    }
    snapshot.channels.resize(count);
    for (auto& entry : snapshot.channels) {
        if (!read_entry(reader, entry)) {
            report_error(diagnostics, SystemCKernelInventoryCode::metadata,
                "SystemC channel inventory entry is truncated or malformed");
            return std::nullopt;
        }
    }
    if (reader.remaining() != 0U
        || !validate_systemc_kernel_channel_inventory(
            snapshot, limits, diagnostics)) {
        if (!diagnostics.has_error()) {
            report_error(diagnostics, SystemCKernelInventoryCode::metadata,
                "SystemC channel inventory has trailing bytes");
        }
        return std::nullopt;
    }
    return snapshot;
}

const char* systemc_kernel_inventory_diagnostic_code(
    const SystemCKernelInventoryCode code) noexcept
{
    switch (code) {
    case SystemCKernelInventoryCode::none:
        return "";
    case SystemCKernelInventoryCode::metadata:
        return "FSIM-SC-W001";
    case SystemCKernelInventoryCode::lifecycle:
        return "FSIM-SC-W002";
    case SystemCKernelInventoryCode::unsupported:
        return "FSIM-SC-W003";
    case SystemCKernelInventoryCode::resource:
        return "FSIM-SC-W004";
    }
    return "FSIM-SC-W001";
}

} // namespace fsim::systemc
