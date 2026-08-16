// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_recording.hpp"

#include <scv.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace fsim::systemc {
namespace {

    template <typename Domain>
    runtime::TransactionStableId stable(const ScvBackendId<Domain> value)
    {
        return { value.high, value.low };
    }

    bool canonical_name(const std::string_view value, const std::size_t limit)
    {
        const auto invalid = std::ranges::find_if(value, [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte == 0x7fU || value == '\\';
        });
        return !value.empty() && value.size() <= limit && invalid == value.end()
            && value.front() != ' ' && value.back() != ' ';
    }

    bool valid_region(const runtime::TransactionRegion value)
    {
        return value >= runtime::TransactionRegion::initialize
            && value <= runtime::TransactionRegion::postponed;
    }

    bool valid_limits(const ScvNativeRecordingLimits& limits)
    {
        const auto bounded = [](const std::size_t value) {
            return value != 0U
                && value <= std::numeric_limits<std::uint32_t>::max();
        };
        return bounded(limits.max_streams) && bounded(limits.max_generators)
            && bounded(limits.max_handles)
            && bounded(limits.max_completed_records)
            && bounded(limits.max_attributes_per_transaction)
            && bounded(limits.max_relations_per_transaction)
            && bounded(limits.max_name_bytes);
    }

    std::optional<std::uint64_t> femtoseconds(const sc_core::sc_time& value)
    {
        const long double result
            = static_cast<long double>(value.to_seconds()) * 1.0e15L;
        if (!std::isfinite(result) || result < 0.0L
            || result
                > static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(std::floor(result + 0.5L));
    }

    runtime::TransactionTypedValue native_integer_value(
        const scv_extensions_if& extension)
    {
        runtime::TransactionTypedValue result;
        result.kind = runtime::TransactionValueKind::signed_integer;
        result.nominal_type = extension.get_type_name();
        result.bit_width = static_cast<std::size_t>(extension.get_bitwidth());
        result.signed_type = true;
        result.aval_words.push_back(
            std::bit_cast<std::uint64_t>(extension.get_integer()));
        result.bval_words.push_back(0U);
        return result;
    }

    bool same_native_value(
        const runtime::TransactionTypedValue& value,
        const scv_extensions_if& extension)
    {
        if (value.kind == runtime::TransactionValueKind::boolean) {
            return extension.get_type() == scv_extensions_if::BOOLEAN
                && value.bit_width == 1U && value.aval_words.size() == 1U
                && value.bval_words.size() == 1U
                && value.bval_words.front() == 0U
                && extension.get_bool() == (value.aval_words.front() != 0U);
        }
        if (value.kind == runtime::TransactionValueKind::signed_integer) {
            return extension.get_type() == scv_extensions_if::INTEGER
                && value.bit_width == 64U && value.signed_type
                && value.aval_words.size() == 1U
                && value.bval_words.size() == 1U
                && value.bval_words.front() == 0U
                && extension.get_integer()
                == std::bit_cast<std::int64_t>(value.aval_words.front());
        }
        if (value.kind == runtime::TransactionValueKind::unsigned_integer) {
            return extension.get_type() == scv_extensions_if::UNSIGNED
                && value.bit_width == 64U && !value.signed_type
                && value.aval_words.size() == 1U
                && value.bval_words.size() == 1U
                && value.bval_words.front() == 0U
                && extension.get_unsigned() == value.aval_words.front();
        }
        return value.kind == runtime::TransactionValueKind::string
            && extension.get_type() == scv_extensions_if::STRING
            && value.bit_width == 0U && !value.signed_type
            && value.aval_words.empty() && value.bval_words.empty()
            && extension.get_string() == value.text;
    }

} // namespace

struct ScvNativeRecordingRegistry::Impl {
    struct StreamEntry {
        ScvStreamId id;
        std::unique_ptr<scv_tr_stream> native;
    };

    struct GeneratorEntry {
        ScvGeneratorId id;
        ScvStreamId stream;
        std::string begin_attribute_name;
        std::string end_attribute_name;
        std::unique_ptr<scv_tr_generator<long long, long long>> native;
    };

    struct HandleEntry {
        ScvTransactionId id;
        ScvGeneratorId generator;
        scv_tr_handle native;
        bool ended { };
    };

    struct BeginContext {
        ScvTransactionId transaction;
        ScvGeneratorId generator;
        ScvNativeTransactionCoordinate coordinate;
    };

    struct AttributeContext {
        ScvTransactionId transaction;
        std::string name;
        runtime::TransactionTypedValue value;
    };

    struct RelationContext {
        ScvTransactionId transaction;
        std::string name;
        ScvTransactionId target;
    };

    struct EndContext {
        ScvTransactionId transaction;
        ScvNativeTransactionCoordinate coordinate;
    };

    Impl(ScvIslandId island_value, std::string database_name_value,
        ScvNativeRecordingLimits limits_value)
        : island(island_value)
        , database_name(std::move(database_name_value))
        , limits(std::move(limits_value))
    {
        if (!island.valid()
            || !canonical_name(database_name, limits.max_name_bytes)
            || !valid_limits(limits)) {
            initialization_error = true;
            return;
        }
        database = std::make_unique<scv_tr_db>(database_name.c_str());
        handle_callback = scv_tr_handle::register_class_cb(
            &Impl::transaction_callback, this);
        attribute_callback = scv_tr_handle::register_record_attribute_cb(
            &Impl::record_attribute_callback, this);
        relation_callback = scv_tr_handle::register_relation_cb(
            &Impl::transaction_relation_callback, this);
    }

    ~Impl()
    {
        if (handle_callback != 0)
            scv_tr_handle::remove_callback(handle_callback);
        if (attribute_callback != 0)
            scv_tr_handle::remove_callback(attribute_callback);
        if (relation_callback != 0)
            scv_tr_handle::remove_callback(relation_callback);
        handles.clear();
        native_to_stable.clear();
        pending.clear();
        generators.clear();
        streams.clear();
        database.reset();
    }

    static void transaction_callback(const scv_tr_handle& handle,
        const scv_tr_handle::callback_reason reason, void* user_data)
    {
        static_cast<Impl*>(user_data)->on_transaction(handle, reason);
    }

    static void record_attribute_callback(const scv_tr_handle& handle,
        const char* name, const scv_extensions_if* extension, void* user_data)
    {
        static_cast<Impl*>(user_data)->on_attribute(handle, name, extension);
    }

    static void transaction_relation_callback(const scv_tr_handle& transaction,
        const scv_tr_handle& target, void* user_data,
        const scv_tr_relation_handle_t relation)
    {
        static_cast<Impl*>(user_data)->on_relation(
            transaction, target, relation);
    }

    bool owns(const scv_tr_handle& handle) const
    {
        return database
            && handle.get_scv_tr_stream().get_scv_tr_db() == database.get();
    }

    void fail(std::string message)
    {
        if (callback_error.empty())
            callback_error = std::move(message);
    }

    const GeneratorEntry* find_native_generator(
        const std::uint64_t native_id) const
    {
        const auto iterator = std::ranges::find_if(
            generators, [native_id](const auto& item) {
                return item.second.native->get_id() == native_id;
            });
        return iterator == generators.end() ? nullptr : &iterator->second;
    }

    void on_transaction(
        const scv_tr_handle& handle, const scv_tr_handle::callback_reason reason)
    {
        if (!owns(handle))
            return;
        ++callback_count;
        const auto native_id = static_cast<std::uint64_t>(handle.get_id());
        if (reason == scv_tr_handle::BEGIN) {
            if (!database->get_recording())
                return;
            if (!begin_context || pending.contains(native_id)) {
                fail("native SCV begin callback has no unique adapter context");
                return;
            }
            const auto* generator
                = find_native_generator(handle.get_scv_tr_generator_base().get_id());
            const auto time = femtoseconds(handle.get_begin_sc_time());
            if (!generator || !time || generator->id != begin_context->generator) {
                fail("native SCV begin callback identity or time is invalid");
                return;
            }
            runtime::TransactionRecord record;
            record.stream = stable(generator->stream);
            record.generator = stable(generator->id);
            record.transaction = stable(begin_context->transaction);
            record.begin_time_fs = *time;
            record.begin_delta = begin_context->coordinate.delta;
            record.begin_region = begin_context->coordinate.region;
            record.end_time_fs = *time;
            record.end_delta = begin_context->coordinate.delta;
            record.end_region = begin_context->coordinate.region;
            const auto* extension = handle.get_begin_exts_p();
            if (!extension || extension->get_type() != scv_extensions_if::INTEGER) {
                fail("native SCV begin callback lost its integer extension");
                return;
            }
            record.attributes.push_back({ generator->begin_attribute_name,
                native_integer_value(*extension) });
            pending.emplace(native_id, std::move(record));
            native_to_stable.emplace(native_id, begin_context->transaction);
            return;
        }
        if (reason == scv_tr_handle::END) {
            if (!database->get_recording()) {
                pending.erase(native_id);
                return;
            }
            const auto iterator = pending.find(native_id);
            if (iterator == pending.end())
                return;
            if (!end_context
                || native_to_stable[native_id] != end_context->transaction) {
                fail("native SCV end callback has no matching adapter context");
                return;
            }
            const auto* generator
                = find_native_generator(handle.get_scv_tr_generator_base().get_id());
            const auto* extension = handle.get_end_exts_p();
            const auto time = femtoseconds(handle.get_end_sc_time());
            if (!generator || !extension || !time
                || extension->get_type() != scv_extensions_if::INTEGER) {
                fail("native SCV end callback lost its generator, value, or time");
                return;
            }
            auto& record = iterator->second;
            record.end_time_fs = *time;
            record.end_delta = end_context->coordinate.delta;
            record.end_region = end_context->coordinate.region;
            record.attributes.push_back({ generator->end_attribute_name,
                native_integer_value(*extension) });
            std::ranges::sort(record.attributes, { }, &runtime::TransactionAttribute::name);
            std::ranges::sort(record.relations, [](const auto& left, const auto& right) {
                return std::tie(left.name, left.target)
                    < std::tie(right.name, right.target);
            });
            candidate = std::move(record);
            pending.erase(iterator);
        }
    }

    void on_attribute(const scv_tr_handle& handle, const char* name,
        const scv_extensions_if* extension)
    {
        if (!owns(handle))
            return;
        ++callback_count;
        if (!database->get_recording())
            return;
        const auto native_id = static_cast<std::uint64_t>(handle.get_id());
        const auto pending_iterator = pending.find(native_id);
        if (pending_iterator == pending.end())
            return;
        if (!attribute_context || !name || !extension
            || attribute_context->name != name
            || native_to_stable[native_id] != attribute_context->transaction
            || !same_native_value(attribute_context->value, *extension)) {
            fail("native SCV attribute callback does not match its adapter value");
            return;
        }
        auto& attributes = pending_iterator->second.attributes;
        if (attributes.size() >= limits.max_attributes_per_transaction
            || std::ranges::any_of(attributes, [name](const auto& attribute) {
                   return attribute.name == name;
               })) {
            fail("native SCV attribute callback exceeds its unique attribute limit");
            return;
        }
        attributes.push_back(
            { attribute_context->name, attribute_context->value });
    }

    void on_relation(const scv_tr_handle& transaction,
        const scv_tr_handle& target, const scv_tr_relation_handle_t relation)
    {
        if (!owns(transaction))
            return;
        ++callback_count;
        if (!database->get_recording())
            return;
        const auto native_id = static_cast<std::uint64_t>(transaction.get_id());
        const auto target_id = static_cast<std::uint64_t>(target.get_id());
        const auto pending_iterator = pending.find(native_id);
        const auto target_iterator = native_to_stable.find(target_id);
        const char* native_name = database->get_relation_name(relation);
        if (pending_iterator == pending.end())
            return;
        if (!relation_context || target_iterator == native_to_stable.end()
            || !native_name || relation_context->name != native_name
            || native_to_stable[native_id] != relation_context->transaction
            || target_iterator->second != relation_context->target) {
            fail("native SCV relation callback does not match its adapter relation");
            return;
        }
        auto& relations = pending_iterator->second.relations;
        if (relations.size() >= limits.max_relations_per_transaction
            || std::ranges::any_of(relations, [&](const auto& existing) {
                   return existing.name == relation_context->name
                       && existing.target == stable(relation_context->target);
               })) {
            fail("native SCV relation callback exceeds its unique relation limit");
            return;
        }
        relations.push_back(
            { relation_context->name, stable(relation_context->target) });
    }

    bool report_callback_error(diagnostic::Engine& diagnostics)
    {
        if (callback_error.empty())
            return true;
        diagnostics.error("FSIM-SCV-N002", std::move(callback_error));
        callback_error.clear();
        return false;
    }

    ScvIslandId island;
    std::string database_name;
    ScvNativeRecordingLimits limits;
    bool initialization_error { };
    std::unique_ptr<scv_tr_db> database;
    std::map<ScvStreamId, StreamEntry> streams;
    std::map<ScvGeneratorId, GeneratorEntry> generators;
    std::map<ScvTransactionId, HandleEntry> handles;
    std::map<std::uint64_t, ScvTransactionId> native_to_stable;
    std::map<std::uint64_t, runtime::TransactionRecord> pending;
    std::vector<runtime::TransactionRecord> completed;
    std::optional<BeginContext> begin_context;
    std::optional<AttributeContext> attribute_context;
    std::optional<RelationContext> relation_context;
    std::optional<EndContext> end_context;
    std::optional<runtime::TransactionRecord> candidate;
    std::string callback_error;
    std::size_t callback_count { };
    scv_tr_handle::callback_h handle_callback { };
    scv_tr_handle::callback_h attribute_callback { };
    scv_tr_handle::callback_h relation_callback { };
};

ScvNativeRecordingRegistry::ScvNativeRecordingRegistry(
    const ScvIslandId island, std::string database_name,
    ScvNativeRecordingLimits limits)
    : impl_(std::make_unique<Impl>(
          island, std::move(database_name), std::move(limits)))
{
}

ScvNativeRecordingRegistry::~ScvNativeRecordingRegistry() = default;
ScvNativeRecordingRegistry::ScvNativeRecordingRegistry(
    ScvNativeRecordingRegistry&&) noexcept
    = default;
ScvNativeRecordingRegistry& ScvNativeRecordingRegistry::operator=(
    ScvNativeRecordingRegistry&&) noexcept
    = default;

bool ScvNativeRecordingRegistry::valid(diagnostic::Engine& diagnostics) const
{
    if (!impl_ || impl_->initialization_error || !impl_->database) {
        diagnostics.error("FSIM-SCV-N003",
            "native SCV recording identity or limits are invalid");
        return false;
    }
    return true;
}

bool ScvNativeRecordingRegistry::create_stream(const ScvStreamId stream,
    std::string name, std::string kind, diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    if (!stream.valid() || !canonical_name(name, impl_->limits.max_name_bytes)
        || !canonical_name(kind, impl_->limits.max_name_bytes)
        || impl_->streams.contains(stream)) {
        diagnostics.error(
            "FSIM-SCV-N001", "native SCV stream identity or name is invalid");
        return false;
    }
    if (impl_->streams.size() >= impl_->limits.max_streams) {
        diagnostics.error(
            "FSIM-SCV-N003", "native SCV stream limit is exhausted");
        return false;
    }
    auto native
        = std::make_unique<scv_tr_stream>(name.c_str(), kind.c_str(), impl_->database.get());
    impl_->streams.emplace(stream,
        Impl::StreamEntry { stream, std::move(native) });
    return true;
}

bool ScvNativeRecordingRegistry::create_generator(
    const ScvGeneratorId generator, const ScvStreamId stream, std::string name,
    std::string begin_attribute_name, std::string end_attribute_name,
    diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    const auto stream_iterator = impl_->streams.find(stream);
    if (!generator.valid() || stream_iterator == impl_->streams.end()
        || impl_->generators.contains(generator)
        || !canonical_name(name, impl_->limits.max_name_bytes)
        || !canonical_name(
            begin_attribute_name, impl_->limits.max_name_bytes)
        || !canonical_name(end_attribute_name, impl_->limits.max_name_bytes)
        || begin_attribute_name == end_attribute_name) {
        diagnostics.error("FSIM-SCV-N001",
            "native SCV generator identity, stream, or name is invalid");
        return false;
    }
    if (impl_->generators.size() >= impl_->limits.max_generators) {
        diagnostics.error(
            "FSIM-SCV-N003", "native SCV generator limit is exhausted");
        return false;
    }
    auto native = std::make_unique<scv_tr_generator<long long, long long>>(
        name.c_str(), *stream_iterator->second.native,
        begin_attribute_name.c_str(), end_attribute_name.c_str());
    impl_->generators.emplace(generator,
        Impl::GeneratorEntry { generator, stream,
            std::move(begin_attribute_name), std::move(end_attribute_name),
            std::move(native) });
    return true;
}

bool ScvNativeRecordingRegistry::set_recording(
    const bool enabled, diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    impl_->database->set_recording(enabled);
    return true;
}

bool ScvNativeRecordingRegistry::begin_transaction(
    const ScvTransactionId transaction, const ScvGeneratorId generator,
    const std::int64_t begin_value,
    const ScvNativeTransactionCoordinate coordinate,
    diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    const auto generator_iterator = impl_->generators.find(generator);
    if (!transaction.valid() || generator_iterator == impl_->generators.end()
        || impl_->handles.contains(transaction) || !valid_region(coordinate.region)) {
        diagnostics.error("FSIM-SCV-N001",
            "native SCV transaction identity, generator, or coordinate is invalid");
        return false;
    }
    if (impl_->handles.size() >= impl_->limits.max_handles) {
        diagnostics.error(
            "FSIM-SCV-N003", "native SCV transaction-handle limit is exhausted");
        return false;
    }
    impl_->callback_error.clear();
    impl_->begin_context = Impl::BeginContext { transaction, generator, coordinate };
    auto native = generator_iterator->second.native->begin_transaction(
        static_cast<long long>(begin_value));
    impl_->begin_context.reset();
    if (!native.is_valid() || !impl_->report_callback_error(diagnostics)) {
        diagnostics.error(
            "FSIM-SCV-N002", "native SCV transaction begin failed");
        return false;
    }
    impl_->native_to_stable.insert_or_assign(
        static_cast<std::uint64_t>(native.get_id()), transaction);
    impl_->handles.emplace(transaction,
        Impl::HandleEntry { transaction, generator, std::move(native), false });
    return true;
}

bool ScvNativeRecordingRegistry::record_attribute(
    const ScvTransactionId transaction, std::string name,
    const runtime::TransactionTypedValue& value,
    diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    const auto iterator = impl_->handles.find(transaction);
    if (iterator == impl_->handles.end() || iterator->second.ended
        || !canonical_name(name, impl_->limits.max_name_bytes)) {
        diagnostics.error("FSIM-SCV-N001",
            "native SCV attribute handle, state, or name is invalid");
        return false;
    }
    impl_->callback_error.clear();
    impl_->attribute_context
        = Impl::AttributeContext { transaction, name, value };
    switch (value.kind) {
    case runtime::TransactionValueKind::boolean:
        if (value.bit_width == 1U && !value.signed_type
            && value.aval_words.size() == 1U && value.bval_words.size() == 1U
            && value.bval_words.front() == 0U
            && value.aval_words.front() <= 1U) {
            iterator->second.native.record_attribute(
                name.c_str(), value.aval_words.front() != 0U);
            break;
        }
        impl_->fail("native SCV Boolean attribute has an invalid shape");
        break;
    case runtime::TransactionValueKind::signed_integer:
        if (value.bit_width == 64U && value.signed_type
            && value.aval_words.size() == 1U && value.bval_words.size() == 1U
            && value.bval_words.front() == 0U) {
            iterator->second.native.record_attribute(name.c_str(),
                static_cast<long long>(
                    std::bit_cast<std::int64_t>(value.aval_words.front())));
            break;
        }
        impl_->fail("native SCV signed attribute has an invalid shape");
        break;
    case runtime::TransactionValueKind::unsigned_integer:
        if (value.bit_width == 64U && !value.signed_type
            && value.aval_words.size() == 1U && value.bval_words.size() == 1U
            && value.bval_words.front() == 0U) {
            iterator->second.native.record_attribute(
                name.c_str(), static_cast<unsigned long long>(value.aval_words.front()));
            break;
        }
        impl_->fail("native SCV unsigned attribute has an invalid shape");
        break;
    case runtime::TransactionValueKind::string:
        if (value.bit_width == 0U && !value.signed_type
            && value.aval_words.empty() && value.bval_words.empty()) {
            iterator->second.native.record_attribute(name.c_str(), value.text);
            break;
        }
        impl_->fail("native SCV string attribute has an invalid shape");
        break;
    default:
        impl_->fail("native SCV attribute kind is not supported by this adapter");
        break;
    }
    impl_->attribute_context.reset();
    return impl_->report_callback_error(diagnostics);
}

bool ScvNativeRecordingRegistry::relate_transaction(
    const ScvTransactionId transaction, std::string relation,
    const ScvTransactionId target, diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    const auto iterator = impl_->handles.find(transaction);
    const auto target_iterator = impl_->handles.find(target);
    if (iterator == impl_->handles.end() || target_iterator == impl_->handles.end()
        || iterator->second.ended
        || !canonical_name(relation, impl_->limits.max_name_bytes)) {
        diagnostics.error("FSIM-SCV-N001",
            "native SCV relation handle, state, target, or name is invalid");
        return false;
    }
    impl_->callback_error.clear();
    impl_->relation_context
        = Impl::RelationContext { transaction, relation, target };
    const bool related = iterator->second.native.add_relation(
        relation.c_str(), target_iterator->second.native);
    impl_->relation_context.reset();
    if (!related) {
        diagnostics.error("FSIM-SCV-N002", "native SCV relation was rejected");
        return false;
    }
    return impl_->report_callback_error(diagnostics);
}

bool ScvNativeRecordingRegistry::end_transaction(
    const ScvTransactionId transaction, const std::int64_t end_value,
    const ScvNativeTransactionCoordinate coordinate,
    diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    const auto iterator = impl_->handles.find(transaction);
    if (iterator == impl_->handles.end() || iterator->second.ended
        || !valid_region(coordinate.region)) {
        diagnostics.error("FSIM-SCV-N001",
            "native SCV transaction end handle, state, or coordinate is invalid");
        return false;
    }
    const auto generator = impl_->generators.find(iterator->second.generator);
    if (generator == impl_->generators.end()) {
        diagnostics.error(
            "FSIM-SCV-N001", "native SCV transaction generator is stale");
        return false;
    }
    impl_->callback_error.clear();
    impl_->candidate.reset();
    impl_->end_context = Impl::EndContext { transaction, coordinate };
    generator->second.native->end_transaction(
        iterator->second.native, static_cast<long long>(end_value));
    impl_->end_context.reset();
    iterator->second.ended = true;
    if (!impl_->report_callback_error(diagnostics))
        return false;
    if (!impl_->candidate)
        return true;
    if (impl_->completed.size() >= impl_->limits.max_completed_records) {
        impl_->candidate.reset();
        diagnostics.error(
            "FSIM-SCV-N003", "native SCV completed-record limit is exhausted");
        return false;
    }
    if (!runtime::validate_transaction_record(
            *impl_->candidate, impl_->limits.record_limits, diagnostics)) {
        impl_->candidate.reset();
        diagnostics.error(
            "FSIM-SCV-N002", "native SCV callback produced an invalid record");
        return false;
    }
    impl_->completed.push_back(std::move(*impl_->candidate));
    impl_->candidate.reset();
    return true;
}

bool ScvNativeRecordingRegistry::release_transaction(
    const ScvTransactionId transaction, diagnostic::Engine& diagnostics)
{
    if (!valid(diagnostics))
        return false;
    const auto iterator = impl_->handles.find(transaction);
    if (iterator == impl_->handles.end() || !iterator->second.ended) {
        diagnostics.error("FSIM-SCV-N001",
            "native SCV transaction release requires an ended live handle");
        return false;
    }
    const auto native_id
        = static_cast<std::uint64_t>(iterator->second.native.get_id());
    impl_->pending.erase(native_id);
    impl_->native_to_stable.erase(native_id);
    impl_->handles.erase(iterator);
    return true;
}

std::size_t ScvNativeRecordingRegistry::live_streams() const noexcept
{
    return impl_ ? impl_->streams.size() : 0U;
}

std::size_t ScvNativeRecordingRegistry::live_generators() const noexcept
{
    return impl_ ? impl_->generators.size() : 0U;
}

std::size_t ScvNativeRecordingRegistry::live_handles() const noexcept
{
    return impl_ ? impl_->handles.size() : 0U;
}

std::size_t ScvNativeRecordingRegistry::native_callback_count() const noexcept
{
    return impl_ ? impl_->callback_count : 0U;
}

const std::vector<runtime::TransactionRecord>&
ScvNativeRecordingRegistry::records() const
{
    static const std::vector<runtime::TransactionRecord> empty;
    return impl_ ? impl_->completed : empty;
}

std::vector<runtime::TransactionRecord>
ScvNativeRecordingRegistry::take_records()
{
    if (!impl_)
        return { };
    auto result = std::move(impl_->completed);
    impl_->completed.clear();
    return result;
}

} // namespace fsim::systemc
