// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_data_read.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace fsim::runtime {

namespace {

std::atomic<std::uint64_t> next_extension_identity { 1U };

bool valid_access(const SystemVerilogVpiDataReadAccess access) noexcept
{
    switch (access) {
    case SystemVerilogVpiDataReadAccess::LimitedInteractive:
    case SystemVerilogVpiDataReadAccess::Interactive:
    case SystemVerilogVpiDataReadAccess::PostProcess:
        return true;
    }
    return false;
}

SystemVerilogVpiDataReadError object_error(
    const SystemVerilogVpiObjectError error) noexcept
{
    switch (error) {
    case SystemVerilogVpiObjectError::None:
        return SystemVerilogVpiDataReadError::None;
    case SystemVerilogVpiObjectError::InvalidSimulation:
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    case SystemVerilogVpiObjectError::CrossSimulation:
        return SystemVerilogVpiDataReadError::CrossExtension;
    case SystemVerilogVpiObjectError::ReleasedHandle:
    case SystemVerilogVpiObjectError::StaleHandle:
        return SystemVerilogVpiDataReadError::ReleasedHandle;
    default:
        return SystemVerilogVpiDataReadError::InvalidObject;
    }
}

SystemVerilogVpiDataReadError value_error(
    const SystemVerilogVpiValueError error) noexcept
{
    switch (error) {
    case SystemVerilogVpiValueError::None:
        return SystemVerilogVpiDataReadError::None;
    case SystemVerilogVpiValueError::InvalidSimulation:
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    case SystemVerilogVpiValueError::CrossSimulation:
        return SystemVerilogVpiDataReadError::CrossExtension;
    case SystemVerilogVpiValueError::ReleasedHandle:
    case SystemVerilogVpiValueError::StaleHandle:
        return SystemVerilogVpiDataReadError::ReleasedHandle;
    case SystemVerilogVpiValueError::NotBound:
        return SystemVerilogVpiDataReadError::NoValue;
    case SystemVerilogVpiValueError::TypeMismatch:
        return SystemVerilogVpiDataReadError::TypeMismatch;
    case SystemVerilogVpiValueError::ResourceLimit:
        return SystemVerilogVpiDataReadError::ResourceLimit;
    default:
        return SystemVerilogVpiDataReadError::InvalidObject;
    }
}

bool valid_property(const SystemVerilogVpiDataReadProperty property) noexcept
{
    switch (property) {
    case SystemVerilogVpiDataReadProperty::IsLoaded:
    case SystemVerilogVpiDataReadProperty::HasDataValueChange:
    case SystemVerilogVpiDataReadProperty::HasValueChange:
    case SystemVerilogVpiDataReadProperty::HasNoValue:
    case SystemVerilogVpiDataReadProperty::BelongsToExtension:
        return true;
    }
    return false;
}

bool valid_control(const SystemVerilogVpiDataReadControl control) noexcept
{
    switch (control) {
    case SystemVerilogVpiDataReadControl::MinimumTime:
    case SystemVerilogVpiDataReadControl::MaximumTime:
    case SystemVerilogVpiDataReadControl::PreviousValueChange:
    case SystemVerilogVpiDataReadControl::NextValueChange:
    case SystemVerilogVpiDataReadControl::Time:
        return true;
    }
    return false;
}

bool traversable_object(const SystemVerilogVpiObjectKind kind) noexcept
{
    using Kind = SystemVerilogVpiObjectKind;
    switch (kind) {
    case Kind::Port:
    case Kind::Net:
    case Kind::Variable:
    case Kind::Parameter:
    case Kind::Memory:
    case Kind::Array:
    case Kind::ClassProperty:
    case Kind::NamedEvent:
    case Kind::Driver:
    case Kind::Constant:
    case Kind::Primitive:
    case Kind::Gate:
    case Kind::Switch:
    case Kind::Assertion:
    case Kind::PortBit:
    case Kind::NetBit:
    case Kind::VariableBit:
    case Kind::MemoryWord:
    case Kind::ArrayWord:
        return true;
    default:
        return false;
    }
}

} // namespace

SystemVerilogVpiStoredValueLookupResult
SystemVerilogVpiObjectRegistry::stored_value(
    const fsim_vpi_handle_v1 handle) const
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return { std::nullopt, SystemVerilogVpiValueError::InvalidSimulation };
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        switch (error) {
        case SystemVerilogVpiObjectError::InvalidSimulation:
            return { std::nullopt,
                SystemVerilogVpiValueError::InvalidSimulation };
        case SystemVerilogVpiObjectError::CrossSimulation:
            return { std::nullopt,
                SystemVerilogVpiValueError::CrossSimulation };
        case SystemVerilogVpiObjectError::StaleHandle:
            return { std::nullopt, SystemVerilogVpiValueError::StaleHandle };
        case SystemVerilogVpiObjectError::ReleasedHandle:
            return { std::nullopt,
                SystemVerilogVpiValueError::ReleasedHandle };
        default:
            return { std::nullopt, SystemVerilogVpiValueError::InvalidHandle };
        }
    }
    const auto& record = records_[slot];
    if (!record.type) {
        return { std::nullopt, SystemVerilogVpiValueError::NotReadable };
    }
    const auto& value = record.forced_value ? record.forced_value : record.value;
    if (!value) {
        return { std::nullopt, SystemVerilogVpiValueError::NotBound };
    }
    return { *value, SystemVerilogVpiValueError::None };
}

struct SystemVerilogVpiDataReadService::Impl final {
    struct Sample {
        SystemVerilogVpiDataReadPosition position;
        SystemVerilogVpiStoredValue value;
        bool value_change { };
    };
    struct History {
        bool loaded { };
        std::vector<Sample> samples;
    };
    struct ReaderObject {
        SystemVerilogVpiDataReadObjectKind kind {
            SystemVerilogVpiDataReadObjectKind::TraverseObject
        };
        bool live { true };
        fsim_vpi_handle_v1 design_object { };
        std::optional<std::size_t> cursor;
        std::vector<fsim_vpi_handle_v1> objects;
        std::vector<SystemVerilogVpiDataReadHandle> traverses;
        std::optional<SystemVerilogVpiDataReadPosition> position;
    };

    Impl(SystemVerilogVpiObjectRegistry& object_registry,
        const SystemVerilogVpiDataReadAccess selected_access,
        std::string selected_database,
        Clock selected_clock,
        SystemVerilogVpiDataReadLimits selected_limits)
        : objects(object_registry), access(selected_access),
          database(std::move(selected_database)),
          clock(std::move(selected_clock)), limits(selected_limits)
    {
        const auto identity = next_extension_identity.fetch_add(1U);
        if (!objects.valid() || !valid_access(access) || identity == 0U
            || limits.maximum_loaded_objects == 0U
            || limits.maximum_value_changes == 0U
            || limits.maximum_collections == 0U
            || limits.maximum_collection_members == 0U
            || limits.maximum_traverse_objects == 0U
            || limits.maximum_database_name_bytes == 0U
            || database.size() > limits.maximum_database_name_bytes
            || (access != SystemVerilogVpiDataReadAccess::LimitedInteractive
                && database.empty())) {
            return;
        }
        owner = identity;
    }

    [[nodiscard]] SystemVerilogVpiDataReadPosition now() const noexcept
    {
        if (!clock) {
            return { };
        }
        try {
            return clock();
        } catch (...) {
            return { };
        }
    }

    [[nodiscard]] SystemVerilogVpiDataReadHandle handle(
        const std::uint64_t id,
        const SystemVerilogVpiDataReadObjectKind kind) const noexcept
    {
        return { owner, id, kind };
    }

    [[nodiscard]] SystemVerilogVpiDataReadError resolve(
        const SystemVerilogVpiDataReadHandle value,
        ReaderObject*& result) noexcept
    {
        if (!value || value.owner != owner) {
            return value.owner != 0U && value.owner != owner
                ? SystemVerilogVpiDataReadError::CrossExtension
                : SystemVerilogVpiDataReadError::InvalidHandle;
        }
        const auto found = reader_objects.find(value.id);
        if (found == reader_objects.end() || found->second.kind != value.kind) {
            return SystemVerilogVpiDataReadError::InvalidHandle;
        }
        if (!found->second.live) {
            return SystemVerilogVpiDataReadError::ReleasedHandle;
        }
        result = &found->second;
        return SystemVerilogVpiDataReadError::None;
    }

    [[nodiscard]] SystemVerilogVpiDataReadError resolve(
        const SystemVerilogVpiDataReadHandle value,
        const ReaderObject*& result) const noexcept
    {
        ReaderObject* mutable_result { };
        const auto error = const_cast<Impl*>(this)->resolve(
            value, mutable_result);
        result = mutable_result;
        return error;
    }

    [[nodiscard]] SystemVerilogVpiDataReadError append(
        const fsim_vpi_handle_v1 object,
        const SystemVerilogVpiDataReadPosition position,
        SystemVerilogVpiStoredValue value,
        const bool explicit_publication)
    {
        const auto type = objects.type_info(object);
        if (!type) {
            return object_error(type.error);
        }
        if (!validate_systemverilog_vpi_stored_value(*type.value, value)) {
            return SystemVerilogVpiDataReadError::TypeMismatch;
        }
        std::scoped_lock lock { mutex };
        if (owner == 0U) {
            return SystemVerilogVpiDataReadError::InvalidSimulation;
        }
        auto found = histories.find(object);
        if (!explicit_publication
            && access == SystemVerilogVpiDataReadAccess::PostProcess) {
            return SystemVerilogVpiDataReadError::None;
        }
        if (!explicit_publication
            && access == SystemVerilogVpiDataReadAccess::LimitedInteractive
            && (found == histories.end() || !found->second.loaded)) {
            return SystemVerilogVpiDataReadError::None;
        }
        if (found == histories.end()) {
            if (histories.size() >= limits.maximum_loaded_objects) {
                return SystemVerilogVpiDataReadError::ResourceLimit;
            }
            try {
                found = histories.emplace(object, History { }).first;
            } catch (...) {
                return SystemVerilogVpiDataReadError::ResourceLimit;
            }
        }
        auto& samples = found->second.samples;
        if (!samples.empty() && position < samples.back().position) {
            return SystemVerilogVpiDataReadError::OutOfOrder;
        }
        if (!samples.empty() && position == samples.back().position) {
            samples.back().value = std::move(value);
            samples.back().value_change = true;
            return SystemVerilogVpiDataReadError::None;
        }
        if (value_change_count >= limits.maximum_value_changes) {
            return SystemVerilogVpiDataReadError::ResourceLimit;
        }
        try {
            samples.push_back({ position, std::move(value), true });
        } catch (...) {
            return SystemVerilogVpiDataReadError::ResourceLimit;
        }
        ++value_change_count;
        return SystemVerilogVpiDataReadError::None;
    }

    [[nodiscard]] SystemVerilogVpiDataReadError load_one(
        const fsim_vpi_handle_v1 object)
    {
        return load_many({ object });
    }

    [[nodiscard]] SystemVerilogVpiDataReadError load_many(
        std::vector<fsim_vpi_handle_v1> selected)
    {
        struct Prepared {
            fsim_vpi_handle_v1 object { };
            std::optional<SystemVerilogVpiStoredValue> current;
        };
        std::ranges::sort(selected);
        selected.erase(std::unique(selected.begin(), selected.end()),
            selected.end());
        std::vector<Prepared> prepared;
        try {
            prepared.reserve(selected.size());
            for (const auto object : selected) {
                const auto lookup = objects.lookup(object);
                if (!lookup) {
                    return object_error(lookup.error);
                }
                if (!traversable_object(lookup.value->kind)
                    || !objects.type_info(object)) {
                    return SystemVerilogVpiDataReadError::InvalidObject;
                }
                const auto current = objects.stored_value(object);
                if (!current
                    && current.error != SystemVerilogVpiValueError::NotBound) {
                    return value_error(current.error);
                }
                prepared.push_back(
                    { object, current ? current.value : std::nullopt });
            }
        } catch (...) {
            return SystemVerilogVpiDataReadError::ResourceLimit;
        }
        const auto position = now();
        std::scoped_lock lock { mutex };
        if (closed) {
            return SystemVerilogVpiDataReadError::Closed;
        }
        std::size_t additional_histories { };
        std::size_t additional_loaded { };
        std::size_t additional_samples { };
        for (const auto& item : prepared) {
            const auto found = histories.find(item.object);
            if (found == histories.end()) {
                ++additional_histories;
                ++additional_loaded;
                additional_samples += item.current.has_value();
            } else if (!found->second.loaded) {
                ++additional_loaded;
                additional_samples += item.current
                    && (found->second.samples.empty()
                        || (found->second.samples.back().position < position
                            && found->second.samples.back().value
                                != *item.current));
            }
        }
        if (histories.size() + additional_histories
                > limits.maximum_loaded_objects
            || loaded_count + additional_loaded
                > limits.maximum_loaded_objects
            || value_change_count + additional_samples
                > limits.maximum_value_changes) {
            return SystemVerilogVpiDataReadError::ResourceLimit;
        }

        // Stage only histories selected by this request. The final swaps and
        // node transfers cannot expose a partially loaded collection.
        std::map<fsim_vpi_handle_v1, History> staged;
        try {
            for (const auto& item : prepared) {
                const auto found = histories.find(item.object);
                auto [entry, inserted] = staged.emplace(item.object,
                    found == histories.end() ? History { } : found->second);
                (void)inserted;
                auto& history = entry->second;
                if (found != histories.end() && found->second.loaded) {
                    continue;
                }
                if (item.current) {
                    if (history.samples.empty()
                        || (history.samples.back().position < position
                            && history.samples.back().value
                                != *item.current)) {
                        history.samples.push_back(
                            { position, *item.current, false });
                    } else if (history.samples.back().position == position) {
                        history.samples.back().value = *item.current;
                    }
                }
                history.loaded = true;
            }
        } catch (...) {
            return SystemVerilogVpiDataReadError::ResourceLimit;
        }
        for (auto iterator = staged.begin(); iterator != staged.end();) {
            const auto existing = histories.find(iterator->first);
            if (existing == histories.end()) {
                auto node = staged.extract(iterator++);
                histories.insert(std::move(node));
            } else {
                std::swap(existing->second, iterator->second);
                ++iterator;
            }
        }
        loaded_count += additional_loaded;
        value_change_count += additional_samples;
        return SystemVerilogVpiDataReadError::None;
    }

    [[nodiscard]] bool within_scope(
        fsim_vpi_handle_v1 object,
        const fsim_vpi_handle_v1 scope) const
    {
        while (object != 0U) {
            if (object == scope) {
                return true;
            }
            const auto info = objects.lookup(object);
            if (!info) {
                return false;
            }
            object = info.value->parent;
        }
        return false;
    }

    [[nodiscard]] SystemVerilogVpiDataReadHandleResult add_reader_object(
        ReaderObject record)
    {
        const bool traverse
            = record.kind == SystemVerilogVpiDataReadObjectKind::TraverseObject;
        if ((traverse && traverse_count >= limits.maximum_traverse_objects)
            || (!traverse && collection_count >= limits.maximum_collections)
            || next_reader_object == 0U) {
            return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
        }
        const auto id = next_reader_object++;
        const auto kind = record.kind;
        try {
            reader_objects.emplace(id, std::move(record));
        } catch (...) {
            --next_reader_object;
            return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
        }
        traverse ? ++traverse_count : ++collection_count;
        return { handle(id, kind), SystemVerilogVpiDataReadError::None };
    }

    SystemVerilogVpiObjectRegistry& objects;
    SystemVerilogVpiDataReadAccess access;
    std::string database;
    Clock clock;
    SystemVerilogVpiDataReadLimits limits;
    std::uint64_t owner { };
    std::uint64_t observer { };
    bool closed { };
    mutable std::mutex mutex;
    std::map<fsim_vpi_handle_v1, History> histories;
    std::map<std::uint64_t, ReaderObject> reader_objects;
    std::uint64_t next_reader_object { 1U };
    std::size_t loaded_count { };
    std::size_t value_change_count { };
    std::size_t traverse_count { };
    std::size_t collection_count { };
};

SystemVerilogVpiDataReadService::SystemVerilogVpiDataReadService(
    SystemVerilogVpiObjectRegistry& objects,
    const SystemVerilogVpiDataReadAccess access,
    std::string database_name,
    Clock clock,
    SystemVerilogVpiDataReadLimits limits)
    : impl_(std::make_shared<Impl>(objects, access,
          std::move(database_name), std::move(clock), limits))
{
    if (impl_->owner == 0U) {
        return;
    }
    const std::weak_ptr<Impl> weak = impl_;
    const auto observer = objects.add_value_observer(
        [weak](const fsim_vpi_handle_v1 object,
            const SystemVerilogVpiStoredValue& value) {
            if (const auto impl = weak.lock()) {
                (void)impl->append(object, impl->now(), value, false);
            }
        });
    if (!observer) {
        impl_->owner = 0U;
        return;
    }
    impl_->observer = *observer;
}

SystemVerilogVpiDataReadService::~SystemVerilogVpiDataReadService()
{
    if (impl_ && impl_->observer != 0U) {
        (void)impl_->objects.remove_value_observer(impl_->observer);
    }
}

bool SystemVerilogVpiDataReadService::valid() const noexcept
{
    return impl_ && impl_->owner != 0U;
}

bool SystemVerilogVpiDataReadService::closed() const noexcept
{
    if (!valid()) {
        return true;
    }
    std::scoped_lock lock { impl_->mutex };
    return impl_->closed;
}

std::uint64_t SystemVerilogVpiDataReadService::extension_identity() const noexcept
{
    return valid() ? impl_->owner : 0U;
}

SystemVerilogVpiDataReadAccess
SystemVerilogVpiDataReadService::access() const noexcept
{
    return impl_->access;
}

std::string SystemVerilogVpiDataReadService::database_name() const
{
    return valid() ? impl_->database : std::string { };
}

SystemVerilogVpiDataReadError SystemVerilogVpiDataReadService::close()
{
    if (!valid()) {
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    }
    std::scoped_lock lock { impl_->mutex };
    if (impl_->closed) {
        return SystemVerilogVpiDataReadError::Closed;
    }
    impl_->closed = true;
    return SystemVerilogVpiDataReadError::None;
}

SystemVerilogVpiDataReadError SystemVerilogVpiDataReadService::load(
    const fsim_vpi_handle_v1 object)
{
    return valid() ? impl_->load_one(object)
                   : SystemVerilogVpiDataReadError::InvalidSimulation;
}

SystemVerilogVpiDataReadError SystemVerilogVpiDataReadService::load(
    const SystemVerilogVpiDataReadHandle collection)
{
    if (!valid()) {
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    }
    std::vector<fsim_vpi_handle_v1> objects;
    {
        std::scoped_lock lock { impl_->mutex };
        const Impl::ReaderObject* record { };
        const auto error = impl_->resolve(collection, record);
        if (error != SystemVerilogVpiDataReadError::None) {
            return error;
        }
        if (record->kind
            != SystemVerilogVpiDataReadObjectKind::ObjectCollection) {
            return SystemVerilogVpiDataReadError::InvalidCollection;
        }
        objects = record->objects;
    }
    return impl_->load_many(std::move(objects));
}

SystemVerilogVpiDataReadError SystemVerilogVpiDataReadService::load_init(
    const std::optional<SystemVerilogVpiDataReadHandle> collection,
    const std::optional<fsim_vpi_handle_v1> scope,
    const std::uint32_t levels)
{
    if (!valid()) {
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    }
    if (collection && scope) {
        return SystemVerilogVpiDataReadError::InvalidObject;
    }
    if (collection) {
        return load(*collection);
    }
    if (!scope) {
        return SystemVerilogVpiDataReadError::InvalidObject;
    }
    const auto root = impl_->objects.lookup(*scope);
    if (!root) {
        return object_error(root.error);
    }
    std::vector<std::pair<fsim_vpi_handle_v1, std::uint32_t>> pending {
        { *scope, 0U }
    };
    std::vector<fsim_vpi_handle_v1> selected;
    while (!pending.empty()) {
        const auto [parent, depth] = pending.back();
        pending.pop_back();
        if (traversable_object(
                impl_->objects.lookup(parent).value->kind)
            && impl_->objects.type_info(parent)) {
            selected.push_back(parent);
        }
        if (levels != 0U && depth >= levels) {
            continue;
        }
        auto iterator = impl_->objects.iterate_children(parent);
        if (!iterator) {
            return iterator.error == SystemVerilogVpiIteratorError::ResourceLimit
                ? SystemVerilogVpiDataReadError::ResourceLimit
                : SystemVerilogVpiDataReadError::InvalidObject;
        }
        for (;;) {
            const auto child = impl_->objects.scan(iterator.value);
            if (child.error == SystemVerilogVpiIteratorError::End) {
                break;
            }
            if (!child) {
                (void)impl_->objects.release_iterator(iterator.value);
                return SystemVerilogVpiDataReadError::InvalidObject;
            }
            pending.emplace_back(child.value, depth + 1U);
        }
        (void)impl_->objects.release_iterator(iterator.value);
    }
    std::ranges::sort(selected, [&](const auto left, const auto right) {
        return impl_->objects.lookup(left).value->ordinal
            < impl_->objects.lookup(right).value->ordinal;
    });
    return impl_->load_many(std::move(selected));
}

SystemVerilogVpiDataReadError SystemVerilogVpiDataReadService::unload(
    const fsim_vpi_handle_v1 object)
{
    if (!valid()) {
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    }
    const auto lookup = impl_->objects.lookup(object);
    if (!lookup) {
        return object_error(lookup.error);
    }
    std::scoped_lock lock { impl_->mutex };
    if (impl_->closed) {
        return SystemVerilogVpiDataReadError::Closed;
    }
    const auto found = impl_->histories.find(object);
    if (found == impl_->histories.end() || !found->second.loaded) {
        return SystemVerilogVpiDataReadError::NotLoaded;
    }
    found->second.loaded = false;
    --impl_->loaded_count;
    return SystemVerilogVpiDataReadError::None;
}

SystemVerilogVpiDataReadError SystemVerilogVpiDataReadService::unload(
    const SystemVerilogVpiDataReadHandle collection)
{
    if (!valid()) {
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    }
    std::vector<fsim_vpi_handle_v1> objects;
    {
        std::scoped_lock lock { impl_->mutex };
        const Impl::ReaderObject* record { };
        const auto error = impl_->resolve(collection, record);
        if (error != SystemVerilogVpiDataReadError::None) {
            return error;
        }
        if (record->kind
            != SystemVerilogVpiDataReadObjectKind::ObjectCollection) {
            return SystemVerilogVpiDataReadError::InvalidCollection;
        }
        objects = record->objects;
    }
    for (const auto object : objects) {
        const auto lookup = impl_->objects.lookup(object);
        if (!lookup) {
            return object_error(lookup.error);
        }
    }
    std::scoped_lock lock { impl_->mutex };
    if (impl_->closed) {
        return SystemVerilogVpiDataReadError::Closed;
    }
    for (const auto object : objects) {
        const auto found = impl_->histories.find(object);
        if (found != impl_->histories.end() && found->second.loaded) {
            found->second.loaded = false;
            --impl_->loaded_count;
        }
    }
    return SystemVerilogVpiDataReadError::None;
}

SystemVerilogVpiDataReadHandleResult
SystemVerilogVpiDataReadService::create_object_collection(
    const std::optional<SystemVerilogVpiDataReadHandle> collection,
    const std::optional<fsim_vpi_handle_v1> object)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    if (object && !impl_->objects.lookup(*object)) {
        return { { }, SystemVerilogVpiDataReadError::InvalidObject };
    }
    std::scoped_lock lock { impl_->mutex };
    if (impl_->closed) {
        return { { }, SystemVerilogVpiDataReadError::Closed };
    }
    if (!collection) {
        Impl::ReaderObject record;
        record.kind = SystemVerilogVpiDataReadObjectKind::ObjectCollection;
        if (object) {
            record.objects.push_back(*object);
        }
        return impl_->add_reader_object(std::move(record));
    }
    Impl::ReaderObject* record { };
    const auto error = impl_->resolve(*collection, record);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { { }, error };
    }
    if (record->kind
        != SystemVerilogVpiDataReadObjectKind::ObjectCollection) {
        return { { }, SystemVerilogVpiDataReadError::InvalidCollection };
    }
    if (!object) {
        return { *collection, SystemVerilogVpiDataReadError::None };
    }
    if (record->objects.size() >= impl_->limits.maximum_collection_members) {
        return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
    }
    try {
        if (std::ranges::find(record->objects, *object)
            == record->objects.end()) {
            record->objects.push_back(*object);
        }
    } catch (...) {
        return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
    }
    return { *collection, SystemVerilogVpiDataReadError::None };
}

SystemVerilogVpiDataReadHandleResult
SystemVerilogVpiDataReadService::create_traverse(
    const fsim_vpi_handle_v1 object)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    const auto lookup = impl_->objects.lookup(object);
    if (!lookup) {
        return { { }, object_error(lookup.error) };
    }
    std::scoped_lock lock { impl_->mutex };
    if (impl_->closed) {
        return { { }, SystemVerilogVpiDataReadError::Closed };
    }
    const auto history = impl_->histories.find(object);
    if (history == impl_->histories.end() || !history->second.loaded) {
        return { { }, SystemVerilogVpiDataReadError::NotLoaded };
    }
    Impl::ReaderObject record;
    record.kind = SystemVerilogVpiDataReadObjectKind::TraverseObject;
    record.design_object = object;
    if (!history->second.samples.empty()) {
        record.cursor = 0U;
        record.position = history->second.samples.front().position;
    }
    return impl_->add_reader_object(std::move(record));
}

SystemVerilogVpiDataReadHandleResult
SystemVerilogVpiDataReadService::create_traverse_collection(
    const std::optional<SystemVerilogVpiDataReadHandle> collection,
    const std::optional<SystemVerilogVpiDataReadHandle> traverse)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    std::scoped_lock lock { impl_->mutex };
    if (impl_->closed) {
        return { { }, SystemVerilogVpiDataReadError::Closed };
    }
    if (traverse) {
        const Impl::ReaderObject* member { };
        const auto error = impl_->resolve(*traverse, member);
        if (error != SystemVerilogVpiDataReadError::None) {
            return { { }, error };
        }
        if (member->kind
            != SystemVerilogVpiDataReadObjectKind::TraverseObject) {
            return { { }, SystemVerilogVpiDataReadError::InvalidObject };
        }
    }
    if (!collection) {
        Impl::ReaderObject record;
        record.kind = SystemVerilogVpiDataReadObjectKind::TraverseCollection;
        if (traverse) {
            record.traverses.push_back(*traverse);
        }
        return impl_->add_reader_object(std::move(record));
    }
    Impl::ReaderObject* record { };
    const auto error = impl_->resolve(*collection, record);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { { }, error };
    }
    if (record->kind
        != SystemVerilogVpiDataReadObjectKind::TraverseCollection) {
        return { { }, SystemVerilogVpiDataReadError::InvalidCollection };
    }
    if (!traverse) {
        return { *collection, SystemVerilogVpiDataReadError::None };
    }
    if (record->traverses.size()
        >= impl_->limits.maximum_collection_members) {
        return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
    }
    try {
        if (std::ranges::find(record->traverses, *traverse)
            == record->traverses.end()) {
            record->traverses.push_back(*traverse);
        }
    } catch (...) {
        return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
    }
    return { *collection, SystemVerilogVpiDataReadError::None };
}

SystemVerilogVpiDataReadStatusResult
SystemVerilogVpiDataReadService::property(
    const SystemVerilogVpiDataReadProperty selected,
    const fsim_vpi_handle_v1 object) const
{
    if (!valid()) {
        return { false, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    if (!valid_property(selected)) {
        return { false, SystemVerilogVpiDataReadError::InvalidProperty };
    }
    const auto lookup = impl_->objects.lookup(object);
    if (!lookup) {
        return { false, object_error(lookup.error) };
    }
    const auto current = impl_->now();
    std::scoped_lock lock { impl_->mutex };
    const auto found = impl_->histories.find(object);
    switch (selected) {
    case SystemVerilogVpiDataReadProperty::IsLoaded:
        return { found != impl_->histories.end() && found->second.loaded, { } };
    case SystemVerilogVpiDataReadProperty::HasDataValueChange:
        return { found != impl_->histories.end()
                && std::ranges::any_of(found->second.samples,
                    &Impl::Sample::value_change), { } };
    case SystemVerilogVpiDataReadProperty::HasValueChange:
        return { found != impl_->histories.end()
                && !found->second.samples.empty()
                && found->second.samples.back().position == current
                && found->second.samples.back().value_change, { } };
    case SystemVerilogVpiDataReadProperty::HasNoValue:
        return { found == impl_->histories.end()
                || found->second.samples.empty(), { } };
    case SystemVerilogVpiDataReadProperty::BelongsToExtension:
        return { true, { } };
    }
    return { false, SystemVerilogVpiDataReadError::InvalidProperty };
}

SystemVerilogVpiDataReadStatusResult
SystemVerilogVpiDataReadService::property(
    const SystemVerilogVpiDataReadProperty selected,
    const SystemVerilogVpiDataReadHandle object) const
{
    if (!valid()) {
        return { false, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    if (!valid_property(selected)) {
        return { false, SystemVerilogVpiDataReadError::InvalidProperty };
    }
    std::scoped_lock lock { impl_->mutex };
    const Impl::ReaderObject* record { };
    const auto error = impl_->resolve(object, record);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { false, error };
    }
    if (selected == SystemVerilogVpiDataReadProperty::BelongsToExtension) {
        return { true, { } };
    }
    if (record->kind
        != SystemVerilogVpiDataReadObjectKind::TraverseObject) {
        return { false, SystemVerilogVpiDataReadError::InvalidProperty };
    }
    const auto history = impl_->histories.find(record->design_object);
    const bool has_data = history != impl_->histories.end()
        && std::ranges::any_of(
            history->second.samples, &Impl::Sample::value_change);
    switch (selected) {
    case SystemVerilogVpiDataReadProperty::IsLoaded:
        return { history != impl_->histories.end() && history->second.loaded,
            { } };
    case SystemVerilogVpiDataReadProperty::HasDataValueChange:
        return { has_data, { } };
    case SystemVerilogVpiDataReadProperty::HasValueChange:
        return { has_data && record->cursor && record->position
                && history->second.samples[*record->cursor].position
                    == *record->position
                && history->second.samples[*record->cursor].value_change,
            { } };
    case SystemVerilogVpiDataReadProperty::HasNoValue:
        return { !record->cursor, { } };
    case SystemVerilogVpiDataReadProperty::BelongsToExtension:
        return { true, { } };
    }
    return { false, SystemVerilogVpiDataReadError::InvalidProperty };
}

SystemVerilogVpiDataReadHandleResult SystemVerilogVpiDataReadService::filter(
    const SystemVerilogVpiDataReadHandle collection,
    const SystemVerilogVpiDataReadFilter& filter)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    if (filter.object_kind.has_value() == filter.property.has_value()
        || (filter.property && !valid_property(*filter.property))) {
        return { { }, SystemVerilogVpiDataReadError::InvalidProperty };
    }
    std::scoped_lock lock { impl_->mutex };
    const Impl::ReaderObject* source { };
    const auto error = impl_->resolve(collection, source);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { { }, error };
    }
    if (source->kind
            != SystemVerilogVpiDataReadObjectKind::ObjectCollection
        && source->kind
            != SystemVerilogVpiDataReadObjectKind::TraverseCollection) {
        return { { }, SystemVerilogVpiDataReadError::InvalidCollection };
    }
    Impl::ReaderObject result;
    result.kind = source->kind;
    result.position = source->position;
    try {
        if (source->kind
            == SystemVerilogVpiDataReadObjectKind::ObjectCollection) {
            for (const auto object : source->objects) {
                bool matches { };
                if (filter.object_kind) {
                    const auto info = impl_->objects.lookup(object);
                    matches = info && info.value->kind == *filter.object_kind;
                } else {
                    const auto found = impl_->histories.find(object);
                    switch (*filter.property) {
                    case SystemVerilogVpiDataReadProperty::IsLoaded:
                        matches = found != impl_->histories.end()
                            && found->second.loaded;
                        break;
                    case SystemVerilogVpiDataReadProperty::HasDataValueChange:
                        matches = found != impl_->histories.end()
                            && std::ranges::any_of(found->second.samples,
                                &Impl::Sample::value_change);
                        break;
                    case SystemVerilogVpiDataReadProperty::HasValueChange:
                        matches = false;
                        break;
                    case SystemVerilogVpiDataReadProperty::HasNoValue:
                        matches = found == impl_->histories.end()
                            || found->second.samples.empty();
                        break;
                    case SystemVerilogVpiDataReadProperty::BelongsToExtension:
                        matches = static_cast<bool>(impl_->objects.lookup(object));
                        break;
                    }
                }
                if (matches == filter.match) {
                    result.objects.push_back(object);
                }
            }
        } else {
            for (const auto traverse : source->traverses) {
                const Impl::ReaderObject* member { };
                if (impl_->resolve(traverse, member)
                    != SystemVerilogVpiDataReadError::None) {
                    continue;
                }
                bool matches { };
                if (filter.object_kind) {
                    const auto info
                        = impl_->objects.lookup(member->design_object);
                    matches = info && info.value->kind == *filter.object_kind;
                } else {
                    const auto history
                        = impl_->histories.find(member->design_object);
                    const bool has_data = history != impl_->histories.end()
                        && std::ranges::any_of(history->second.samples,
                            &Impl::Sample::value_change);
                    switch (*filter.property) {
                    case SystemVerilogVpiDataReadProperty::IsLoaded:
                        matches = history != impl_->histories.end()
                            && history->second.loaded;
                        break;
                    case SystemVerilogVpiDataReadProperty::HasDataValueChange:
                        matches = has_data;
                        break;
                    case SystemVerilogVpiDataReadProperty::HasValueChange:
                        matches = has_data && member->cursor && member->position
                            && history->second.samples[*member->cursor].position
                                == *member->position
                            && history->second.samples[*member->cursor]
                                .value_change;
                        break;
                    case SystemVerilogVpiDataReadProperty::HasNoValue:
                        matches = !member->cursor;
                        break;
                    case SystemVerilogVpiDataReadProperty::BelongsToExtension:
                        matches = true;
                        break;
                    }
                }
                if (matches == filter.match) {
                    result.traverses.push_back(traverse);
                }
            }
        }
    } catch (...) {
        return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
    }
    return impl_->add_reader_object(std::move(result));
}

SystemVerilogVpiDataReadHandleResult SystemVerilogVpiDataReadService::go_to(
    const SystemVerilogVpiDataReadHandle traverse,
    const SystemVerilogVpiDataReadControl control,
    const std::optional<SystemVerilogVpiDataReadPosition> requested)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    if (!valid_control(control)
        || (control == SystemVerilogVpiDataReadControl::Time)
            != requested.has_value()) {
        return { { }, SystemVerilogVpiDataReadError::InvalidControl };
    }
    std::scoped_lock lock { impl_->mutex };
    const Impl::ReaderObject* source { };
    const auto error = impl_->resolve(traverse, source);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { { }, error };
    }
    if (source->kind
        == SystemVerilogVpiDataReadObjectKind::TraverseObject) {
        const auto history = impl_->histories.find(source->design_object);
        if (history == impl_->histories.end()
            || history->second.samples.empty()) {
            return { { }, SystemVerilogVpiDataReadError::NoValue };
        }
        const auto& samples = history->second.samples;
        std::optional<std::size_t> cursor;
        switch (control) {
        case SystemVerilogVpiDataReadControl::MinimumTime:
            cursor = 0U;
            break;
        case SystemVerilogVpiDataReadControl::MaximumTime:
            cursor = samples.size() - 1U;
            break;
        case SystemVerilogVpiDataReadControl::PreviousValueChange:
            if (source->cursor && *source->cursor != 0U) {
                for (auto index = *source->cursor; index-- > 0U;) {
                    if (samples[index].value_change) {
                        cursor = index;
                        break;
                    }
                }
            }
            break;
        case SystemVerilogVpiDataReadControl::NextValueChange:
            if (source->cursor) {
                for (auto index = *source->cursor + 1U;
                    index < samples.size(); ++index) {
                    if (samples[index].value_change) {
                        cursor = index;
                        break;
                    }
                }
            }
            break;
        case SystemVerilogVpiDataReadControl::Time: {
            const auto found = std::ranges::upper_bound(
                samples, *requested, { }, &Impl::Sample::position);
            if (found != samples.begin()) {
                cursor = static_cast<std::size_t>(
                    std::distance(samples.begin(), found) - 1);
            }
            break;
        }
        }
        if (!cursor && control != SystemVerilogVpiDataReadControl::Time) {
            return { { }, SystemVerilogVpiDataReadError::NoValueChange };
        }
        Impl::ReaderObject result = *source;
        result.cursor = cursor;
        result.position = control == SystemVerilogVpiDataReadControl::Time
            ? requested
            : std::optional { samples[*cursor].position };
        return impl_->add_reader_object(std::move(result));
    }
    if (source->kind
        != SystemVerilogVpiDataReadObjectKind::TraverseCollection) {
        return { { }, SystemVerilogVpiDataReadError::InvalidObject };
    }

    std::optional<SystemVerilogVpiDataReadPosition> target = requested;
    for (const auto member_handle : source->traverses) {
        const Impl::ReaderObject* member { };
        if (impl_->resolve(member_handle, member)
            != SystemVerilogVpiDataReadError::None) {
            continue;
        }
        const auto history = impl_->histories.find(member->design_object);
        if (history == impl_->histories.end()) {
            continue;
        }
        for (const auto& sample : history->second.samples) {
            bool candidate { };
            switch (control) {
            case SystemVerilogVpiDataReadControl::MinimumTime:
                candidate = !target || sample.position < *target;
                break;
            case SystemVerilogVpiDataReadControl::MaximumTime:
                candidate = !target || sample.position > *target;
                break;
            case SystemVerilogVpiDataReadControl::PreviousValueChange:
                candidate = source->position && sample.position < *source->position
                    && (!target || sample.position > *target);
                break;
            case SystemVerilogVpiDataReadControl::NextValueChange:
                candidate = source->position && sample.position > *source->position
                    && (!target || sample.position < *target);
                break;
            case SystemVerilogVpiDataReadControl::Time:
                candidate = false;
                break;
            }
            const bool requires_change
                = control
                    == SystemVerilogVpiDataReadControl::PreviousValueChange
                || control
                    == SystemVerilogVpiDataReadControl::NextValueChange;
            if (candidate && (!requires_change || sample.value_change)) {
                target = sample.position;
            }
        }
    }
    if (!target) {
        return { { }, SystemVerilogVpiDataReadError::NoValueChange };
    }
    if (impl_->traverse_count + source->traverses.size()
            > impl_->limits.maximum_traverse_objects
        || impl_->collection_count >= impl_->limits.maximum_collections) {
        return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
    }
    Impl::ReaderObject result;
    result.kind = SystemVerilogVpiDataReadObjectKind::TraverseCollection;
    result.position = target;
    std::vector<std::uint64_t> created_ids;
    try {
        result.traverses.reserve(source->traverses.size());
        created_ids.reserve(source->traverses.size());
        for (const auto member_handle : source->traverses) {
            const Impl::ReaderObject* source_member { };
            if (impl_->resolve(member_handle, source_member)
                != SystemVerilogVpiDataReadError::None) {
                continue;
            }
            Impl::ReaderObject member = *source_member;
            member.cursor.reset();
            member.position = target;
            const auto history = impl_->histories.find(member.design_object);
            if (history != impl_->histories.end()) {
                const auto found = std::ranges::upper_bound(
                    history->second.samples, *target, { },
                    &Impl::Sample::position);
                if (found != history->second.samples.begin()) {
                    member.cursor = static_cast<std::size_t>(
                        std::distance(history->second.samples.begin(), found)
                        - 1);
                }
            }
            const auto id = impl_->next_reader_object++;
            impl_->reader_objects.emplace(id, std::move(member));
            ++impl_->traverse_count;
            created_ids.push_back(id);
            result.traverses.push_back(impl_->handle(id,
                SystemVerilogVpiDataReadObjectKind::TraverseObject));
        }
    } catch (...) {
        for (const auto id : created_ids) {
            impl_->reader_objects.erase(id);
            --impl_->traverse_count;
        }
        return { { }, SystemVerilogVpiDataReadError::ResourceLimit };
    }
    const auto published = impl_->add_reader_object(std::move(result));
    if (!published) {
        for (const auto id : created_ids) {
            impl_->reader_objects.erase(id);
            --impl_->traverse_count;
        }
    }
    return published;
}

SystemVerilogVpiDataReadTimeResult SystemVerilogVpiDataReadService::time(
    const SystemVerilogVpiDataReadHandle traverse,
    const SystemVerilogVpiDataReadControl selection) const
{
    if (!valid()) {
        return { { }, SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    if (!valid_control(selection)
        || selection == SystemVerilogVpiDataReadControl::PreviousValueChange
        || selection == SystemVerilogVpiDataReadControl::NextValueChange) {
        return { { }, SystemVerilogVpiDataReadError::InvalidControl };
    }
    std::scoped_lock lock { impl_->mutex };
    const Impl::ReaderObject* record { };
    const auto error = impl_->resolve(traverse, record);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { { }, error };
    }
    if (record->kind
        == SystemVerilogVpiDataReadObjectKind::TraverseCollection) {
        return record->position
            ? SystemVerilogVpiDataReadTimeResult { *record->position, { } }
            : SystemVerilogVpiDataReadTimeResult {
                { }, SystemVerilogVpiDataReadError::NoValue };
    }
    if (record->kind
        != SystemVerilogVpiDataReadObjectKind::TraverseObject) {
        return { { }, SystemVerilogVpiDataReadError::InvalidObject };
    }
    const auto history = impl_->histories.find(record->design_object);
    if (history == impl_->histories.end()
        || history->second.samples.empty()) {
        return { { }, SystemVerilogVpiDataReadError::NoValue };
    }
    switch (selection) {
    case SystemVerilogVpiDataReadControl::MinimumTime:
        return { history->second.samples.front().position, { } };
    case SystemVerilogVpiDataReadControl::MaximumTime:
        return { history->second.samples.back().position, { } };
    case SystemVerilogVpiDataReadControl::Time:
        return record->position
            ? SystemVerilogVpiDataReadTimeResult { *record->position, { } }
            : SystemVerilogVpiDataReadTimeResult {
                { }, SystemVerilogVpiDataReadError::NoValue };
    default:
        break;
    }
    return { { }, SystemVerilogVpiDataReadError::InvalidControl };
}

SystemVerilogVpiDataReadValueResult SystemVerilogVpiDataReadService::value(
    const SystemVerilogVpiDataReadHandle traverse) const
{
    if (!valid()) {
        return { std::nullopt,
            SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    std::scoped_lock lock { impl_->mutex };
    const Impl::ReaderObject* record { };
    const auto error = impl_->resolve(traverse, record);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { std::nullopt, error };
    }
    if (record->kind
        != SystemVerilogVpiDataReadObjectKind::TraverseObject) {
        return { std::nullopt, SystemVerilogVpiDataReadError::InvalidObject };
    }
    const auto history = impl_->histories.find(record->design_object);
    if (history == impl_->histories.end() || !record->cursor
        || *record->cursor >= history->second.samples.size()) {
        return { std::nullopt, SystemVerilogVpiDataReadError::NoValue };
    }
    return { history->second.samples[*record->cursor].value, { } };
}

SystemVerilogVpiDataReadMembersResult
SystemVerilogVpiDataReadService::members(
    const SystemVerilogVpiDataReadHandle collection) const
{
    if (!valid()) {
        return { { }, { },
            SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    std::scoped_lock lock { impl_->mutex };
    const Impl::ReaderObject* record { };
    const auto error = impl_->resolve(collection, record);
    if (error != SystemVerilogVpiDataReadError::None) {
        return { { }, { }, error };
    }
    if (record->kind
            != SystemVerilogVpiDataReadObjectKind::ObjectCollection
        && record->kind
            != SystemVerilogVpiDataReadObjectKind::TraverseCollection) {
        return { { }, { },
            SystemVerilogVpiDataReadError::InvalidCollection };
    }
    return { record->objects, record->traverses, { } };
}

SystemVerilogVpiDataReadMembersResult
SystemVerilogVpiDataReadService::loaded_objects(
    const std::optional<fsim_vpi_handle_v1> scope) const
{
    if (!valid()) {
        return { { }, { },
            SystemVerilogVpiDataReadError::InvalidSimulation };
    }
    if (scope && !impl_->objects.lookup(*scope)) {
        return { { }, { }, SystemVerilogVpiDataReadError::InvalidObject };
    }
    std::vector<std::pair<std::uint64_t, fsim_vpi_handle_v1>> ordered;
    {
        std::scoped_lock lock { impl_->mutex };
        try {
            ordered.reserve(impl_->loaded_count);
            for (const auto& [object, history] : impl_->histories) {
                if (!history.loaded
                    || (scope && !impl_->within_scope(object, *scope))) {
                    continue;
                }
                const auto info = impl_->objects.lookup(object);
                if (info) {
                    ordered.emplace_back(info.value->ordinal, object);
                }
            }
        } catch (...) {
            return { { }, { },
                SystemVerilogVpiDataReadError::ResourceLimit };
        }
    }
    std::ranges::sort(ordered);
    SystemVerilogVpiDataReadMembersResult result;
    try {
        result.objects.reserve(ordered.size());
        for (const auto& [ordinal, object] : ordered) {
            (void)ordinal;
            result.objects.push_back(object);
        }
    } catch (...) {
        result.objects.clear();
        result.error = SystemVerilogVpiDataReadError::ResourceLimit;
    }
    return result;
}

SystemVerilogVpiDataReadError SystemVerilogVpiDataReadService::release(
    const SystemVerilogVpiDataReadHandle object)
{
    if (!valid()) {
        return SystemVerilogVpiDataReadError::InvalidSimulation;
    }
    std::scoped_lock lock { impl_->mutex };
    Impl::ReaderObject* record { };
    const auto error = impl_->resolve(object, record);
    if (error != SystemVerilogVpiDataReadError::None) {
        return error;
    }
    record->live = false;
    record->objects.clear();
    record->traverses.clear();
    if (record->kind
        == SystemVerilogVpiDataReadObjectKind::TraverseObject) {
        --impl_->traverse_count;
    } else {
        --impl_->collection_count;
    }
    return SystemVerilogVpiDataReadError::None;
}

SystemVerilogVpiDataReadError
SystemVerilogVpiDataReadService::publish_value_change(
    const fsim_vpi_handle_v1 object,
    const SystemVerilogVpiDataReadPosition position,
    SystemVerilogVpiStoredValue value)
{
    return valid()
        ? impl_->append(object, position, std::move(value), true)
        : SystemVerilogVpiDataReadError::InvalidSimulation;
}

} // namespace fsim::runtime
