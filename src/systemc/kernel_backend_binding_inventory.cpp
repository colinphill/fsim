// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_binding_inventory.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace fsim::systemc {
namespace {

    constexpr std::array<std::byte, 4> kMagic { std::byte { 'F' },
        std::byte { 'S' }, std::byte { 'B' }, std::byte { 'I' } };
    constexpr std::size_t kHeaderBytes = 48U;
    constexpr std::size_t kBindingBytes = 32U;
    constexpr std::size_t kTargetBytes = 44U;

    bool report_error(diagnostic::Engine& diagnostics,
        const SystemCKernelBindingCode code, const std::string_view message)
    {
        diagnostics.error(systemc_kernel_binding_diagnostic_code(code),
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

    bool valid_limits(const SystemCKernelBindingLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_bindings == 0U
            || limits.max_targets_per_binding == 0U
            || limits.max_chain_depth < 2U || limits.max_path_bytes == 0U
            || limits.max_interface_name_bytes == 0U
            || limits.max_encoded_bytes < kHeaderBytes) {
            return report_error(diagnostics,
                SystemCKernelBindingCode::resource,
                "SystemC binding inventory limits are invalid");
        }
        return true;
    }

    bool known_language(const SystemCKernelHostLanguage language) noexcept
    {
        return in_closed_range(language, SystemCKernelHostLanguage::verilog,
            SystemCKernelHostLanguage::vhdl);
    }

    const SystemCKernelChannelInventoryEntry* find_channel(
        const SystemCKernelChannelInventorySnapshot& channels,
        const std::string_view path)
    {
        const auto found = std::ranges::lower_bound(channels.channels, path, { },
            [](const auto& entry) {
                return std::string_view { entry.descriptor.canonical_path };
            });
        if (found == channels.channels.end()
            || found->descriptor.canonical_path != path) {
            return nullptr;
        }
        return &*found;
    }

    bool validate_target(const SystemCKernelBindingTarget& target,
        const std::string_view declared_path,
        const SystemCKernelBindingLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (target.final_channel_path.empty() || !target.final_channel.valid()
            || target.final_channel_path.size() > limits.max_path_bytes
            || target.chain.size() < 2U
            || target.chain.size() > limits.max_chain_depth
            || target.chain.front() != declared_path
            || target.chain.back() != target.final_channel_path
            || target.foreign_language.has_value()
                != target.foreign_endpoint.has_value()
            || (target.foreign_language
                && (!known_language(*target.foreign_language)
                    || !target.foreign_endpoint->valid()))) {
            return report_error(diagnostics,
                SystemCKernelBindingCode::topology,
                "SystemC binding target or complete chain is invalid");
        }
        auto paths = target.chain;
        if (std::ranges::any_of(paths, [&](const auto& path) {
                return path.empty() || path.size() > limits.max_path_bytes;
            })) {
            return report_error(diagnostics, SystemCKernelBindingCode::resource,
                "SystemC binding chain path exceeds its governed limit");
        }
        std::ranges::sort(paths);
        if (std::ranges::adjacent_find(paths) != paths.end()) {
            return report_error(diagnostics,
                SystemCKernelBindingCode::topology,
                "SystemC binding chain contains a cycle or repeated alias");
        }
        return true;
    }

    bool validate_descriptor(const SystemCKernelBindingDescriptor& descriptor,
        const SystemCKernelBindingLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (descriptor.declared_path.empty() || descriptor.interface_name.empty()
            || descriptor.declared_path.size() > limits.max_path_bytes
            || descriptor.interface_name.size()
                > limits.max_interface_name_bytes
            || descriptor.targets.empty()
            || descriptor.targets.size() > limits.max_targets_per_binding
            || !in_closed_range(descriptor.kind,
                SystemCKernelBindingKind::port,
                SystemCKernelBindingKind::export_interface)
            || !in_closed_range(descriptor.direction,
                SystemCKernelBindingDirection::none,
                SystemCKernelBindingDirection::inout)
            || (descriptor.kind == SystemCKernelBindingKind::port
                && descriptor.direction == SystemCKernelBindingDirection::none)
            || (descriptor.kind
                    == SystemCKernelBindingKind::export_interface
                && descriptor.direction
                    != SystemCKernelBindingDirection::none)) {
            return report_error(diagnostics, SystemCKernelBindingCode::metadata,
                "SystemC port or export metadata is incomplete or unsupported");
        }
        std::string_view previous;
        for (const auto& target : descriptor.targets) {
            if (!validate_target(
                    target, descriptor.declared_path, limits, diagnostics)
                || (!previous.empty()
                    && previous >= target.final_channel_path)) {
                return report_error(diagnostics,
                    SystemCKernelBindingCode::topology,
                    "SystemC binding targets are duplicated or unordered");
            }
            previous = target.final_channel_path;
        }
        return true;
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

    bool checked_add(std::size_t& total, const std::size_t amount) noexcept
    {
        if (amount > std::numeric_limits<std::size_t>::max() - total) {
            return false;
        }
        total += amount;
        return true;
    }

    std::optional<std::size_t> encoded_size(
        const SystemCKernelBindingInventorySnapshot& snapshot)
    {
        std::size_t result = kHeaderBytes;
        for (const auto& binding : snapshot.bindings) {
            const auto& descriptor = binding.descriptor;
            if (!checked_add(result, kBindingBytes)
                || !checked_add(result, descriptor.declared_path.size())
                || !checked_add(result, descriptor.interface_name.size())) {
                return std::nullopt;
            }
            for (const auto& target : descriptor.targets) {
                if (!checked_add(result, kTargetBytes)
                    || !checked_add(result, target.final_channel_path.size())) {
                    return std::nullopt;
                }
                for (const auto& path : target.chain) {
                    if (!checked_add(result, 4U)
                        || !checked_add(result, path.size())) {
                        return std::nullopt;
                    }
                }
            }
        }
        return result;
    }

    void write_target(Writer& writer, const SystemCKernelBindingTarget& target)
    {
        writer.id(target.final_channel);
        writer.id(target.foreign_endpoint.value_or(SystemCEndpointId { }));
        writer.u32(static_cast<std::uint32_t>(target.chain.size()));
        writer.u32(static_cast<std::uint32_t>(target.final_channel_path.size()));
        writer.u8(target.foreign_endpoint ? 1U : 0U);
        writer.u8(static_cast<std::uint8_t>(target.foreign_language.value_or(
            SystemCKernelHostLanguage { })));
        writer.u8(0U);
        writer.u8(0U);
        writer.text(target.final_channel_path);
        for (const auto& path : target.chain) {
            writer.u32(static_cast<std::uint32_t>(path.size()));
            writer.text(path);
        }
    }

    bool read_target(Reader& reader, SystemCKernelBindingTarget& target)
    {
        SystemCEndpointId foreign;
        std::uint32_t chain_count { };
        std::uint32_t final_size { };
        std::uint8_t flags { };
        std::uint8_t language { };
        std::uint8_t reserved0 { };
        std::uint8_t reserved1 { };
        if (!reader.id(target.final_channel) || !reader.id(foreign)
            || !reader.u32(chain_count) || !reader.u32(final_size)
            || !reader.u8(flags) || !reader.u8(language)
            || !reader.u8(reserved0) || !reader.u8(reserved1)
            || (flags & 0xfeU) != 0U || reserved0 != 0U || reserved1 != 0U
            || !reader.text(final_size, target.final_channel_path)) {
            return false;
        }
        if ((flags & 1U) != 0U) {
            target.foreign_endpoint = foreign;
            target.foreign_language
                = static_cast<SystemCKernelHostLanguage>(language);
        } else if (foreign.valid() || language != 0U) {
            return false;
        }
        target.chain.resize(chain_count);
        for (auto& path : target.chain) {
            std::uint32_t path_size { };
            if (!reader.u32(path_size) || !reader.text(path_size, path)) {
                return false;
            }
        }
        return true;
    }

    bool read_binding(Reader& reader, const SystemCKernelBindingLimits& limits,
        SystemCKernelBindingEntry& binding, diagnostic::Engine& diagnostics)
    {
        auto& descriptor = binding.descriptor;
        std::uint32_t path_size { };
        std::uint32_t interface_size { };
        std::uint32_t target_count { };
        std::uint8_t kind { };
        std::uint8_t direction { };
        std::uint8_t reserved0 { };
        std::uint8_t reserved1 { };
        if (!reader.id(binding.declaration) || !reader.u32(path_size)
            || !reader.u32(interface_size) || !reader.u32(target_count)
            || !reader.u8(kind) || !reader.u8(direction)
            || !reader.u8(reserved0) || !reader.u8(reserved1)
            || reserved0 != 0U || reserved1 != 0U
            || target_count > limits.max_targets_per_binding
            || !reader.text(path_size, descriptor.declared_path)
            || !reader.text(interface_size, descriptor.interface_name)) {
            return report_error(diagnostics, SystemCKernelBindingCode::metadata,
                "SystemC binding inventory entry is truncated or malformed");
        }
        descriptor.kind = static_cast<SystemCKernelBindingKind>(kind);
        descriptor.direction
            = static_cast<SystemCKernelBindingDirection>(direction);
        descriptor.targets.resize(target_count);
        for (auto& target : descriptor.targets) {
            if (!read_target(reader, target)) {
                return report_error(diagnostics,
                    SystemCKernelBindingCode::topology,
                    "SystemC binding target encoding is truncated or malformed");
            }
        }
        return true;
    }

} // namespace

SystemCKernelBindingInventory::SystemCKernelBindingInventory(
    const SystemCKernelChannelInventorySnapshot& channels,
    SystemCKernelBindingLimits limits)
    : channels_ { &channels }
    , limits_ { std::move(limits) }
{
    snapshot_.island = channels.island;
    snapshot_.hierarchy = channels.hierarchy;
}

bool SystemCKernelBindingInventory::register_binding(
    SystemCKernelBindingDescriptor descriptor,
    diagnostic::Engine& diagnostics)
{
    if (frozen_) {
        return report_error(diagnostics, SystemCKernelBindingCode::lifecycle,
            "SystemC binding inventory is already frozen");
    }
    if (!valid_limits(limits_, diagnostics)
        || snapshot_.bindings.size() >= limits_.max_bindings) {
        return report_error(diagnostics, SystemCKernelBindingCode::resource,
            "SystemC binding inventory exceeds its governed limit");
    }
    if (std::ranges::any_of(snapshot_.bindings, [&](const auto& current) {
            return current.descriptor.declared_path == descriptor.declared_path;
        })) {
        return report_error(diagnostics, SystemCKernelBindingCode::metadata,
            "SystemC port or export declaration is duplicated");
    }
    std::ranges::sort(descriptor.targets, { },
        &SystemCKernelBindingTarget::final_channel_path);
    for (auto& target : descriptor.targets) {
        const auto* channel = find_channel(*channels_, target.final_channel_path);
        if (channel == nullptr) {
            return report_error(diagnostics, SystemCKernelBindingCode::topology,
                "SystemC binding does not resolve to an inventoried channel");
        }
        target.final_channel = channel->channel;
    }
    if (!validate_descriptor(descriptor, limits_, diagnostics)) {
        return false;
    }
    const SystemCKernelProtocolLimits protocol_limits;
    const auto object = make_systemc_object_id(snapshot_.hierarchy,
        descriptor.declared_path, protocol_limits, diagnostics);
    const auto declaration = object
        ? make_systemc_endpoint_id(
              *object, "binding", protocol_limits, diagnostics)
        : std::nullopt;
    if (!declaration) {
        return false;
    }
    snapshot_.bindings.push_back({ *declaration, std::move(descriptor) });
    return true;
}

bool SystemCKernelBindingInventory::freeze(diagnostic::Engine& diagnostics)
{
    if (frozen_) {
        return report_error(diagnostics, SystemCKernelBindingCode::lifecycle,
            "SystemC binding inventory cannot be frozen twice");
    }
    std::ranges::sort(snapshot_.bindings, { }, [](const auto& entry) {
        return entry.descriptor.declared_path;
    });
    if (!validate_systemc_kernel_binding_inventory(
            snapshot_, limits_, diagnostics)) {
        return false;
    }
    frozen_ = true;
    return true;
}

bool SystemCKernelBindingInventory::frozen() const noexcept { return frozen_; }

const SystemCKernelBindingInventorySnapshot&
SystemCKernelBindingInventory::snapshot() const noexcept
{
    return snapshot_;
}

bool validate_systemc_kernel_binding_inventory(
    const SystemCKernelBindingInventorySnapshot& snapshot,
    const SystemCKernelBindingLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || !snapshot.island.valid()
        || !snapshot.hierarchy.valid()
        || snapshot.bindings.size() > limits.max_bindings) {
        return report_error(diagnostics, SystemCKernelBindingCode::metadata,
            "SystemC binding inventory identity or cardinality is invalid");
    }
    std::string_view previous;
    const SystemCKernelProtocolLimits protocol_limits;
    for (const auto& binding : snapshot.bindings) {
        const auto& descriptor = binding.descriptor;
        const auto object = make_systemc_object_id(snapshot.hierarchy,
            descriptor.declared_path, protocol_limits, diagnostics);
        const auto expected = object
            ? make_systemc_endpoint_id(
                  *object, "binding", protocol_limits, diagnostics)
            : std::nullopt;
        if (!validate_descriptor(descriptor, limits, diagnostics) || !expected
            || binding.declaration != *expected
            || (!previous.empty() && previous >= descriptor.declared_path)) {
            return report_error(diagnostics, SystemCKernelBindingCode::metadata,
                "SystemC binding inventory is noncanonical or unordered");
        }
        previous = descriptor.declared_path;
    }
    return true;
}

std::optional<std::vector<std::byte>>
serialize_systemc_kernel_binding_inventory(
    const SystemCKernelBindingInventorySnapshot& snapshot,
    const SystemCKernelBindingLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_systemc_kernel_binding_inventory(
            snapshot, limits, diagnostics)) {
        return std::nullopt;
    }
    const auto size = encoded_size(snapshot);
    if (!size || *size > limits.max_encoded_bytes
        || snapshot.bindings.size()
            > std::numeric_limits<std::uint32_t>::max()) {
        report_error(diagnostics, SystemCKernelBindingCode::resource,
            "SystemC binding inventory encoding exceeds its governed limit");
        return std::nullopt;
    }
    Writer writer { *size };
    writer.raw(kMagic);
    writer.u32(kSystemCKernelBindingInventoryVersion);
    writer.id(snapshot.island);
    writer.id(snapshot.hierarchy);
    writer.u32(static_cast<std::uint32_t>(snapshot.bindings.size()));
    writer.u32(0U);
    for (const auto& binding : snapshot.bindings) {
        const auto& descriptor = binding.descriptor;
        writer.id(binding.declaration);
        writer.u32(static_cast<std::uint32_t>(descriptor.declared_path.size()));
        writer.u32(static_cast<std::uint32_t>(descriptor.interface_name.size()));
        writer.u32(static_cast<std::uint32_t>(descriptor.targets.size()));
        writer.u8(static_cast<std::uint8_t>(descriptor.kind));
        writer.u8(static_cast<std::uint8_t>(descriptor.direction));
        writer.u8(0U);
        writer.u8(0U);
        writer.text(descriptor.declared_path);
        writer.text(descriptor.interface_name);
        for (const auto& target : descriptor.targets) {
            write_target(writer, target);
        }
    }
    return std::move(writer).take();
}

std::optional<SystemCKernelBindingInventorySnapshot>
deserialize_systemc_kernel_binding_inventory(
    const std::span<const std::byte> bytes,
    const SystemCKernelBindingLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || bytes.size() < kHeaderBytes
        || bytes.size() > limits.max_encoded_bytes) {
        report_error(diagnostics, SystemCKernelBindingCode::resource,
            "SystemC binding inventory bytes are truncated or over budget");
        return std::nullopt;
    }
    Reader reader { bytes };
    std::span<const std::byte> magic;
    std::uint32_t version { };
    std::uint32_t count { };
    std::uint32_t reserved { };
    SystemCKernelBindingInventorySnapshot snapshot;
    if (!reader.raw(kMagic.size(), magic) || !std::ranges::equal(magic, kMagic)
        || !reader.u32(version) || !reader.id(snapshot.island)
        || !reader.id(snapshot.hierarchy) || !reader.u32(count)
        || !reader.u32(reserved)
        || version != kSystemCKernelBindingInventoryVersion || reserved != 0U
        || count > limits.max_bindings
        || count > reader.remaining() / kBindingBytes) {
        report_error(diagnostics, SystemCKernelBindingCode::metadata,
            "SystemC binding inventory header is malformed or noncanonical");
        return std::nullopt;
    }
    snapshot.bindings.resize(count);
    for (auto& binding : snapshot.bindings) {
        if (!read_binding(reader, limits, binding, diagnostics)) {
            return std::nullopt;
        }
    }
    if (reader.remaining() != 0U
        || !validate_systemc_kernel_binding_inventory(
            snapshot, limits, diagnostics)) {
        if (!diagnostics.has_error()) {
            report_error(diagnostics, SystemCKernelBindingCode::metadata,
                "SystemC binding inventory has trailing bytes");
        }
        return std::nullopt;
    }
    return snapshot;
}

const char* systemc_kernel_binding_diagnostic_code(
    const SystemCKernelBindingCode code) noexcept
{
    switch (code) {
    case SystemCKernelBindingCode::none:
        return "";
    case SystemCKernelBindingCode::metadata:
        return "FSIM-SC-X001";
    case SystemCKernelBindingCode::topology:
        return "FSIM-SC-X002";
    case SystemCKernelBindingCode::lifecycle:
        return "FSIM-SC-X003";
    case SystemCKernelBindingCode::resource:
        return "FSIM-SC-X004";
    }
    return "FSIM-SC-X001";
}

} // namespace fsim::systemc
