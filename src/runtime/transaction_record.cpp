// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/transaction_record.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>

namespace fsim::runtime {
namespace {

    constexpr std::array<std::byte, 8> magic {
        std::byte { 'F' }, std::byte { 'S' }, std::byte { 'I' }, std::byte { 'M' },
        std::byte { 'T' }, std::byte { 'R' }, std::byte { 'N' }, std::byte { '1' }
    };

    class Writer {
    public:
        void u8(const std::uint8_t value) { bytes.push_back(std::byte { value }); }
        void u16(const std::uint16_t value)
        {
            for (unsigned shift = 0; shift < 16U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }
        void u32(const std::uint32_t value)
        {
            for (unsigned shift = 0; shift < 32U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }
        void u64(const std::uint64_t value)
        {
            for (unsigned shift = 0; shift < 64U; shift += 8U) {
                u8(static_cast<std::uint8_t>(value >> shift));
            }
        }
        void id(const TransactionStableId value)
        {
            u64(value.high);
            u64(value.low);
        }
        void string(const std::string_view value)
        {
            u32(static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(),
                reinterpret_cast<const std::byte*>(value.data()),
                reinterpret_cast<const std::byte*>(value.data() + value.size()));
        }
        std::vector<std::byte> bytes;
    };

    class Reader {
    public:
        explicit Reader(const std::span<const std::byte> input)
            : bytes(input)
        {
        }
        std::optional<std::uint8_t> u8()
        {
            if (offset >= bytes.size())
                return std::nullopt;
            return std::to_integer<std::uint8_t>(bytes[offset++]);
        }
        std::optional<std::uint16_t> u16()
        {
            std::uint16_t result { };
            for (unsigned shift = 0; shift < 16U; shift += 8U) {
                const auto value = u8();
                if (!value)
                    return std::nullopt;
                result |= static_cast<std::uint16_t>(*value) << shift;
            }
            return result;
        }
        std::optional<std::uint32_t> u32()
        {
            std::uint32_t result { };
            for (unsigned shift = 0; shift < 32U; shift += 8U) {
                const auto value = u8();
                if (!value)
                    return std::nullopt;
                result |= static_cast<std::uint32_t>(*value) << shift;
            }
            return result;
        }
        std::optional<std::uint64_t> u64()
        {
            std::uint64_t result { };
            for (unsigned shift = 0; shift < 64U; shift += 8U) {
                const auto value = u8();
                if (!value)
                    return std::nullopt;
                result |= static_cast<std::uint64_t>(*value) << shift;
            }
            return result;
        }
        std::optional<TransactionStableId> id()
        {
            const auto high = u64();
            const auto low = u64();
            if (!high || !low)
                return std::nullopt;
            return TransactionStableId { *high, *low };
        }
        std::optional<std::string> string(const std::size_t maximum)
        {
            const auto size = u32();
            if (!size || *size > maximum || *size > bytes.size() - offset) {
                return std::nullopt;
            }
            const auto* first = reinterpret_cast<const char*>(bytes.data() + offset);
            std::string result { first, first + *size };
            offset += *size;
            return result;
        }
        std::span<const std::byte> bytes;
        std::size_t offset { };
    };

    bool valid_limits(
        const TransactionRecordLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        const auto bounded = [](const std::size_t value) {
            return value != 0U
                && value <= std::numeric_limits<std::uint32_t>::max();
        };
        if (!bounded(limits.max_message_bytes)
            || !bounded(limits.max_string_bytes)
            || !bounded(limits.max_total_string_bytes)
            || !bounded(limits.max_value_bits)
            || !bounded(limits.max_total_words)
            || !bounded(limits.max_attributes)
            || !bounded(limits.max_relations)
            || !bounded(limits.max_correlated_objects)) {
            diagnostics.error(
                "FSIM-SCV-T003", "transaction-record limits are inconsistent");
            return false;
        }
        return true;
    }

    bool canonical_name(const std::string_view value)
    {
        const auto invalid = std::ranges::find_if(value, [](const char byte) {
            const auto code = static_cast<unsigned char>(byte);
            return code < 0x20U || code == 0x7fU || byte == '\\';
        });
        return !value.empty() && invalid == value.end() && value.front() != ' '
            && value.back() != ' ';
    }

    bool valid_region(const TransactionRegion value)
    {
        return value >= TransactionRegion::initialize
            && value <= TransactionRegion::postponed;
    }

    bool valid_value_kind(const TransactionValueKind value)
    {
        return value >= TransactionValueKind::boolean
            && value <= TransactionValueKind::logic_vector;
    }

    bool validate_value(
        const TransactionTypedValue& value,
        const TransactionRecordLimits& limits,
        std::size_t& total_strings,
        std::size_t& total_words,
        diagnostic::Engine& diagnostics)
    {
        if (value.bit_width > limits.max_value_bits) {
            diagnostics.error(
                "FSIM-SCV-T003", "transaction typed value exceeds its limits");
            return false;
        }
        const auto words = (value.bit_width + 63U) / 64U;
        const auto string_bytes = static_cast<std::uint64_t>(
                                      value.nominal_type.size())
            + static_cast<std::uint64_t>(value.text.size());
        if (!valid_value_kind(value.kind) || value.nominal_type.empty()
            || value.nominal_type.size() > limits.max_string_bytes
            || value.text.size() > limits.max_string_bytes
            || string_bytes > limits.max_total_string_bytes - total_strings
            || value.aval_words.size() > limits.max_total_words - total_words
            || value.bval_words.size()
                > limits.max_total_words - total_words - value.aval_words.size()) {
            diagnostics.error(
                "FSIM-SCV-T003", "transaction typed value exceeds its limits");
            return false;
        }
        const auto textual = value.kind == TransactionValueKind::string;
        if ((textual
                && (value.bit_width != 0U || !value.aval_words.empty()
                    || !value.bval_words.empty()))
            || (!textual
                && (value.bit_width == 0U || value.aval_words.size() != words
                    || value.bval_words.size() != words))
            || (value.kind == TransactionValueKind::boolean
                && value.bit_width != 1U)
            || (value.kind == TransactionValueKind::signed_integer
                && !value.signed_type)
            || (value.kind != TransactionValueKind::signed_integer
                && value.signed_type)
            || (value.kind == TransactionValueKind::bit_vector
                && std::ranges::any_of(
                    value.bval_words, [](const auto word) { return word != 0U; }))) {
            diagnostics.error(
                "FSIM-SCV-T002", "transaction typed value shape is inconsistent");
            return false;
        }
        if (!textual && value.bit_width % 64U != 0U) {
            const auto mask = (UINT64_C(1) << (value.bit_width % 64U)) - 1U;
            if ((value.aval_words.back() & ~mask) != 0U
                || (value.bval_words.back() & ~mask) != 0U) {
                diagnostics.error("FSIM-SCV-T002",
                    "transaction typed value has noncanonical high bits");
                return false;
            }
        }
        total_strings += static_cast<std::size_t>(string_bytes);
        total_words += value.aval_words.size() + value.bval_words.size();
        return true;
    }

    void write_value(Writer& writer, const TransactionTypedValue& value)
    {
        writer.u8(static_cast<std::uint8_t>(value.kind));
        writer.u8(value.signed_type ? 1U : 0U);
        writer.u16(0U);
        writer.u64(static_cast<std::uint64_t>(value.bit_width));
        writer.string(value.nominal_type);
        writer.string(value.text);
        writer.u32(static_cast<std::uint32_t>(value.aval_words.size()));
        for (const auto word : value.aval_words)
            writer.u64(word);
        writer.u32(static_cast<std::uint32_t>(value.bval_words.size()));
        for (const auto word : value.bval_words)
            writer.u64(word);
    }

    std::optional<TransactionTypedValue> read_value(
        Reader& reader,
        const TransactionRecordLimits& limits)
    {
        const auto kind = reader.u8();
        const auto signed_type = reader.u8();
        const auto reserved = reader.u16();
        const auto width = reader.u64();
        const auto nominal = reader.string(limits.max_string_bytes);
        const auto text = reader.string(limits.max_string_bytes);
        const auto aval_count = reader.u32();
        if (!kind || !signed_type || !reserved || !width || !nominal || !text
            || *reserved != 0U || *signed_type > 1U
            || *width > limits.max_value_bits || !aval_count
            || *aval_count > limits.max_total_words) {
            return std::nullopt;
        }
        TransactionTypedValue result;
        result.kind = static_cast<TransactionValueKind>(*kind);
        result.signed_type = *signed_type != 0U;
        result.bit_width = static_cast<std::size_t>(*width);
        result.nominal_type = std::move(*nominal);
        result.text = std::move(*text);
        result.aval_words.reserve(*aval_count);
        for (std::uint32_t index = 0; index < *aval_count; ++index) {
            const auto word = reader.u64();
            if (!word)
                return std::nullopt;
            result.aval_words.push_back(*word);
        }
        const auto bval_count = reader.u32();
        if (!bval_count || *bval_count > limits.max_total_words) {
            return std::nullopt;
        }
        result.bval_words.reserve(*bval_count);
        for (std::uint32_t index = 0; index < *bval_count; ++index) {
            const auto word = reader.u64();
            if (!word)
                return std::nullopt;
            result.bval_words.push_back(*word);
        }
        return result;
    }

} // namespace

bool validate_transaction_record(
    const TransactionRecord& record,
    const TransactionRecordLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics))
        return false;
    if (record.schema != transaction_record_schema_version
        || !record.stream.valid() || !record.generator.valid()
        || !record.transaction.valid() || !valid_region(record.begin_region)
        || !valid_region(record.end_region)) {
        diagnostics.error(
            "FSIM-SCV-T001", "transaction record has invalid schema or identity");
        return false;
    }
    if (std::tie(record.end_time_fs, record.end_delta, record.end_region)
        < std::tie(record.begin_time_fs, record.begin_delta, record.begin_region)) {
        diagnostics.error(
            "FSIM-SCV-T002", "transaction record ends before it begins");
        return false;
    }
    if (record.attributes.size() > limits.max_attributes
        || record.relations.size() > limits.max_relations
        || record.correlated_objects.size() > limits.max_correlated_objects) {
        diagnostics.error(
            "FSIM-SCV-T003", "transaction record collection limit exceeded");
        return false;
    }
    std::size_t total_strings { };
    std::size_t total_words { };
    std::string_view prior_attribute;
    for (const auto& attribute : record.attributes) {
        if (!canonical_name(attribute.name)
            || attribute.name.size() > limits.max_string_bytes
            || (!prior_attribute.empty() && prior_attribute >= attribute.name)
            || attribute.name.size()
                > limits.max_total_string_bytes - total_strings) {
            diagnostics.error("FSIM-SCV-T002",
                "transaction attributes are not canonical and strictly ordered");
            return false;
        }
        total_strings += attribute.name.size();
        if (!validate_value(attribute.value, limits, total_strings, total_words,
                diagnostics)) {
            return false;
        }
        prior_attribute = attribute.name;
    }
    std::tuple<std::string_view, TransactionStableId> prior_relation;
    bool have_relation { };
    for (const auto& relation : record.relations) {
        const auto key = std::tuple { std::string_view { relation.name }, relation.target };
        if (!canonical_name(relation.name) || !relation.target.valid()
            || relation.name.size() > limits.max_string_bytes
            || relation.name.size()
                > limits.max_total_string_bytes - total_strings
            || (have_relation && prior_relation >= key)) {
            diagnostics.error("FSIM-SCV-T002",
                "transaction relations are not canonical and strictly ordered");
            return false;
        }
        total_strings += relation.name.size();
        prior_relation = key;
        have_relation = true;
    }
    if (!std::ranges::is_sorted(record.correlated_objects)
        || std::ranges::adjacent_find(record.correlated_objects)
            != record.correlated_objects.end()
        || std::ranges::any_of(record.correlated_objects, [](const auto& value) {
               return value.domain < TransactionObjectDomain::systemc
                   || value.domain > TransactionObjectDomain::tlm2
                   || !value.object.valid();
           })) {
        diagnostics.error("FSIM-SCV-T002",
            "transaction correlated objects are invalid or noncanonical");
        return false;
    }
    return true;
}

bool transaction_record_precedes(
    const TransactionRecord& left,
    const TransactionRecord& right) noexcept
{
    return std::tie(left.begin_time_fs, left.begin_delta, left.begin_region,
               left.stream, left.generator, left.transaction)
        < std::tie(right.begin_time_fs, right.begin_delta, right.begin_region,
            right.stream, right.generator, right.transaction);
}

std::optional<std::vector<std::byte>> serialize_transaction_record(
    const TransactionRecord& record,
    const TransactionRecordLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!validate_transaction_record(record, limits, diagnostics)) {
        return std::nullopt;
    }
    Writer writer;
    writer.bytes.insert(writer.bytes.end(), magic.begin(), magic.end());
    writer.u32(record.schema);
    writer.u32(0U);
    writer.id(record.stream);
    writer.id(record.generator);
    writer.id(record.transaction);
    writer.u64(record.begin_time_fs);
    writer.u64(record.begin_delta);
    writer.u8(static_cast<std::uint8_t>(record.begin_region));
    writer.u8(static_cast<std::uint8_t>(record.end_region));
    writer.u16(0U);
    writer.u32(0U);
    writer.u64(record.end_time_fs);
    writer.u64(record.end_delta);
    writer.u32(static_cast<std::uint32_t>(record.attributes.size()));
    writer.u32(static_cast<std::uint32_t>(record.relations.size()));
    writer.u32(static_cast<std::uint32_t>(record.correlated_objects.size()));
    writer.u32(0U);
    for (const auto& attribute : record.attributes) {
        writer.string(attribute.name);
        write_value(writer, attribute.value);
    }
    for (const auto& relation : record.relations) {
        writer.string(relation.name);
        writer.id(relation.target);
    }
    for (const auto object : record.correlated_objects) {
        writer.u8(static_cast<std::uint8_t>(object.domain));
        writer.u8(0U);
        writer.u16(0U);
        writer.u32(0U);
        writer.id(object.object);
    }
    if (writer.bytes.size() > limits.max_message_bytes) {
        diagnostics.error(
            "FSIM-SCV-T003", "serialized transaction record exceeds its limit");
        return std::nullopt;
    }
    return std::move(writer.bytes);
}

std::optional<TransactionRecord> deserialize_transaction_record(
    const std::span<const std::byte> bytes,
    const TransactionRecordLimits& limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(limits, diagnostics) || bytes.size() > limits.max_message_bytes
        || bytes.size() < magic.size()
        || !std::ranges::equal(magic, bytes.first(magic.size()))) {
        diagnostics.error(
            "FSIM-SCV-T001", "transaction record has bad magic or size");
        return std::nullopt;
    }
    Reader reader { bytes };
    reader.offset = magic.size();
    const auto schema = reader.u32();
    const auto reserved0 = reader.u32();
    const auto stream = reader.id();
    const auto generator = reader.id();
    const auto transaction = reader.id();
    const auto begin_time = reader.u64();
    const auto begin_delta = reader.u64();
    const auto begin_region = reader.u8();
    const auto end_region = reader.u8();
    const auto reserved1 = reader.u16();
    const auto reserved2 = reader.u32();
    const auto end_time = reader.u64();
    const auto end_delta = reader.u64();
    const auto attribute_count = reader.u32();
    const auto relation_count = reader.u32();
    const auto object_count = reader.u32();
    const auto reserved3 = reader.u32();
    if (!schema || !reserved0 || !stream || !generator || !transaction
        || !begin_time || !begin_delta || !begin_region || !end_region
        || !reserved1 || !reserved2 || !end_time || !end_delta
        || !attribute_count || !relation_count || !object_count || !reserved3
        || *reserved0 != 0U || *reserved1 != 0U || *reserved2 != 0U
        || *reserved3 != 0U || *attribute_count > limits.max_attributes
        || *relation_count > limits.max_relations
        || *object_count > limits.max_correlated_objects) {
        diagnostics.error(
            "FSIM-SCV-T002", "transaction record header is truncated or malformed");
        return std::nullopt;
    }
    TransactionRecord result;
    result.schema = *schema;
    result.stream = *stream;
    result.generator = *generator;
    result.transaction = *transaction;
    result.begin_time_fs = *begin_time;
    result.begin_delta = *begin_delta;
    result.begin_region = static_cast<TransactionRegion>(*begin_region);
    result.end_time_fs = *end_time;
    result.end_delta = *end_delta;
    result.end_region = static_cast<TransactionRegion>(*end_region);
    result.attributes.reserve(*attribute_count);
    for (std::uint32_t index = 0; index < *attribute_count; ++index) {
        auto name = reader.string(limits.max_string_bytes);
        auto value = read_value(reader, limits);
        if (!name || !value) {
            diagnostics.error(
                "FSIM-SCV-T002", "transaction attribute is truncated");
            return std::nullopt;
        }
        result.attributes.push_back({ std::move(*name), std::move(*value) });
    }
    result.relations.reserve(*relation_count);
    for (std::uint32_t index = 0; index < *relation_count; ++index) {
        auto name = reader.string(limits.max_string_bytes);
        const auto target = reader.id();
        if (!name || !target) {
            diagnostics.error(
                "FSIM-SCV-T002", "transaction relation is truncated");
            return std::nullopt;
        }
        result.relations.push_back({ std::move(*name), *target });
    }
    result.correlated_objects.reserve(*object_count);
    for (std::uint32_t index = 0; index < *object_count; ++index) {
        const auto domain = reader.u8();
        const auto reserved4 = reader.u8();
        const auto reserved5 = reader.u16();
        const auto reserved6 = reader.u32();
        const auto object = reader.id();
        if (!domain || !reserved4 || !reserved5 || !reserved6 || !object
            || *reserved4 != 0U || *reserved5 != 0U || *reserved6 != 0U) {
            diagnostics.error(
                "FSIM-SCV-T002", "transaction correlated object is truncated");
            return std::nullopt;
        }
        result.correlated_objects.push_back(
            { static_cast<TransactionObjectDomain>(*domain), *object });
    }
    if (reader.offset != bytes.size()
        || !validate_transaction_record(result, limits, diagnostics)) {
        if (reader.offset != bytes.size()) {
            diagnostics.error(
                "FSIM-SCV-T002", "transaction record has trailing bytes");
        }
        return std::nullopt;
    }
    return result;
}

} // namespace fsim::runtime
