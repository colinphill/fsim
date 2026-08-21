// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal implementation fragment included by simir.hpp after all concrete
// operation alternatives have been defined.

template <typename... Alternatives>
struct OperationGroup {
    using Storage = std::variant<Alternatives...>;

    Storage storage;

    OperationGroup() = default;

    template <typename Alternative>
        requires(
            std::is_same_v<std::remove_cvref_t<Alternative>, Alternatives>
            || ...)
    OperationGroup(Alternative&& operation)
        : storage(std::forward<Alternative>(operation))
    {
    }
};

using ValueOperationGroup = OperationGroup<LoadConstant, CopyRegister, ConvertToTwoState, UnaryNot, LogicalNot,
    LogicalBinary, Reduction, CountOnes, CountBits, Shift,
    Extract, DynamicExtract, DynamicPartSelect, Concatenate,
    Binary, Insert, DynamicInsert, DynamicPartInsert,
    IntegerUnary, IntegerBinary, IntegerCheck,
    ConditionalSelect, RandomValue, RandomDistribution, ScopeRandomize,
    StochasticQueueOperation>;

using SignalOperationGroup = OperationGroup<ReadSignal, SignalEvent, SignalLastValue, SignalLastEvent,
    SignalActive,
    WriteBlocking, WriteUpdate, WriteAfter,
    WriteInertial, WriteProjected, WriteProjectedWaveform,
    WriteBlockingSlice, WriteUpdateSlice, WriteAfterSlice,
    WriteInertialSlice, WriteProjectedSlice,
    WriteProjectedWaveformSlice, WriteBlockingDynamicSlice,
    WriteUpdateDynamicSlice, WriteAfterDynamicSlice,
    WriteBlockingDynamicPartSlice,
    WriteUpdateDynamicPartSlice,
    WriteAfterDynamicPartSlice, ForceSignalSlice,
    ReleaseSignalSlice, WriteInertialDynamicSlice,
    WriteProjectedDynamicSlice,
    WriteProjectedWaveformDynamicSlice, SignalLastActive,
    SignalDriving, SignalDrivingValue, ReadSimulationTime,
    VitalTimingCheck, VitalDelay,
    WriteInertialDynamicPartSlice>;

using StringOperationGroup = OperationGroup<LoadStringConstant, CopyStringRegister, ReadStringObject,
    WriteStringObject, ConcatenateStrings, CompareStrings,
    StringLength, StringIndex, StringReplaceCodePoint,
    StringMethod, PlusArgSelect, SystemCommand, VcdControl,
    CoverageDatabaseControl>;

using ContainerOperationGroup = OperationGroup<SystemVerilogScalarBinary, SystemVerilogMath, ResizeContainer,
    CopyContainerRegister,
    ConditionalContainerSelect, CompareContainers,
    ReadContainerObject, WriteContainerObject, ContainerSize,
    ContainerReduction, OrderContainer, LocateContainer,
    ContainerRead, ContainerWrite,
    WriteContainerObjectElement,
    ContainerAggregateRead, ContainerAggregateWrite,
    CopyContainerAggregateElement, DeleteContainer,
    ContainerExists, TraverseContainer, LoadMemory,
    VitalMemoryDeclare,
    PushContainer, PopContainer,
    ContainerStringRead, ContainerStringWrite,
    ContainerElementRead, ContainerElementWrite, PlaEvaluate>;

using FileOperationGroup = OperationGroup<FileOpen, FileClose, FileWriteLiteral, FileWriteFormatted,
    FileWriteString, FileReadLine, FileEndOfFile,
    FileErrorStatus, FileScan, FileBinaryRead, FilePosition,
    FileFlush>;

using SchedulingOperationGroup = OperationGroup<WaitFor, WaitRegion, WaitOn, WaitPla, WaitOrder, EventTriggered, EventAlias, WaitSensitivity, WaitForever, Yield, Fork,
    ForkEnd, WaitFork, DisableFork, DisableBlock, ProcessSelf,
    ProcessStatusQuery, ProcessCompleted, ProcessAwait,
    ProcessKill, ProcessSuspend, ProcessResume,
    ProcessGetRandState, ProcessSetRandState, ProcessSrandom,
    MailboxCreate, MailboxPut, MailboxGet,
    MailboxNum, SemaphoreCreate, SemaphoreGet, SemaphorePut>;

using ControlOperationGroup = OperationGroup<Jump, Call, Return,
    CallableFramePush, CallableFramePop, Branch, DebugPoint, Assert, Report,
    Pause, Stop, Halt>;

using OutputOperationGroup = OperationGroup<Display, FormatDisplay, StringDisplay, StringReport,
    TimeDisplay, MonitorInstall, MonitorControl, TimeFormatControl,
    CoverageSample, CoverageQuery>;

using ClassOperationGroup = OperationGroup<
    ClassAllocate, ClassPropertyRead, ClassPropertyWrite, ClassMethodCall,
    ClassStaticPropertyRead, ClassStaticPropertyWrite,
    ClassStaticMethodCall>;

template <typename Alternative, typename Variant>
struct VariantContains;

template <typename Alternative, typename... Alternatives>
struct VariantContains<Alternative, std::variant<Alternatives...>>
    : std::bool_constant<
          (std::is_same_v<Alternative, Alternatives> || ...)> { };

template <typename Alternative, typename Group>
inline constexpr bool operation_group_contains_v = VariantContains<Alternative, typename Group::Storage>::value;

template <typename Alternative>
inline constexpr bool is_operation_alternative_v = operation_group_contains_v<Alternative, ValueOperationGroup>
    || operation_group_contains_v<Alternative, SignalOperationGroup>
    || operation_group_contains_v<Alternative, StringOperationGroup>
    || operation_group_contains_v<Alternative, ContainerOperationGroup>
    || operation_group_contains_v<Alternative, FileOperationGroup>
    || operation_group_contains_v<Alternative, SchedulingOperationGroup>
    || operation_group_contains_v<Alternative, ControlOperationGroup>
    || operation_group_contains_v<Alternative, OutputOperationGroup>
    || operation_group_contains_v<Alternative, ClassOperationGroup>;

template <typename Alternative>
struct OperationGroupSelector {
    using Type = std::remove_cvref_t<Alternative>;
    static_assert(
        is_operation_alternative_v<Type>,
        "unsupported SimIR operation alternative");
    using Group = std::conditional_t<
        operation_group_contains_v<Type, ValueOperationGroup>,
        ValueOperationGroup,
        std::conditional_t<
            operation_group_contains_v<Type, SignalOperationGroup>,
            SignalOperationGroup,
            std::conditional_t<
                operation_group_contains_v<Type, StringOperationGroup>,
                StringOperationGroup,
                std::conditional_t<
                    operation_group_contains_v<Type, ContainerOperationGroup>,
                    ContainerOperationGroup,
                    std::conditional_t<
                        operation_group_contains_v<Type, FileOperationGroup>,
                        FileOperationGroup,
                        std::conditional_t<
                            operation_group_contains_v<
                                Type, SchedulingOperationGroup>,
                            SchedulingOperationGroup,
                            std::conditional_t<
                                operation_group_contains_v<
                                    Type, ControlOperationGroup>,
                                ControlOperationGroup,
                                std::conditional_t<
                                    operation_group_contains_v<
                                        Type, OutputOperationGroup>,
                                    OutputOperationGroup,
                                    ClassOperationGroup>>>>>>>>;
};

template <typename Alternative>
using OperationGroupFor =
    typename OperationGroupSelector<Alternative>::Group;

template <typename Group>
inline constexpr bool boxed_operation_group_v
    = std::is_same_v<Group, SignalOperationGroup>
    || std::is_same_v<Group, StringOperationGroup>
    || std::is_same_v<Group, ContainerOperationGroup>
    || std::is_same_v<Group, FileOperationGroup>
    || std::is_same_v<Group, OutputOperationGroup>
    || std::is_same_v<Group, ClassOperationGroup>;

template <typename Group>
struct BoxedOperationGroup {
    using boxed_group_type = Group;
    std::unique_ptr<Group> value;

    BoxedOperationGroup()
        : value(std::make_unique<Group>())
    {
    }

    template <typename Alternative>
    explicit BoxedOperationGroup(Alternative&& operation)
        : value(std::make_unique<Group>(
              std::forward<Alternative>(operation)))
    {
    }

    BoxedOperationGroup(const BoxedOperationGroup& other)
        : value(std::make_unique<Group>(*other.value))
    {
    }

    BoxedOperationGroup(BoxedOperationGroup&&) noexcept = default;

    BoxedOperationGroup& operator=(const BoxedOperationGroup& other)
    {
        if (this != &other) {
            *value = *other.value;
        }
        return *this;
    }

    BoxedOperationGroup& operator=(BoxedOperationGroup&&) noexcept = default;
};

template <typename Group>
using OperationStorageFor = std::conditional_t<
    boxed_operation_group_v<Group>, BoxedOperationGroup<Group>, Group>;

struct Operation {
    using Storage = std::variant<
        OperationStorageFor<ValueOperationGroup>,
        OperationStorageFor<SignalOperationGroup>,
        OperationStorageFor<StringOperationGroup>,
        OperationStorageFor<ContainerOperationGroup>,
        OperationStorageFor<FileOperationGroup>,
        OperationStorageFor<SchedulingOperationGroup>,
        OperationStorageFor<ControlOperationGroup>,
        OperationStorageFor<OutputOperationGroup>,
        OperationStorageFor<ClassOperationGroup>>;

    Storage storage;

    Operation() = default;
    Operation(const Operation&) = default;
    Operation(Operation&&) noexcept = default;
    Operation& operator=(const Operation&) = default;
    Operation& operator=(Operation&&) noexcept = default;

    template <typename Alternative>
        requires is_operation_alternative_v<
            std::remove_cvref_t<Alternative>>
    Operation(Alternative&& operation)
        : storage(
              std::in_place_type<OperationStorageFor<
                  OperationGroupFor<Alternative>>>,
              std::forward<Alternative>(operation))
    {
    }

    template <typename Alternative>
        requires is_operation_alternative_v<
            std::remove_cvref_t<Alternative>>
    Operation& operator=(Alternative&& operation)
    {
        using Group = OperationGroupFor<Alternative>;
        using Stored = OperationStorageFor<Group>;
        if (auto* stored = std::get_if<Stored>(&storage)) {
            auto* group = [&]() -> Group* {
                if constexpr (boxed_operation_group_v<Group>) {
                    return stored->value.get();
                } else {
                    return stored;
                }
            }();
            group->storage = std::forward<Alternative>(operation);
        } else {
            storage.emplace<Stored>(
                std::forward<Alternative>(operation));
        }
        return *this;
    }
};

template <typename Group>
[[nodiscard]] Group* operation_group_if(Operation* operation) noexcept
{
    if (operation == nullptr) {
        return nullptr;
    }
    using Stored = OperationStorageFor<Group>;
    auto* stored = std::get_if<Stored>(&operation->storage);
    if constexpr (boxed_operation_group_v<Group>) {
        return stored == nullptr ? nullptr : stored->value.get();
    } else {
        return stored;
    }
}

template <typename Group>
[[nodiscard]] const Group* operation_group_if(
    const Operation* operation) noexcept
{
    return operation_group_if<Group>(const_cast<Operation*>(operation));
}

template <typename Group>
Group& operation_emplace_group(Operation& operation)
{
    using Stored = OperationStorageFor<Group>;
    auto& stored = operation.storage.template emplace<Stored>();
    if constexpr (boxed_operation_group_v<Group>) {
        return *stored.value;
    } else {
        return stored;
    }
}

[[nodiscard]] inline std::size_t operation_group_index(
    const Operation& operation) noexcept
{
    return operation.storage.index();
}

[[nodiscard]] inline std::size_t operation_alternative_index(
    const Operation& operation) noexcept
{
    return std::visit(
        [](const auto& stored) {
            if constexpr (requires { typename std::decay_t<
                                          decltype(stored)>::boxed_group_type; }) {
                return stored.value->storage.index();
            } else {
                return stored.storage.index();
            }
        },
        operation.storage);
}

static_assert(
    std::variant_size_v<ValueOperationGroup::Storage>
        + std::variant_size_v<SignalOperationGroup::Storage>
        + std::variant_size_v<StringOperationGroup::Storage>
        + std::variant_size_v<ContainerOperationGroup::Storage>
        + std::variant_size_v<FileOperationGroup::Storage>
        + std::variant_size_v<SchedulingOperationGroup::Storage>
        + std::variant_size_v<ControlOperationGroup::Storage>
        + std::variant_size_v<OutputOperationGroup::Storage>
        + std::variant_size_v<ClassOperationGroup::Storage>
    == 178);

template <typename Alternative>
[[nodiscard]] Alternative* operation_get_if(Operation* operation) noexcept
{
    if (operation == nullptr) {
        return nullptr;
    }
    using Group = OperationGroupFor<Alternative>;
    auto* group = operation_group_if<Group>(operation);
    return group == nullptr
        ? nullptr
        : std::get_if<Alternative>(&group->storage);
}

template <typename Alternative>
[[nodiscard]] const Alternative* operation_get_if(
    const Operation* operation) noexcept
{
    if (operation == nullptr) {
        return nullptr;
    }
    using Group = OperationGroupFor<Alternative>;
    const auto* group = operation_group_if<Group>(operation);
    return group == nullptr
        ? nullptr
        : std::get_if<Alternative>(&group->storage);
}

template <typename Alternative>
[[nodiscard]] Alternative& operation_get(Operation& operation)
{
    using Group = OperationGroupFor<Alternative>;
    return std::get<Alternative>(operation_group_if<Group>(&operation)->storage);
}

template <typename Alternative>
[[nodiscard]] const Alternative& operation_get(
    const Operation& operation)
{
    using Group = OperationGroupFor<Alternative>;
    return std::get<Alternative>(operation_group_if<Group>(&operation)->storage);
}

template <typename Alternative>
[[nodiscard]] bool operation_holds(
    const Operation& operation) noexcept
{
    return operation_get_if<Alternative>(&operation) != nullptr;
}

template <typename Visitor, typename OperationType>
decltype(auto) visit_operation(
    Visitor&& visitor, OperationType&& operation)
{
    return std::visit(
        [&visitor](auto&& stored) -> decltype(auto) {
            auto&& group = [&]() -> decltype(auto) {
                using Stored = std::remove_reference_t<decltype(stored)>;
                if constexpr (requires {
                                  typename std::remove_cv_t<
                                      Stored>::boxed_group_type;
                              }) {
                    if constexpr (std::is_const_v<Stored>) {
                        return std::as_const(*stored.value);
                    } else {
                        return *stored.value;
                    }
                } else {
                    return std::forward<decltype(stored)>(stored);
                }
            }();
            return std::visit(
                visitor,
                std::forward<decltype(group)>(group).storage);
        },
        std::forward<OperationType>(operation).storage);
}

/// Copy-on-write SimIR program storage.
///
/// Elaborated instances commonly contain the same executable program with
/// only hierarchy-local signal and source metadata substitutions.  Keeping
/// the immutable body shared avoids retaining one full Operation vector per
/// instance.  Mutating users retain ordinary vector semantics: the first
/// non-const access materializes the instance-specific program.
struct Process;
struct Signal;

class OperationList {
public:
    using value_type = Operation;
    using Storage = std::vector<Operation>;
    using iterator = Storage::iterator;
    using size_type = Storage::size_type;
    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using iterator_concept = std::random_access_iterator_tag;
        using value_type = Operation;
        using difference_type = std::ptrdiff_t;
        using pointer = const Operation*;
        using reference = const Operation&;

        const_iterator() = default;
        reference operator*() const { return (*owner_)[index_]; }
        pointer operator->() const { return &**this; }
        reference operator[](const difference_type offset) const
        {
            return *(*this + offset);
        }
        const_iterator& operator++() { ++index_; return *this; }
        const_iterator operator++(int) { auto copy = *this; ++*this; return copy; }
        const_iterator& operator--() { --index_; return *this; }
        const_iterator operator--(int) { auto copy = *this; --*this; return copy; }
        const_iterator& operator+=(const difference_type offset)
        {
            index_ = static_cast<size_type>(
                static_cast<difference_type>(index_) + offset);
            return *this;
        }
        const_iterator& operator-=(const difference_type offset)
        {
            return *this += -offset;
        }
        friend const_iterator operator+(
            const_iterator iterator, const difference_type offset)
        {
            iterator += offset;
            return iterator;
        }
        friend const_iterator operator+(
            const difference_type offset, const_iterator iterator)
        {
            return iterator + offset;
        }
        friend const_iterator operator-(
            const_iterator iterator, const difference_type offset)
        {
            iterator -= offset;
            return iterator;
        }
        friend difference_type operator-(
            const const_iterator left, const const_iterator right)
        {
            return static_cast<difference_type>(left.index_)
                - static_cast<difference_type>(right.index_);
        }
        friend bool operator==(const const_iterator&, const const_iterator&)
            = default;
        friend auto operator<=>(
            const const_iterator& left, const const_iterator& right)
        {
            return left.index_ <=> right.index_;
        }

    private:
        const_iterator(const OperationList* owner, const size_type index)
            : owner_(owner), index_(index) { }
        const OperationList* owner_ { };
        size_type index_ { };
        friend class OperationList;
    };

    OperationList();
    OperationList(std::initializer_list<Operation> operations);
    OperationList(const Storage& operations);
    OperationList(Storage&& operations);
    OperationList(const OperationList&) noexcept = default;
    OperationList(OperationList&&) noexcept = default;
    OperationList& operator=(const OperationList&) noexcept = default;
    OperationList& operator=(OperationList&&) noexcept = default;
    OperationList& operator=(std::initializer_list<Operation> operations);
    OperationList& operator=(const Storage& operations);
    OperationList& operator=(Storage&& operations);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_type size() const noexcept;
    [[nodiscard]] size_type capacity() const noexcept;
    [[nodiscard]] size_type max_size() const noexcept;

    [[nodiscard]] const Operation& operator[](size_type index) const noexcept;
    [[nodiscard]] Operation& operator[](size_type index);
    [[nodiscard]] const Operation& at(size_type index) const;
    [[nodiscard]] Operation& at(size_type index);
    [[nodiscard]] const Operation& front() const;
    [[nodiscard]] Operation& front();
    [[nodiscard]] const Operation& back() const;
    [[nodiscard]] Operation& back();
    [[nodiscard]] const Operation* data() const noexcept;
    [[nodiscard]] Operation* data();

    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;
    [[nodiscard]] const_iterator cbegin() const noexcept;
    [[nodiscard]] const_iterator cend() const noexcept;
    [[nodiscard]] iterator begin();
    [[nodiscard]] iterator end();

    void clear();
    void reserve(size_type capacity);
    void resize(size_type size);
    void push_back(const Operation& operation);
    void push_back(Operation&& operation);
    void replace(size_type index, Operation operation);
    template <typename... Arguments>
    Operation& emplace_back(Arguments&&... arguments)
    {
        auto& storage = mutable_storage();
        return storage.emplace_back(
            std::forward<Arguments>(arguments)...);
    }
    template <typename... Arguments>
    iterator insert(const_iterator position, Arguments&&... arguments)
    {
        const auto offset = static_cast<size_type>(position - cbegin());
        auto& storage = mutable_storage();
        return storage.insert(
            storage.begin() + static_cast<std::ptrdiff_t>(offset),
            std::forward<Arguments>(arguments)...);
    }
    template <typename... Arguments>
    iterator insert(iterator position, Arguments&&... arguments)
    {
        auto& storage = mutable_storage();
        return storage.insert(
            position, std::forward<Arguments>(arguments)...);
    }

    /// Resolve a signal operand from the canonical program into this
    /// instance's elaborated signal identity.
    [[nodiscard]] SignalId signal(SignalId canonical) const noexcept;

    /// Return an artifact/debugger-visible operation with all per-instance
    /// metadata and signal operands restored.
    [[nodiscard]] Operation expanded(size_type index) const;
    [[nodiscard]] const DebugPoint& debug_point(
        size_type index, const DebugPoint& canonical) const noexcept;
    [[nodiscard]] const InternedString& debug_scope(
        const InternedString& canonical) const noexcept;
    [[nodiscard]] Assert assertion(
        size_type index, const Assert& canonical) const;
    [[nodiscard]] ContainerObjectId container_object(
        size_type index, ContainerObjectId canonical) const noexcept;

    [[nodiscard]] bool shares_body_with(
        const OperationList& other) const noexcept;
    [[nodiscard]] const std::vector<std::pair<SignalId, SignalId>>&
    signal_remap() const noexcept;

private:
    struct DebugOverride {
        InstructionIndex instruction { };
        DebugPoint point;
    };
    struct AssertOverride {
        InstructionIndex instruction { };
        std::string message;
        SourceLocation source;
    };
    struct ContainerObjectOverride {
        InstructionIndex instruction { };
        ContainerObjectId object { };
    };
    struct OperationOverride {
        InstructionIndex instruction { };
        Operation operation;
    };
    struct DebugScopeOverride {
        InternedString canonical;
        InternedString instance;
    };

    [[nodiscard]] const Storage& storage() const noexcept;
    [[nodiscard]] Storage& mutable_storage();
    void reset(Storage operations);
    void apply_instance_fields(Operation& operation, size_type index) const;

    std::shared_ptr<Storage> storage_;
    std::vector<std::pair<SignalId, SignalId>> signal_remap_;
    std::vector<DebugOverride> debug_overrides_;
    std::vector<AssertOverride> assert_overrides_;
    std::vector<ContainerObjectOverride> container_object_overrides_;
    std::vector<OperationOverride> operation_overrides_;
    std::vector<DebugScopeOverride> debug_scope_overrides_;
    std::uint64_t operation_override_filter_ { };

    friend bool share_process_operations(
        const Process&, Process&, std::span<const Signal>, Storage*);
};
