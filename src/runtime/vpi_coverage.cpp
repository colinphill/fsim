// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_coverage.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

    constexpr std::uint64_t iterator_bit = UINT64_C(1) << 63U;
    constexpr std::uint64_t coverage_bit = UINT64_C(1) << 62U;
    constexpr std::uint64_t slot_mask = (UINT64_C(1) << 24U) - 1U;
    constexpr std::uint64_t epoch_mask = (UINT64_C(1) << 16U) - 1U;
    constexpr std::uint64_t owner_mask = (UINT64_C(1) << 21U) - 1U;
    std::atomic<std::uint32_t> next_coverage_owner { 1U };

    [[nodiscard]] bool valid_control(
        const SystemVerilogVpiCoverageControl control) noexcept
    {
        const auto value = static_cast<std::int32_t>(control);
        return value >= 750 && value <= 755;
    }

    [[nodiscard]] bool valid_type(
        const SystemVerilogVpiCoverageType type) noexcept
    {
        const auto value = static_cast<std::int32_t>(type);
        return value >= 760 && value <= 763;
    }

    [[nodiscard]] bool valid_property(
        const SystemVerilogVpiCoverageProperty property) noexcept
    {
        switch (property) {
        case SystemVerilogVpiCoverageProperty::AssertCoverage:
        case SystemVerilogVpiCoverageProperty::FsmStateCoverage:
        case SystemVerilogVpiCoverageProperty::StatementCoverage:
        case SystemVerilogVpiCoverageProperty::ToggleCoverage:
        case SystemVerilogVpiCoverageProperty::Covered:
        case SystemVerilogVpiCoverageProperty::CoveredMax:
        case SystemVerilogVpiCoverageProperty::CoveredCount:
        case SystemVerilogVpiCoverageProperty::AssertAttemptCovered:
        case SystemVerilogVpiCoverageProperty::AssertSuccessCovered:
        case SystemVerilogVpiCoverageProperty::AssertFailureCovered:
        case SystemVerilogVpiCoverageProperty::AssertVacuousSuccessCovered:
        case SystemVerilogVpiCoverageProperty::AssertDisableCovered:
        case SystemVerilogVpiCoverageProperty::AssertKillCovered:
            return true;
        }
        return false;
    }

    [[nodiscard]] bool valid_relation(
        const SystemVerilogVpiCoverageRelation relation) noexcept
    {
        return relation == SystemVerilogVpiCoverageRelation::Fsm
            || relation == SystemVerilogVpiCoverageRelation::FsmHandle
            || relation == SystemVerilogVpiCoverageRelation::FsmStates
            || relation
                == SystemVerilogVpiCoverageRelation::FsmStateExpression;
    }

    [[nodiscard]] bool statistics_valid(
        const SystemVerilogVpiCoverageStatistics& statistics) noexcept
    {
        return statistics.covered_items <= statistics.coverable_items
            && statistics.covered_items <= statistics.covered_count;
    }

    [[nodiscard]] bool has_type(
        const std::span<const SystemVerilogVpiCoverageType> types,
        const SystemVerilogVpiCoverageType requested) noexcept
    {
        return std::ranges::find(types, requested) != types.end();
    }

    [[nodiscard]] SystemVerilogVpiCoverageError object_error(
        const SystemVerilogVpiObjectError error) noexcept
    {
        return error == SystemVerilogVpiObjectError::CrossSimulation
            ? SystemVerilogVpiCoverageError::CrossSimulation
            : SystemVerilogVpiCoverageError::InvalidObject;
    }

    [[nodiscard]] bool target_kind_accepts(
        const SystemVerilogVpiObjectKind kind,
        const SystemVerilogVpiCoverageType type) noexcept
    {
        const bool scope = kind == SystemVerilogVpiObjectKind::Root
            || kind == SystemVerilogVpiObjectKind::Module
            || kind == SystemVerilogVpiObjectKind::Interface
            || kind == SystemVerilogVpiObjectKind::Program
            || kind == SystemVerilogVpiObjectKind::GenerateScope;
        if (type == SystemVerilogVpiCoverageType::Assertion) {
            return scope || kind == SystemVerilogVpiObjectKind::Assertion;
        }
        if (type == SystemVerilogVpiCoverageType::Toggle) {
            return scope || kind == SystemVerilogVpiObjectKind::Port
                || kind == SystemVerilogVpiObjectKind::Net
                || kind == SystemVerilogVpiObjectKind::Variable;
        }
        return scope || kind == SystemVerilogVpiObjectKind::Process;
    }

    [[nodiscard]] std::optional<std::int32_t> bounded_property(
        const std::uint64_t value) noexcept
    {
        if (value > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int32_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(value);
    }

    [[nodiscard]] std::optional<std::string> state_identity(
        const SystemVerilogVpiStoredValue& state)
    {
        if (state.strength) {
            return std::nullopt;
        }
        if (const auto* value = std::get_if<PackedBit2>(&state.payload)) {
            return value->empty()
                ? std::nullopt
                : std::optional { "bit2:" + value->to_msb_string() };
        }
        if (const auto* value = std::get_if<PackedLogic4>(&state.payload)) {
            return value->empty()
                ? std::nullopt
                : std::optional { "logic4:" + value->to_msb_string() };
        }
        if (const auto* value = std::get_if<PackedLogic9>(&state.payload)) {
            return value->empty()
                ? std::nullopt
                : std::optional { "logic9:" + value->to_msb_string() };
        }
        if (const auto* value = std::get_if<std::uint64_t>(&state.payload)) {
            return "integer:" + std::to_string(*value);
        }
        return std::nullopt;
    }

} // namespace

struct SystemVerilogVpiCoverageService::Impl {
    struct TargetRecord {
        std::vector<SystemVerilogVpiCoverageType> types;
    };
    struct CoverageRecord {
        SystemVerilogVpiCoverageObjectKind kind {
            SystemVerilogVpiCoverageObjectKind::Fsm
        };
        std::uint16_t epoch { };
        fsim_vpi_handle_v1 parent { };
        fsim_vpi_handle_v1 expression { };
        std::string name;
        std::optional<SystemVerilogVpiStoredValue> value;
        std::vector<fsim_vpi_handle_v1> states;
    };
    struct IteratorRecord {
        std::uint16_t epoch { };
        bool live { };
        std::size_t cursor { };
        std::vector<fsim_vpi_handle_v1> objects;
    };

    std::uint32_t owner { };
    SystemVerilogVpiObjectRegistry* objects { };
    SystemVerilogVpiCoverageStatisticsProvider statistics;
    SystemVerilogVpiCoverageControlHook control;
    SystemVerilogVpiCoverageLimits limits;
    mutable std::mutex mutex;
    std::map<fsim_vpi_handle_v1, TargetRecord> targets;
    std::vector<CoverageRecord> records;
    std::map<fsim_vpi_handle_v1, fsim_vpi_handle_v1> expression_fsms;
    std::size_t fsms { };
    std::size_t states { };
    std::vector<IteratorRecord> iterators;
    std::vector<std::uint32_t> free_iterators;

    [[nodiscard]] fsim_vpi_handle_v1 encode_record(
        const std::uint32_t slot, const std::uint16_t epoch) const noexcept
    {
        return coverage_bit
            | (static_cast<std::uint64_t>(owner) << 40U)
            | (static_cast<std::uint64_t>(epoch) << 24U)
            | (static_cast<std::uint64_t>(slot) + 1U);
    }

    [[nodiscard]] fsim_vpi_handle_v1 encode_iterator(
        const std::uint32_t slot, const std::uint16_t epoch) const noexcept
    {
        return iterator_bit | encode_record(slot, epoch);
    }

    [[nodiscard]] SystemVerilogVpiCoverageError resolve_record(
        const fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept
    {
        if (handle == 0U || (handle & coverage_bit) == 0U
            || (handle & iterator_bit) != 0U
            || (handle & slot_mask) == 0U) {
            return SystemVerilogVpiCoverageError::InvalidHandle;
        }
        if (((handle >> 40U) & owner_mask) != owner) {
            return SystemVerilogVpiCoverageError::CrossSimulation;
        }
        const auto decoded = static_cast<std::uint32_t>(
            (handle & slot_mask) - 1U);
        if (decoded >= records.size()) {
            return SystemVerilogVpiCoverageError::InvalidHandle;
        }
        const auto epoch = static_cast<std::uint16_t>(
            (handle >> 24U) & epoch_mask);
        if (records[decoded].epoch != epoch) {
            return SystemVerilogVpiCoverageError::StaleHandle;
        }
        slot = decoded;
        return SystemVerilogVpiCoverageError::None;
    }

    [[nodiscard]] SystemVerilogVpiCoverageError resolve_iterator(
        const fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept
    {
        if (handle == 0U || (handle & coverage_bit) == 0U
            || (handle & iterator_bit) == 0U
            || (handle & slot_mask) == 0U) {
            return SystemVerilogVpiCoverageError::InvalidHandle;
        }
        if (((handle >> 40U) & owner_mask) != owner) {
            return SystemVerilogVpiCoverageError::CrossSimulation;
        }
        const auto decoded = static_cast<std::uint32_t>(
            (handle & slot_mask) - 1U);
        if (decoded >= iterators.size()) {
            return SystemVerilogVpiCoverageError::InvalidHandle;
        }
        const auto epoch = static_cast<std::uint16_t>(
            (handle >> 24U) & epoch_mask);
        if (iterators[decoded].epoch != epoch) {
            return SystemVerilogVpiCoverageError::StaleHandle;
        }
        if (!iterators[decoded].live) {
            return SystemVerilogVpiCoverageError::ReleasedHandle;
        }
        slot = decoded;
        return SystemVerilogVpiCoverageError::None;
    }
};

SystemVerilogVpiCoverageService::SystemVerilogVpiCoverageService(
    SystemVerilogVpiObjectRegistry& objects,
    SystemVerilogVpiCoverageStatisticsProvider statistics,
    SystemVerilogVpiCoverageControlHook control,
    const SystemVerilogVpiCoverageLimits limits)
    : impl_(std::make_unique<Impl>())
{
    const auto owner = next_coverage_owner.fetch_add(
        1U, std::memory_order_relaxed);
    if (owner <= owner_mask) {
        impl_->owner = owner;
    }
    impl_->objects = &objects;
    impl_->statistics = std::move(statistics);
    impl_->control = std::move(control);
    impl_->limits = limits;
}

SystemVerilogVpiCoverageService::~SystemVerilogVpiCoverageService() = default;

bool SystemVerilogVpiCoverageService::valid() const noexcept
{
    return impl_ && impl_->owner != 0U && impl_->objects != nullptr
        && impl_->objects->valid() && impl_->statistics && impl_->control
        && impl_->limits.maximum_targets != 0U
        && impl_->limits.maximum_fsms != 0U
        && impl_->limits.maximum_states != 0U
        && impl_->limits.maximum_iterators != 0U
        && impl_->limits.maximum_iterator_objects != 0U
        && impl_->limits.maximum_name_bytes != 0U;
}

std::uint64_t
SystemVerilogVpiCoverageService::simulation_identity() const noexcept
{
    return valid() ? impl_->objects->simulation_identity() : 0U;
}

SystemVerilogVpiCoverageError
SystemVerilogVpiCoverageService::publish_target(
    const fsim_vpi_handle_v1 object,
    const std::span<const SystemVerilogVpiCoverageType> types)
{
    if (!valid()) {
        return SystemVerilogVpiCoverageError::InvalidService;
    }
    const auto info = impl_->objects->lookup(object);
    if (!info) {
        return object_error(info.error);
    }
    if (types.empty()) {
        return SystemVerilogVpiCoverageError::InvalidCoverageType;
    }
    std::vector<SystemVerilogVpiCoverageType> canonical;
    try {
        canonical.assign(types.begin(), types.end());
        for (const auto type : canonical) {
            if (!valid_type(type)
                || !target_kind_accepts(info.value->kind, type)) {
                return SystemVerilogVpiCoverageError::InvalidCoverageType;
            }
        }
        std::ranges::sort(canonical, { }, [](const auto type) {
            return static_cast<std::int32_t>(type);
        });
        if (std::ranges::adjacent_find(canonical) != canonical.end()) {
            return SystemVerilogVpiCoverageError::InvalidCoverageType;
        }
        std::scoped_lock lock { impl_->mutex };
        if (impl_->targets.contains(object)) {
            return SystemVerilogVpiCoverageError::DuplicateObject;
        }
        if (impl_->targets.size() >= impl_->limits.maximum_targets) {
            return SystemVerilogVpiCoverageError::ResourceLimit;
        }
        impl_->targets.emplace(object, Impl::TargetRecord {
            std::move(canonical) });
    } catch (...) {
        return SystemVerilogVpiCoverageError::ResourceLimit;
    }
    return SystemVerilogVpiCoverageError::None;
}

SystemVerilogVpiCoverageHandleResult
SystemVerilogVpiCoverageService::publish_fsm(
    SystemVerilogVpiCoverageFsmDescriptor descriptor)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidService };
    }
    const auto instance = impl_->objects->lookup(descriptor.instance);
    const auto expression = impl_->objects->lookup(
        descriptor.state_expression);
    if (!instance) {
        return { { }, object_error(instance.error) };
    }
    if (!expression) {
        return { { }, object_error(expression.error) };
    }
    if (!target_kind_accepts(
            instance.value->kind,
            SystemVerilogVpiCoverageType::FsmState)
        || (expression.value->kind != SystemVerilogVpiObjectKind::Port
            && expression.value->kind != SystemVerilogVpiObjectKind::Net
            && expression.value->kind
                != SystemVerilogVpiObjectKind::Variable)) {
        return { { }, SystemVerilogVpiCoverageError::InvalidObject };
    }
    if (descriptor.name.empty()
        || descriptor.name.size() > impl_->limits.maximum_name_bytes
        || descriptor.name.find('\0') != std::string::npos
        || descriptor.states.empty()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidState };
    }
    if (descriptor.states.size() > impl_->limits.maximum_states) {
        return { { }, SystemVerilogVpiCoverageError::ResourceLimit };
    }
    if (!expression.value->type) {
        return { { }, SystemVerilogVpiCoverageError::InvalidState };
    }
    std::set<std::string_view> names;
    std::set<std::string> values;
    for (const auto& state : descriptor.states) {
        if (state.name.empty()
            || state.name.size() > impl_->limits.maximum_name_bytes
            || state.name.find('\0') != std::string::npos) {
            return { { }, SystemVerilogVpiCoverageError::InvalidState };
        }
        if (!names.insert(state.name).second) {
            return { { }, SystemVerilogVpiCoverageError::DuplicateState };
        }
        const auto identity = state_identity(state.value);
        if (!identity
            || !validate_systemverilog_vpi_stored_value(
                *expression.value->type, state.value)) {
            return { { }, SystemVerilogVpiCoverageError::InvalidState };
        }
        if (!values.insert(*identity).second) {
            return { { }, SystemVerilogVpiCoverageError::DuplicateState };
        }
    }
    try {
        std::scoped_lock lock { impl_->mutex };
        if (impl_->expression_fsms.contains(descriptor.state_expression)) {
            return { { }, SystemVerilogVpiCoverageError::DuplicateObject };
        }
        const auto additions = descriptor.states.size() + 1U;
        if (impl_->fsms >= impl_->limits.maximum_fsms
            || impl_->states > impl_->limits.maximum_states
                - descriptor.states.size()
            || additions > slot_mask
            || impl_->records.size() > slot_mask - additions) {
            return { { }, SystemVerilogVpiCoverageError::ResourceLimit };
        }
        const auto fsm_slot = static_cast<std::uint32_t>(
            impl_->records.size());
        const auto fsm_handle = impl_->encode_record(fsm_slot, 0U);
        Impl::CoverageRecord fsm;
        fsm.parent = descriptor.instance;
        fsm.expression = descriptor.state_expression;
        fsm.name = std::move(descriptor.name);
        fsm.states.reserve(descriptor.states.size());
        std::vector<Impl::CoverageRecord> states;
        states.reserve(descriptor.states.size());
        for (auto& state : descriptor.states) {
            const auto slot = static_cast<std::uint32_t>(
                impl_->records.size() + states.size() + 1U);
            const auto handle = impl_->encode_record(slot, 0U);
            fsm.states.push_back(handle);
            Impl::CoverageRecord record;
            record.kind = SystemVerilogVpiCoverageObjectKind::FsmState;
            record.parent = fsm_handle;
            record.name = std::move(state.name);
            record.value = std::move(state.value);
            states.push_back(std::move(record));
        }
        const auto previous_size = impl_->records.size();
        impl_->records.reserve(
            impl_->records.size() + states.size() + 1U);
        try {
            impl_->records.push_back(std::move(fsm));
            impl_->records.insert(impl_->records.end(),
                std::make_move_iterator(states.begin()),
                std::make_move_iterator(states.end()));
            impl_->expression_fsms.emplace(
                descriptor.state_expression, fsm_handle);
        } catch (...) {
            impl_->records.resize(previous_size);
            impl_->expression_fsms.erase(descriptor.state_expression);
            throw;
        }
        ++impl_->fsms;
        impl_->states += descriptor.states.size();
        return { fsm_handle, { } };
    } catch (...) {
        return { { }, SystemVerilogVpiCoverageError::ResourceLimit };
    }
}

SystemVerilogVpiCoverageControlResult
SystemVerilogVpiCoverageService::control(
    const SystemVerilogVpiCoverageControlRequest& request) const
{
    if (!valid()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidService };
    }
    if (!valid_control(request.control)) {
        return { { }, SystemVerilogVpiCoverageError::InvalidControl };
    }
    if (!valid_type(request.type)) {
        return { { }, SystemVerilogVpiCoverageError::InvalidCoverageType };
    }
    const bool file_control
        = request.control == SystemVerilogVpiCoverageControl::Merge
        || request.control == SystemVerilogVpiCoverageControl::Save;
    if (file_control != !request.filename.empty()
        || file_control == request.object.has_value()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidRequest };
    }
    if (request.object) {
        const auto info = impl_->objects->lookup(*request.object);
        if (!info) {
            return { { }, object_error(info.error) };
        }
        std::scoped_lock lock { impl_->mutex };
        const auto found = impl_->targets.find(*request.object);
        if (found == impl_->targets.end()
            || !has_type(found->second.types, request.type)) {
            return { { }, SystemVerilogVpiCoverageError::NotFound };
        }
    }
    try {
        return { impl_->control(request), { } };
    } catch (...) {
        return { { }, SystemVerilogVpiCoverageError::ProviderFailure };
    }
}

SystemVerilogVpiCoveragePropertyResult
SystemVerilogVpiCoverageService::property(
    const SystemVerilogVpiCoverageProperty property,
    const fsim_vpi_handle_v1 object) const
{
    if (!valid()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidService };
    }
    if (!valid_property(property)) {
        return { { }, SystemVerilogVpiCoverageError::InvalidProperty };
    }
    bool internal { };
    bool assertion { };
    bool fsm { };
    bool target_known { };
    std::vector<SystemVerilogVpiCoverageType> target_types;
    {
        std::scoped_lock lock { impl_->mutex };
        std::uint32_t slot { };
        if ((object & coverage_bit) != 0U) {
            const auto error = impl_->resolve_record(object, slot);
            if (error != SystemVerilogVpiCoverageError::None) {
                return { { }, error };
            }
            internal = true;
            fsm = true;
        } else {
            const auto found = impl_->targets.find(object);
            if (found != impl_->targets.end()) {
                target_known = true;
                target_types = found->second.types;
                assertion = has_type(target_types,
                    SystemVerilogVpiCoverageType::Assertion);
            }
        }
    }
    if (!internal) {
        const auto info = impl_->objects->lookup(object);
        if (!info) {
            return { { }, object_error(info.error) };
        }
    }
    const auto type_property = static_cast<std::int32_t>(property) >= 760
        && static_cast<std::int32_t>(property) <= 763;
    if (type_property) {
        const auto requested = static_cast<SystemVerilogVpiCoverageType>(
            static_cast<std::int32_t>(property));
        const bool present = fsm
            ? requested == SystemVerilogVpiCoverageType::FsmState
            : target_known && has_type(target_types, requested);
        return { present ? 1 : 0, { } };
    }
    if (!internal && !target_known) {
        return { { }, SystemVerilogVpiCoverageError::NotFound };
    }
    const bool assertion_property
        = static_cast<std::int32_t>(property) >= 770;
    if (assertion_property && !assertion) {
        return { { }, SystemVerilogVpiCoverageError::InvalidProperty };
    }
    std::optional<SystemVerilogVpiCoverageStatistics> statistics;
    try {
        statistics = impl_->statistics(object);
    } catch (...) {
        return { { }, SystemVerilogVpiCoverageError::ProviderFailure };
    }
    if (!statistics) {
        return { { }, SystemVerilogVpiCoverageError::NotFound };
    }
    if (!statistics_valid(*statistics)) {
        return { { }, SystemVerilogVpiCoverageError::InvalidStatistics };
    }
    std::uint64_t value { };
    switch (property) {
    case SystemVerilogVpiCoverageProperty::Covered:
        return { statistics->coverable_items != 0U
                    && statistics->covered_items
                        == statistics->coverable_items,
            { } };
    case SystemVerilogVpiCoverageProperty::CoveredMax:
        value = statistics->coverable_items;
        break;
    case SystemVerilogVpiCoverageProperty::CoveredCount:
        value = statistics->covered_count;
        break;
    case SystemVerilogVpiCoverageProperty::AssertAttemptCovered:
        value = statistics->assertion_attempts;
        break;
    case SystemVerilogVpiCoverageProperty::AssertSuccessCovered:
        value = statistics->assertion_successes;
        break;
    case SystemVerilogVpiCoverageProperty::AssertFailureCovered:
        value = statistics->assertion_failures;
        break;
    case SystemVerilogVpiCoverageProperty::AssertVacuousSuccessCovered:
        value = statistics->assertion_vacuous_successes;
        break;
    case SystemVerilogVpiCoverageProperty::AssertDisableCovered:
        value = statistics->assertion_disables;
        break;
    case SystemVerilogVpiCoverageProperty::AssertKillCovered:
        value = statistics->assertion_kills;
        break;
    case SystemVerilogVpiCoverageProperty::AssertCoverage:
    case SystemVerilogVpiCoverageProperty::FsmStateCoverage:
    case SystemVerilogVpiCoverageProperty::StatementCoverage:
    case SystemVerilogVpiCoverageProperty::ToggleCoverage:
        return { { }, SystemVerilogVpiCoverageError::InvalidProperty };
    }
    const auto bounded = bounded_property(value);
    return bounded
        ? SystemVerilogVpiCoveragePropertyResult { *bounded, { } }
        : SystemVerilogVpiCoveragePropertyResult {
              { }, SystemVerilogVpiCoverageError::Overflow };
}

SystemVerilogVpiCoverageHandleResult
SystemVerilogVpiCoverageService::handle(
    const SystemVerilogVpiCoverageRelation relation,
    const fsim_vpi_handle_v1 reference) const
{
    if (!valid()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidService };
    }
    if (!valid_relation(relation)
        || (relation != SystemVerilogVpiCoverageRelation::FsmHandle
            && relation
                != SystemVerilogVpiCoverageRelation::FsmStateExpression)) {
        return { { }, SystemVerilogVpiCoverageError::InvalidRelation };
    }
    if (relation == SystemVerilogVpiCoverageRelation::FsmHandle) {
        const auto info = impl_->objects->lookup(reference);
        if (!info) {
            return { { }, object_error(info.error) };
        }
        std::scoped_lock lock { impl_->mutex };
        const auto found = impl_->expression_fsms.find(reference);
        return found == impl_->expression_fsms.end()
            ? SystemVerilogVpiCoverageHandleResult {
                  { }, SystemVerilogVpiCoverageError::NotFound }
            : SystemVerilogVpiCoverageHandleResult { found->second, { } };
    }
    std::scoped_lock lock { impl_->mutex };
    std::uint32_t slot { };
    const auto error = impl_->resolve_record(reference, slot);
    if (error != SystemVerilogVpiCoverageError::None) {
        return { { }, error };
    }
    const auto& record = impl_->records[slot];
    return record.kind == SystemVerilogVpiCoverageObjectKind::Fsm
        ? SystemVerilogVpiCoverageHandleResult { record.expression, { } }
        : SystemVerilogVpiCoverageHandleResult {
              { }, SystemVerilogVpiCoverageError::InvalidObject };
}

SystemVerilogVpiCoverageHandleResult
SystemVerilogVpiCoverageService::iterate(
    const SystemVerilogVpiCoverageRelation relation,
    const fsim_vpi_handle_v1 reference)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidService };
    }
    if (relation != SystemVerilogVpiCoverageRelation::Fsm
        && relation != SystemVerilogVpiCoverageRelation::FsmStates) {
        return { { }, SystemVerilogVpiCoverageError::InvalidRelation };
    }
    std::vector<fsim_vpi_handle_v1> objects;
    if (relation == SystemVerilogVpiCoverageRelation::Fsm) {
        const auto info = impl_->objects->lookup(reference);
        if (!info) {
            return { { }, object_error(info.error) };
        }
        if (!target_kind_accepts(
                info.value->kind,
                SystemVerilogVpiCoverageType::FsmState)) {
            return { { }, SystemVerilogVpiCoverageError::InvalidObject };
        }
    }
    try {
        std::scoped_lock lock { impl_->mutex };
        if (relation == SystemVerilogVpiCoverageRelation::Fsm) {
            for (std::uint32_t slot = 0U;
                slot < impl_->records.size(); ++slot) {
                const auto& record = impl_->records[slot];
                if (record.kind == SystemVerilogVpiCoverageObjectKind::Fsm
                    && record.parent == reference) {
                    objects.push_back(
                        impl_->encode_record(slot, record.epoch));
                }
            }
        } else {
            std::uint32_t slot { };
            const auto error = impl_->resolve_record(reference, slot);
            if (error != SystemVerilogVpiCoverageError::None) {
                return { { }, error };
            }
            const auto& record = impl_->records[slot];
            if (record.kind != SystemVerilogVpiCoverageObjectKind::Fsm) {
                return { { }, SystemVerilogVpiCoverageError::InvalidObject };
            }
            objects = record.states;
        }
        if (objects.size() > impl_->limits.maximum_iterator_objects) {
            return { { }, SystemVerilogVpiCoverageError::ResourceLimit };
        }
        std::uint32_t slot { };
        bool reused { };
        while (!impl_->free_iterators.empty()) {
            slot = impl_->free_iterators.back();
            impl_->free_iterators.pop_back();
            if (impl_->iterators[slot].epoch
                != std::numeric_limits<std::uint16_t>::max()) {
                reused = true;
                break;
            }
        }
        if (reused) {
            auto& iterator = impl_->iterators[slot];
            ++iterator.epoch;
            iterator.live = true;
            iterator.cursor = 0U;
            iterator.objects = std::move(objects);
        } else {
            if (impl_->iterators.size()
                >= impl_->limits.maximum_iterators
                || impl_->iterators.size() >= slot_mask) {
                return { { }, SystemVerilogVpiCoverageError::ResourceLimit };
            }
            slot = static_cast<std::uint32_t>(impl_->iterators.size());
            impl_->iterators.push_back(
                { 0U, true, 0U, std::move(objects) });
        }
        return { impl_->encode_iterator(
                     slot, impl_->iterators[slot].epoch),
            { } };
    } catch (...) {
        return { { }, SystemVerilogVpiCoverageError::ResourceLimit };
    }
}

SystemVerilogVpiCoverageHandleResult
SystemVerilogVpiCoverageService::scan(
    const fsim_vpi_handle_v1 iterator)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiCoverageError::InvalidService };
    }
    std::scoped_lock lock { impl_->mutex };
    std::uint32_t slot { };
    const auto error = impl_->resolve_iterator(iterator, slot);
    if (error != SystemVerilogVpiCoverageError::None) {
        return { { }, error };
    }
    auto& record = impl_->iterators[slot];
    if (record.cursor == record.objects.size()) {
        return { { }, SystemVerilogVpiCoverageError::End };
    }
    return { record.objects[record.cursor++], { } };
}

SystemVerilogVpiCoverageError
SystemVerilogVpiCoverageService::release_iterator(
    const fsim_vpi_handle_v1 iterator)
{
    if (!valid()) {
        return SystemVerilogVpiCoverageError::InvalidService;
    }
    std::scoped_lock lock { impl_->mutex };
    std::uint32_t slot { };
    const auto error = impl_->resolve_iterator(iterator, slot);
    if (error != SystemVerilogVpiCoverageError::None) {
        return error;
    }
    auto& record = impl_->iterators[slot];
    record.live = false;
    record.cursor = 0U;
    record.objects.clear();
    impl_->free_iterators.push_back(slot);
    return SystemVerilogVpiCoverageError::None;
}

SystemVerilogVpiCoverageValueResult
SystemVerilogVpiCoverageService::value(
    const fsim_vpi_handle_v1 state) const
{
    if (!valid()) {
        return { std::nullopt,
            SystemVerilogVpiCoverageError::InvalidService };
    }
    std::scoped_lock lock { impl_->mutex };
    std::uint32_t slot { };
    const auto error = impl_->resolve_record(state, slot);
    if (error != SystemVerilogVpiCoverageError::None) {
        return { std::nullopt, error };
    }
    const auto& record = impl_->records[slot];
    return record.kind == SystemVerilogVpiCoverageObjectKind::FsmState
        && record.value
        ? SystemVerilogVpiCoverageValueResult { record.value, { } }
        : SystemVerilogVpiCoverageValueResult { std::nullopt,
              SystemVerilogVpiCoverageError::InvalidObject };
}

std::size_t SystemVerilogVpiCoverageService::target_count() const
{
    std::scoped_lock lock { impl_->mutex };
    return impl_->targets.size();
}

std::size_t SystemVerilogVpiCoverageService::fsm_count() const
{
    std::scoped_lock lock { impl_->mutex };
    return impl_->fsms;
}

std::size_t SystemVerilogVpiCoverageService::state_count() const
{
    std::scoped_lock lock { impl_->mutex };
    return impl_->states;
}

} // namespace fsim::runtime
