// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <array>
#include <mutex>

namespace fsim::runtime::simir {

namespace {

struct InternedStringBucket {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<const std::string>> values;
};

std::array<InternedStringBucket, 64>& interned_string_buckets()
{
    static std::array<InternedStringBucket, 64> buckets;
    return buckets;
}

} // namespace

std::shared_ptr<const std::string> InternedString::intern(std::string value)
{
    if (value.empty()) {
        return { };
    }
    auto& buckets = interned_string_buckets();
    auto& bucket = buckets[std::hash<std::string_view> { }(value)
        % buckets.size()];
    const std::lock_guard lock { bucket.mutex };
    if (const auto found = bucket.values.find(value);
        found != bucket.values.end()) {
        if (auto existing = found->second.lock()) {
            return existing;
        }
    }
    auto result = std::make_shared<const std::string>(std::move(value));
    bucket.values.insert_or_assign(*result, result);
    return result;
}

InternedString::InternedString(std::string value)
    : value_(intern(std::move(value)))
{
}

InternedString::InternedString(const std::string_view value)
    : InternedString(std::string { value })
{
}

InternedString::InternedString(const char* const value)
    : InternedString(value == nullptr ? std::string { } : std::string { value })
{
}

InternedString& InternedString::operator=(std::string value)
{
    value_ = intern(std::move(value));
    return *this;
}

InternedString& InternedString::operator=(const std::string_view value)
{
    return *this = std::string { value };
}

InternedString& InternedString::operator=(const char* const value)
{
    return *this = value == nullptr ? std::string { } : std::string { value };
}

const std::string& InternedString::str() const noexcept
{
    static const std::string empty;
    return value_ ? *value_ : empty;
}

namespace {

template <typename Mapper>
void remap_program_signals(Operation& operation, Mapper&& mapper)
{
    visit_operation(
        [&](auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, ReadSignal>) {
                value.signal = mapper(value.signal);
                if (value.clock) {
                    value.clock = mapper(*value.clock);
                }
                if (value.gate) {
                    value.gate = mapper(*value.gate);
                }
            } else if constexpr (
                std::is_same_v<Type, WriteBlocking>
                || std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteProjected>
                || std::is_same_v<Type, WriteBlockingSlice>
                || std::is_same_v<Type, WriteUpdateSlice>
                || std::is_same_v<Type, WriteProjectedSlice>
                || std::is_same_v<Type, WriteUpdateDynamicPartSlice>) {
                value.signal = mapper(value.signal);
            }
        },
        operation);
}

template <typename Override>
const Override* find_override(
    const std::vector<Override>& overrides,
    const OperationList::size_type index) noexcept
{
    const auto found = std::ranges::lower_bound(
        overrides,
        static_cast<InstructionIndex>(index),
        { },
        &Override::instruction);
    return found != overrides.end() && found->instruction == index
        ? &*found
        : nullptr;
}

} // namespace

OperationList::OperationList()
    : storage_(std::make_shared<Storage>())
{
}

OperationList::OperationList(std::initializer_list<Operation> operations)
    : storage_(std::make_shared<Storage>(operations))
{
}

OperationList::OperationList(const Storage& operations)
    : storage_(std::make_shared<Storage>(operations))
{
}

OperationList::OperationList(Storage&& operations)
    : storage_(std::make_shared<Storage>(std::move(operations)))
{
}

OperationList& OperationList::operator=(
    std::initializer_list<Operation> operations)
{
    reset(Storage { operations });
    return *this;
}

OperationList& OperationList::operator=(const Storage& operations)
{
    reset(operations);
    return *this;
}

OperationList& OperationList::operator=(Storage&& operations)
{
    reset(std::move(operations));
    return *this;
}

const OperationList::Storage& OperationList::storage() const noexcept
{
    static const Storage empty;
    return storage_ ? *storage_ : empty;
}

void OperationList::reset(Storage operations)
{
    storage_ = std::make_shared<Storage>(std::move(operations));
    signal_remap_.clear();
    debug_overrides_.clear();
    assert_overrides_.clear();
    container_object_overrides_.clear();
    operation_overrides_.clear();
    coverage_hit_overrides_.clear();
    debug_scope_overrides_.clear();
    operation_override_filter_ = 0;
}

OperationList::Storage& OperationList::mutable_storage()
{
    if (!storage_) {
        storage_ = std::make_shared<Storage>();
    }
    if (!storage_.unique() || !signal_remap_.empty()
        || !debug_overrides_.empty() || !assert_overrides_.empty()
        || !container_object_overrides_.empty()
        || !operation_overrides_.empty()
        || !coverage_hit_overrides_.empty()
        || !debug_scope_overrides_.empty()) {
        Storage expanded;
        expanded.reserve(size());
        for (size_type index = 0; index < size(); ++index) {
            expanded.push_back(this->expanded(index));
        }
        reset(std::move(expanded));
    }
    return *storage_;
}

bool OperationList::empty() const noexcept { return storage().empty(); }
OperationList::size_type OperationList::size() const noexcept { return storage().size(); }
OperationList::size_type OperationList::capacity() const noexcept { return storage().capacity(); }
OperationList::size_type OperationList::max_size() const noexcept { return storage().max_size(); }
const Operation& OperationList::operator[](const size_type index) const noexcept
{
    if ((operation_override_filter_
            & (UINT64_C(1) << (index & 63U))) != 0U) {
        if (const auto* override = find_override(
                operation_overrides_, index)) {
            return override->operation;
        }
    }
    return storage()[index];
}
Operation& OperationList::operator[](const size_type index) { return mutable_storage()[index]; }
const Operation& OperationList::at(const size_type index) const
{
    if (index >= size()) {
        throw std::out_of_range { "SimIR operation index is out of range" };
    }
    return (*this)[index];
}
Operation& OperationList::at(const size_type index) { return mutable_storage().at(index); }
const Operation& OperationList::front() const { return at(0); }
Operation& OperationList::front() { return mutable_storage().front(); }
const Operation& OperationList::back() const { return at(size() - 1U); }
Operation& OperationList::back() { return mutable_storage().back(); }
const Operation* OperationList::data() const noexcept { return storage().data(); }
Operation* OperationList::data() { return mutable_storage().data(); }
OperationList::const_iterator OperationList::begin() const noexcept { return { this, 0U }; }
OperationList::const_iterator OperationList::end() const noexcept { return { this, size() }; }
OperationList::const_iterator OperationList::cbegin() const noexcept { return begin(); }
OperationList::const_iterator OperationList::cend() const noexcept { return end(); }
OperationList::iterator OperationList::begin() { return mutable_storage().begin(); }
OperationList::iterator OperationList::end() { return mutable_storage().end(); }
void OperationList::clear() { mutable_storage().clear(); }
void OperationList::reserve(const size_type capacity) { mutable_storage().reserve(capacity); }
void OperationList::resize(const size_type size) { mutable_storage().resize(size); }
void OperationList::push_back(const Operation& operation) { mutable_storage().push_back(operation); }
void OperationList::push_back(Operation&& operation) { mutable_storage().push_back(std::move(operation)); }

void OperationList::replace(const size_type index, Operation operation)
{
    if (index >= size()) {
        throw std::out_of_range { "SimIR operation index is out of range" };
    }
    const auto found = std::ranges::lower_bound(
        operation_overrides_,
        static_cast<InstructionIndex>(index),
        { },
        &OperationOverride::instruction);
    if (found != operation_overrides_.end()
        && found->instruction == index) {
        found->operation = std::move(operation);
    } else {
        operation_overrides_.insert(found,
            { static_cast<InstructionIndex>(index),
                std::move(operation) });
    }
    operation_override_filter_
        |= UINT64_C(1) << (index & 63U);
}

SignalId OperationList::signal(const SignalId canonical) const noexcept
{
    const auto found = std::ranges::lower_bound(
        signal_remap_, canonical, { },
        &std::pair<SignalId, SignalId>::first);
    return found != signal_remap_.end() && found->first == canonical
        ? found->second
        : canonical;
}

const DebugPoint& OperationList::debug_point(
    const size_type index, const DebugPoint& canonical) const noexcept
{
    if (const auto* override = find_override(debug_overrides_, index)) {
        return override->point;
    }
    return canonical;
}

const InternedString& OperationList::debug_scope(
    const InternedString& canonical) const noexcept
{
    const auto found = std::ranges::find(
        debug_scope_overrides_, canonical,
        &DebugScopeOverride::canonical);
    return found == debug_scope_overrides_.end()
        ? canonical
        : found->instance;
}

Assert OperationList::assertion(
    const size_type index, const Assert& canonical) const
{
    auto result = canonical;
    if (const auto* override = find_override(assert_overrides_, index)) {
        result.message = override->message;
        result.source = override->source;
    }
    return result;
}

ContainerObjectId OperationList::container_object(
    const size_type index, const ContainerObjectId canonical) const noexcept
{
    if (const auto* override = find_override(
            container_object_overrides_, index)) {
        return override->object;
    }
    return canonical;
}

::fsim::runtime::CodeCoverageCounterId OperationList::code_coverage_counter(
    const size_type index,
    const ::fsim::runtime::CodeCoverageCounterId canonical) const noexcept
{
    if ((operation_override_filter_
            & (UINT64_C(1) << (index & 63U))) != 0U
        && find_override(operation_overrides_, index) != nullptr) {
        return canonical;
    }
    if (const auto* override = find_override(
            coverage_hit_overrides_, index)) {
        return override->counter;
    }
    return canonical;
}

void OperationList::apply_instance_fields(
    Operation& operation, const size_type index) const
{
    remap_program_signals(operation,
        [&](const SignalId canonical) { return signal(canonical); });
    if (auto* point = operation_get_if<DebugPoint>(&operation)) {
        *point = debug_point(index, *point);
        point->scope = debug_scope(point->scope);
    } else if (auto* assertion = operation_get_if<Assert>(&operation)) {
        *assertion = this->assertion(index, *assertion);
    } else if (auto* read = operation_get_if<ReadContainerObject>(&operation)) {
        read->object = container_object(index, read->object);
    } else if (auto* hit = operation_get_if<CodeCoverageHit>(&operation)) {
        hit->counter = code_coverage_counter(index, hit->counter);
    }
}

Operation OperationList::expanded(const size_type index) const
{
    auto result = at(index);
    apply_instance_fields(result, index);
    return result;
}

bool OperationList::shares_body_with(const OperationList& other) const noexcept
{
    return storage_ == other.storage_;
}

const std::vector<std::pair<SignalId, SignalId>>&
OperationList::signal_remap() const noexcept
{
    return signal_remap_;
}

bool share_process_operations(
    const Process& representative,
    Process& candidate,
    const std::span<const Signal> signals,
    OperationList::Storage* const recycled_operations)
{
    if (representative.operations.size() != candidate.operations.size()
        || representative.register_count != candidate.register_count
        || representative.register_value_kinds
            != candidate.register_value_kinds
        || representative.string_register_count
            != candidate.string_register_count
        || representative.container_register_count
            != candidate.container_register_count
        || representative.container_register_types
            != candidate.container_register_types) {
        return false;
    }

    std::map<SignalId, SignalId> assigned;
    const auto map_signal = [&](const SignalId source,
                                const SignalId target) {
        if (source >= signals.size() || target >= signals.size()
            || signals[source].initial_value.width()
                != signals[target].initial_value.width()
            || signals[source].value_kind != signals[target].value_kind) {
            return false;
        }
        const auto [found, inserted] = assigned.try_emplace(source, target);
        return inserted || found->second == target;
    };

    std::vector<OperationList::DebugOverride> debug_overrides;
    std::vector<OperationList::AssertOverride> assert_overrides;
    std::vector<OperationList::ContainerObjectOverride>
        container_object_overrides;
    std::vector<OperationList::OperationOverride> operation_overrides;
    std::vector<OperationList::CoverageHitOverride> coverage_hit_overrides;
    std::vector<OperationList::DebugScopeOverride> debug_scope_overrides;
    for (std::size_t index = 0;
         index < representative.operations.size(); ++index) {
        bool compatible = true;
        visit_operation(
            [&](const auto& left) {
                using Type = std::decay_t<decltype(left)>;
                const auto* right = operation_get_if<Type>(
                    &candidate.operations[index]);
                if (right == nullptr) {
                    compatible = false;
                    return;
                }
                if constexpr (std::is_same_v<Type, DebugPoint>) {
                    if (left.kind != right->kind
                        || left.source != right->source) {
                        debug_overrides.push_back(
                            { static_cast<InstructionIndex>(index), *right });
                    } else if (left.scope != right->scope) {
                        const auto found = std::ranges::find(
                            debug_scope_overrides, left.scope,
                            &OperationList::DebugScopeOverride::canonical);
                        if (found == debug_scope_overrides.end()) {
                            debug_scope_overrides.push_back(
                                { left.scope, right->scope });
                        } else if (found->instance != right->scope) {
                            compatible = false;
                        }
                    }
                } else if constexpr (
                    std::is_same_v<Type, WaitSensitivity>) {
                    // All sensitivity identities remain process-local in the
                    // Process::static_sensitivity vector.
                } else if constexpr (std::is_same_v<Type, ReadSignal>) {
                    compatible = left.destination == right->destination
                        && left.kind == right->kind
                        && left.ticks == right->ticks
                        && left.clock.has_value()
                            == right->clock.has_value()
                        && left.clock_edge == right->clock_edge
                        && left.gate.has_value() == right->gate.has_value()
                        && map_signal(left.signal, right->signal);
                    if (compatible && left.clock) {
                        compatible = map_signal(*left.clock, *right->clock);
                    }
                    if (compatible && left.gate) {
                        compatible = map_signal(*left.gate, *right->gate);
                    }
                    if (compatible
                        && (left.signal != right->signal
                            || left.clock != right->clock
                            || left.gate != right->gate)) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                candidate.operations[index] });
                    }
                } else if constexpr (
                    std::is_same_v<Type, WriteProjected>) {
                    compatible = left.source == right->source
                        && left.delay == right->delay
                        && left.rejection == right->rejection
                        && left.mode == right->mode
                        && map_signal(left.signal, right->signal);
                    if (compatible && left.signal != right->signal) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                candidate.operations[index] });
                    }
                } else if constexpr (
                    std::is_same_v<Type, WriteProjectedSlice>) {
                    compatible = left.source == right->source
                        && left.offset == right->offset
                        && left.delay == right->delay
                        && left.rejection == right->rejection
                        && left.mode == right->mode
                        && map_signal(left.signal, right->signal);
                    if (compatible && left.signal != right->signal) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                candidate.operations[index] });
                    }
                } else if constexpr (
                    std::is_same_v<Type, WriteBlocking>
                    || std::is_same_v<Type, WriteUpdate>) {
                    compatible = left.source == right->source
                        && map_signal(left.signal, right->signal);
                    if (compatible && left.signal != right->signal) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                candidate.operations[index] });
                    }
                } else if constexpr (
                    std::is_same_v<Type, WriteBlockingSlice>
                    || std::is_same_v<Type, WriteUpdateSlice>) {
                    compatible = left.source == right->source
                        && left.offset == right->offset
                        && map_signal(left.signal, right->signal);
                    if (compatible && left.signal != right->signal) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                candidate.operations[index] });
                    }
                } else if constexpr (
                    std::is_same_v<Type, WriteUpdateDynamicPartSlice>) {
                    compatible = left.source == right->source
                        && left.selection == right->selection
                        && map_signal(left.signal, right->signal);
                    if (compatible && left.signal != right->signal) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                candidate.operations[index] });
                    }
                } else if constexpr (std::is_same_v<Type, LoadConstant>) {
                    compatible = left.destination == right->destination
                        && left.value == right->value;
                } else if constexpr (std::is_same_v<Type, CopyRegister>
                    || std::is_same_v<Type, UnaryNot>
                    || std::is_same_v<Type, LogicalNot>) {
                    compatible = left.destination == right->destination
                        && left.source == right->source;
                } else if constexpr (std::is_same_v<Type, IntegerCheck>) {
                    compatible = left.source == right->source
                        && left.lower == right->lower
                        && left.upper == right->upper;
                } else if constexpr (std::is_same_v<Type, DynamicInsert>) {
                    compatible = left.destination == right->destination
                        && left.target == right->target
                        && left.source == right->source
                        && left.selection == right->selection;
                } else if constexpr (
                    std::is_same_v<Type, DynamicPartSelect>) {
                    compatible = left.destination == right->destination
                        && left.source == right->source
                        && left.base == right->base
                        && left.left == right->left
                        && left.right == right->right
                        && left.width == right->width
                        && left.increasing == right->increasing
                        && left.source_descending
                            == right->source_descending
                        && left.two_state == right->two_state
                        && left.base_offset == right->base_offset;
                } else if constexpr (std::is_same_v<Type, IntegerBinary>
                    || std::is_same_v<Type, Binary>
                    || std::is_same_v<Type, LogicalBinary>) {
                    compatible = left.operation == right->operation
                        && left.destination == right->destination
                        && left.lhs == right->lhs
                        && left.rhs == right->rhs;
                } else if constexpr (std::is_same_v<Type, DynamicExtract>) {
                    compatible = left.destination == right->destination
                        && left.source == right->source
                        && left.selection == right->selection;
                } else if constexpr (
                    std::is_same_v<Type, ReadContainerObject>) {
                    compatible = left.destination == right->destination;
                    if (left.object != right->object) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                Operation { *right } });
                    }
                } else if constexpr (std::is_same_v<Type, ContainerRead>) {
                    compatible = left.destination == right->destination
                        && left.source == right->source
                        && left.index == right->index
                        && left.signed_index == right->signed_index
                        && left.linear_index == right->linear_index
                        && left.string_index == right->string_index;
                } else if constexpr (
                    std::is_same_v<Type, CallableFramePop>) {
                    compatible = left.identity == right->identity
                        && left.preserve_packed == right->preserve_packed
                        && left.preserve_strings == right->preserve_strings
                        && left.preserve_containers
                            == right->preserve_containers;
                } else if constexpr (
                    std::is_same_v<Type, CallableFramePush>) {
                    compatible = left.identity == right->identity
                        && left.packed == right->packed
                        && left.strings == right->strings
                        && left.containers == right->containers
                        && left.native_isolated == right->native_isolated;
                } else if constexpr (std::is_same_v<Type, Call>) {
                    compatible = left.target == right->target
                        && left.return_target == right->return_target
                        && left.stack.pointer == right->stack.pointer
                        && left.stack.entries == right->stack.entries
                        && left.stack.capacity == right->stack.capacity;
                } else if constexpr (std::is_same_v<Type, Jump>) {
                    compatible = left.target == right->target;
                } else if constexpr (std::is_same_v<Type, Assert>) {
                    compatible = left.condition == right->condition
                        && left.severity == right->severity;
                    if (left.message != right->message
                        || left.source != right->source) {
                        operation_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                Operation { *right } });
                    }
                } else if constexpr (std::is_same_v<Type, Reduction>) {
                    compatible = left.operation == right->operation
                        && left.destination == right->destination
                        && left.source == right->source;
                } else if constexpr (std::is_same_v<Type, Shift>) {
                    compatible = left.operation == right->operation
                        && left.destination == right->destination
                        && left.value == right->value
                        && left.amount == right->amount
                        && left.signed_amount == right->signed_amount;
                } else if constexpr (std::is_same_v<Type, Concatenate>) {
                    compatible = left.destination == right->destination
                        && left.operands == right->operands
                        && left.width == right->width;
                } else if constexpr (
                    std::is_same_v<Type, ConditionalSelect>) {
                    compatible = left.destination == right->destination
                        && left.condition == right->condition
                        && left.when_true == right->when_true
                        && left.when_false == right->when_false;
                } else if constexpr (std::is_same_v<Type, Branch>) {
                    compatible = left.condition == right->condition
                        && left.when_true == right->when_true
                        && left.when_false == right->when_false
                        && left.unknown_policy == right->unknown_policy;
                } else if constexpr (std::is_same_v<Type, Insert>) {
                    compatible = left.destination == right->destination
                        && left.target == right->target
                        && left.source == right->source
                        && left.offset == right->offset;
                } else if constexpr (std::is_same_v<Type, Return>) {
                    compatible = left.stack.pointer == right->stack.pointer
                        && left.stack.entries == right->stack.entries
                        && left.stack.capacity == right->stack.capacity;
                } else if constexpr (std::is_same_v<Type, Extract>) {
                    compatible = left.destination == right->destination
                        && left.source == right->source
                        && left.offset == right->offset
                        && left.width == right->width;
                } else if constexpr (
                    std::is_same_v<Type, CodeCoverageHit>) {
                    compatible = left.point == right->point
                        && left.metric == right->metric;
                    const auto right_counter
                        = candidate.operations.code_coverage_counter(
                            index, right->counter);
                    if (compatible && left.counter != right_counter) {
                        coverage_hit_overrides.push_back(
                            { static_cast<InstructionIndex>(index),
                                right_counter });
                    }
                } else {
                    // Never share an operation whose instance-dependent
                    // fields have not been audited explicitly.
                    compatible = false;
                }
            },
            representative.operations[index]);
        if (!compatible) {
            return false;
        }
    }

    if (recycled_operations != nullptr
        && candidate.operations.storage_
        && candidate.operations.storage_.unique()) {
        *recycled_operations
            = std::move(*candidate.operations.storage_);
    }
    candidate.operations.storage_ = representative.operations.storage_;
    candidate.operations.signal_remap_.clear();
    candidate.operations.debug_overrides_ = std::move(debug_overrides);
    candidate.operations.assert_overrides_ = std::move(assert_overrides);
    candidate.operations.container_object_overrides_
        = std::move(container_object_overrides);
    candidate.operations.operation_overrides_
        = std::move(operation_overrides);
    candidate.operations.coverage_hit_overrides_
        = std::move(coverage_hit_overrides);
    candidate.operations.debug_scope_overrides_
        = std::move(debug_scope_overrides);
    candidate.operations.operation_override_filter_ = 0;
    for (const auto& override : candidate.operations.operation_overrides_) {
        candidate.operations.operation_override_filter_
            |= UINT64_C(1) << (override.instruction & 63U);
    }
    if (representative.expression_profiles
        == candidate.expression_profiles) {
        candidate.expression_profiles.share_from(
            representative.expression_profiles);
    }
    return true;
}

bool process_operations_shareable(const Process& process)
{
    return std::ranges::all_of(
        process.operations,
        [](const Operation& operation) {
            bool shareable = false;
            visit_operation(
                [&](const auto& value) {
                    using Type = std::decay_t<decltype(value)>;
                    shareable = std::is_same_v<Type, DebugPoint>
                        || std::is_same_v<Type, ReadSignal>
                        || std::is_same_v<Type, WriteBlocking>
                        || std::is_same_v<Type, WriteUpdate>
                        || std::is_same_v<Type, WriteBlockingSlice>
                        || std::is_same_v<Type, WriteUpdateSlice>
                        || std::is_same_v<
                            Type, WriteUpdateDynamicPartSlice>
                        || std::is_same_v<Type, CopyRegister>
                        || std::is_same_v<Type, IntegerCheck>
                        || std::is_same_v<Type, LoadConstant>
                        || std::is_same_v<Type, DynamicInsert>
                        || std::is_same_v<Type, DynamicPartSelect>
                        || std::is_same_v<Type, IntegerBinary>
                        || std::is_same_v<Type, DynamicExtract>
                        || std::is_same_v<Type, ReadContainerObject>
                        || std::is_same_v<Type, ContainerRead>
                        || std::is_same_v<Type, WriteProjected>
                        || std::is_same_v<Type, WriteProjectedSlice>
                        || std::is_same_v<Type, WaitSensitivity>
                        || std::is_same_v<Type, CallableFramePop>
                        || std::is_same_v<Type, CallableFramePush>
                        || std::is_same_v<Type, Call>
                        || std::is_same_v<Type, Jump>
                        || std::is_same_v<Type, Binary>
                        || std::is_same_v<Type, Assert>
                        || std::is_same_v<Type, UnaryNot>
                        || std::is_same_v<Type, LogicalNot>
                        || std::is_same_v<Type, LogicalBinary>
                        || std::is_same_v<Type, Reduction>
                        || std::is_same_v<Type, Shift>
                        || std::is_same_v<Type, Concatenate>
                        || std::is_same_v<Type, ConditionalSelect>
                        || std::is_same_v<Type, Branch>
                        || std::is_same_v<Type, Insert>
                        || std::is_same_v<Type, Return>
                        || std::is_same_v<Type, Extract>
                        || std::is_same_v<Type, CodeCoverageHit>;
                },
                operation);
            return shareable;
        });
}

std::optional<SimulationTick> transition_delay(
    const PackedLogic4& current,
    const PackedLogic4& next,
    const TransitionDelays& delays) {
  if (current.width() == 0 || current.width() != next.width()) {
    throw std::invalid_argument(
        "transition-delay values must have the same non-zero width");
  }
  std::optional<SimulationTick> selected;
  const auto consider =
      [&](const SimulationTick candidate) {
        if (!selected || candidate < *selected) {
          selected = candidate;
        }
      };
  for (std::size_t bit = 0; bit < current.width(); ++bit) {
    if (current.get(bit) == next.get(bit)) {
      continue;
    }
    switch (next.get(bit)) {
      case Logic4::zero:
        consider(delays.fall);
        break;
      case Logic4::one:
        consider(delays.rise);
        break;
      case Logic4::z:
        consider(delays.turnoff);
        break;
      case Logic4::x:
        consider(std::min(
            {delays.rise, delays.fall, delays.turnoff}));
        break;
    }
  }
  return selected;
}

InterpreterError::InterpreterError(ProcessId process,
                                   InstructionIndex instruction,
                                   std::string message)
    : std::runtime_error(error_text(process, instruction, message)),
      process_(process), instruction_(instruction) {}

AssertionError::AssertionError(ProcessId process,
                               InstructionIndex instruction,
                               std::string message,
                               AssertionSeverity severity,
                               SourceLocation source,
                               const bool reported)
    : InterpreterError(
          process, instruction,
          message.empty() ? "assertion failed" : std::move(message)),
      severity_(severity), source_(std::move(source)),
      reported_(reported) {}


} // namespace fsim::runtime::simir
