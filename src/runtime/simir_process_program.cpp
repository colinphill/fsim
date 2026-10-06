// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] bool same_debug_local(
    const DebugLocal& left, const DebugLocal& right)
{
    return left.name == right.name
        && left.type_name == right.type_name
        && left.register_id == right.register_id
        && left.width == right.width
        && left.source == right.source
        && left.integer_lower == right.integer_lower
        && left.integer_upper == right.integer_upper
        && left.value_kind == right.value_kind
        && left.enumeration_literals == right.enumeration_literals
        && left.systemverilog_scalar == right.systemverilog_scalar;
}

[[nodiscard]] bool same_debug_string_local(
    const DebugStringLocal& left, const DebugStringLocal& right)
{
    return left.name == right.name
        && left.register_id == right.register_id
        && left.source == right.source;
}

[[nodiscard]] bool same_debug_container_local(
    const DebugContainerLocal& left, const DebugContainerLocal& right)
{
    return left.name == right.name
        && left.register_id == right.register_id
        && left.type == right.type
        && left.source == right.source;
}

template<class LeftRange, class RightRange, class Equal>
[[nodiscard]] bool ranges_equal(
    const LeftRange& left, const RightRange& right, Equal equal)
{
    return std::ranges::equal(left, right, equal);
}

} // namespace

ProcessProgramTemplate::ProcessProgramTemplate(const Process& process)
    : ProcessProgramTemplate(ProcessProgramView { process })
{
}

ProcessProgramTemplate::ProcessProgramTemplate(
    const ProcessProgramView& process)
    : language_standard { process.language_standard() }
    , compatibility_profile { process.compatibility_profile() }
    , register_count { process.register_count() }
    , string_register_count { process.string_register_count() }
    , container_register_count { process.container_register_count() }
    , debug_locals { process.debug_locals() }
    , debug_string_locals { process.debug_string_locals() }
    , debug_container_locals { process.debug_container_locals() }
    , container_register_types { process.container_register_types() }
    , static_trigger_regions { process.static_trigger_regions() }
    , register_value_kinds { process.register_value_kinds() }
    , expression_profiles { process.expression_profiles() }
    , scheduling_domain { process.scheduling_domain() }
{
}

bool ProcessProgramTemplate::matches(const Process& process) const
{
    return matches(ProcessProgramView { process });
}

bool ProcessProgramTemplate::matches(
    const ProcessProgramView& process) const
{
    return language_standard == process.language_standard()
        && compatibility_profile == process.compatibility_profile()
        && register_count == process.register_count()
        && string_register_count == process.string_register_count()
        && container_register_count == process.container_register_count()
        && ranges_equal(
            debug_locals, process.debug_locals(), same_debug_local)
        && ranges_equal(debug_string_locals,
            process.debug_string_locals(), same_debug_string_local)
        && ranges_equal(debug_container_locals,
            process.debug_container_locals(), same_debug_container_local)
        && container_register_types == process.container_register_types()
        && static_trigger_regions == process.static_trigger_regions()
        && register_value_kinds == process.register_value_kinds()
        && std::ranges::equal(
            expression_profiles, process.expression_profiles())
        && scheduling_domain == process.scheduling_domain();
}

ProcessInstanceProgram::ProcessInstanceProgram(Process process)
    : id { process.id }
    , name { std::move(process.name) }
    , static_sensitivity { std::move(process.static_sensitivity) }
    , operations { std::move(process.operations) }
    , driver_regions { std::move(process.driver_regions) }
    , drive_strength { process.drive_strength }
    , switch_source { process.switch_source }
    , switch_target { process.switch_target }
    , switch_control { process.switch_control }
    , switch_source_offset { process.switch_source_offset }
    , switch_target_offset { process.switch_target_offset }
    , switch_width { process.switch_width }
    , switch_active_high { process.switch_active_high }
    , switch_bidirectional { process.switch_bidirectional }
    , switch_resistive { process.switch_resistive }
    , initialize { process.initialize }
    , observed { process.observed }
    , reactive { process.reactive }
    , program_owner { process.program_owner }
    , postponed { process.postponed }
    , final { process.final }
{
}

ProcessInstanceProgram::ProcessInstanceProgram(
    const ProcessProgramView& process)
    : id { process.id() }
    , name { process.name() }
    , static_sensitivity { process.static_sensitivity() }
    , operations { process.operations() }
    , driver_regions { process.driver_regions() }
    , drive_strength { process.drive_strength() }
    , switch_source { process.switch_source() }
    , switch_target { process.switch_target() }
    , switch_control { process.switch_control() }
    , switch_source_offset { process.switch_source_offset() }
    , switch_target_offset { process.switch_target_offset() }
    , switch_width { process.switch_width() }
    , switch_active_high { process.switch_active_high() }
    , switch_bidirectional { process.switch_bidirectional() }
    , switch_resistive { process.switch_resistive() }
    , initialize { process.initialize() }
    , observed { process.observed() }
    , reactive { process.reactive() }
    , program_owner { process.program_owner() }
    , postponed { process.postponed() }
    , final { process.final() }
{
}

ProcessProgramView::ProcessProgramView(
    const ProcessProgramTemplate& common,
    const ProcessInstanceProgram& instance,
    const ProcessStartupWriteBank* const startup_write_bank) noexcept
    : common_ { &common }
    , instance_ { &instance }
    , startup_write_bank_ { startup_write_bank }
{
}

OperationList ProcessStartupWriteBank::materialize_operations() const
{
    std::vector<Operation> operations;
    operations.reserve(operation_count);
    operations.emplace_back(entry_point);
    if (statement_point) {
        operations.emplace_back(*statement_point);
    }
    operations.emplace_back(LoadConstant { 0U, value });
    if (constant_copy) {
        operations.emplace_back(*constant_copy);
    }
    const auto write_source
        = constant_copy ? constant_copy->destination : 0U;
    if (slice) {
        operations.emplace_back(WriteUpdateSlice {
            signal, write_source, offset, update_domain
        });
    } else {
        operations.emplace_back(WriteUpdate {
            signal, write_source, update_domain
        });
    }
    operations.emplace_back(Halt { });
    return OperationList { std::move(operations) };
}

ProcessStartupWriteBodyCache::ProcessStartupWriteBodyCache(
    const OperationList& source)
    : registration_identity_ { source }
{
}

std::shared_ptr<const OperationList>
ProcessStartupWriteBodyCache::cached_operations() const noexcept
{
    return materialized_operations_.load(std::memory_order_acquire);
}

std::shared_ptr<const OperationList>
ProcessStartupWriteBodyCache::publish_or_get(
    std::shared_ptr<const OperationList> candidate)
{
    auto expected = materialized_operations_.load(
        std::memory_order_acquire);
    if (expected) {
        return expected;
    }
    if (materialized_operations_.compare_exchange_strong(
            expected, candidate,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return candidate;
    }
    return expected;
}

void ProcessStartupWriteBodyCache::release_registration_identity() noexcept
{
    registration_identity_.reset();
}

std::shared_ptr<const OperationList>
ProcessStartupWriteBank::cached_operations() const noexcept
{
    return materialized_operations.load(std::memory_order_acquire);
}

const OperationList& ProcessStartupWriteBank::operations() const
{
    auto materialized = materialized_operations.load(
        std::memory_order_acquire);
    if (materialized) {
        return *materialized;
    }

    std::shared_ptr<const OperationList> candidate;
    if (body_cache == nullptr) {
        candidate = std::make_shared<const OperationList>(
            materialize_operations());
    } else {
        auto representative = body_cache->cached_operations();
        if (!representative) {
            auto initial_body = std::make_shared<const OperationList>(
                materialize_operations());
            representative = body_cache->publish_or_get(initial_body);
            if (representative == initial_body) {
                candidate = std::move(initial_body);
            }
        }
        if (!candidate) {
            OperationList instance = *representative;
            if (instance.size() != operation_count) {
                throw std::logic_error {
                    "shared startup write body changed its operation count"
                };
            }
            const auto statement_offset = statement_point ? 1U : 0U;
            instance.replace(0U, entry_point);
            if (statement_point) {
                instance.replace(1U, *statement_point);
            }
            instance.replace(1U + statement_offset,
                LoadConstant { 0U, value });
            const auto copy_offset
                = 2U + statement_offset;
            if (constant_copy) {
                instance.replace(copy_offset, *constant_copy);
            }
            const auto write_index = copy_offset
                + static_cast<std::size_t>(constant_copy.has_value());
            const auto write_source
                = constant_copy ? constant_copy->destination : 0U;
            if (slice) {
                instance.replace(write_index,
                    WriteUpdateSlice {
                        signal, write_source, offset, update_domain
                    });
            } else {
                instance.replace(write_index,
                    WriteUpdate { signal, write_source, update_domain });
            }
            candidate = std::make_shared<const OperationList>(
                std::move(instance));
        }
    }
    std::shared_ptr<const OperationList> expected;
    if (materialized_operations.compare_exchange_strong(
            expected, candidate,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return *candidate;
    }
    return *expected;
}

ProcessProgramView::ProcessProgramView(const Process& facade) noexcept
    : facade_ { &facade }
{
}

ProcessProgramView::ProcessProgramView(const ProcessProgramView& original,
    const ProcessProgramOverlay& overlay) noexcept
    : ProcessProgramView(original)
{
    overlay_ = &overlay;
}

bool ProcessProgramView::valid() const noexcept
{
    return facade_ != nullptr
        || (common_ != nullptr && instance_ != nullptr);
}

const void* ProcessProgramView::common_identity() const noexcept
{
    if (overlay_ != nullptr) {
        return overlay_;
    }
    return common_ != nullptr
        ? static_cast<const void*>(common_)
        : static_cast<const void*>(facade_);
}

bool ProcessProgramView::matches_registered_binding(
    const ProcessId process,
    const ProcessExecutorProgramBinding& binding) const noexcept
{
    if (!valid() || !binding.valid()) {
        return false;
    }
    const void* operation_identity { };
    if (startup_write_bank_ != nullptr
        && facade_ == nullptr && overlay_ == nullptr) {
        const auto cached = startup_write_bank_->cached_operations();
        if (!cached) {
            return false;
        }
        operation_identity = cached->body_identity();
    } else {
        operation_identity = operations().body_identity();
    }
    return process == binding.registered_process_
        && id() == binding.registered_process_
        && operation_identity == binding.registered_body_
        && operations().access_revision() == binding.registered_revision_;
}

bool ProcessProgramView::matches_forked_binding(
    const ProcessId process,
    const ProcessExecutorProgramBinding& binding) const noexcept
{
    if (!valid() || !binding.valid()) {
        return false;
    }
    const void* operation_identity { };
    if (startup_write_bank_ != nullptr
        && facade_ == nullptr && overlay_ == nullptr) {
        const auto cached = startup_write_bank_->cached_operations();
        if (!cached) {
            return false;
        }
        operation_identity = cached->body_identity();
    } else {
        operation_identity = operations().body_identity();
    }
    return id() == process
        && operation_identity == binding.registered_body_
        && operations().access_revision() == binding.registered_revision_;
}

ProcessExecutorProgramBinding ProcessProgramView::executor_binding(
    const ProcessProgramView& registered,
    const ProcessProgramView& generated,
    const ProcessId generated_process,
    std::shared_ptr<const ProcessSignalRemap> signal_remap)
{
    ProcessExecutorProgramBinding result;
    if (!registered.valid() || !generated.valid()
        || generated_process != generated.id()) {
        return result;
    }
    const auto& registered_operations = registered.operations();
    const auto& generated_operations = generated.operations();
    result.registered_process_ = registered.id();
    result.registered_body_ = registered_operations.body_identity();
    result.registered_revision_ = registered_operations.access_revision();
    result.generated_process_ = generated_process;
    result.generated_program_id_ = generated.id();
    result.generated_body_ = generated_operations.body_identity();
    result.generated_revision_ = generated_operations.access_revision();
    result.signal_remap_ = std::move(signal_remap);
    return result.valid() ? result : ProcessExecutorProgramBinding { };
}

ProcessExecutorProgramBinding ProcessProgramView::fork_access_attestation(
    const ProcessExecutorProgramBinding& binding)
{
    auto result = binding;
    if (binding.signal_remap_ && !binding.signal_remap_->empty()) {
        result.signal_remap_
            = std::make_shared<const ProcessSignalRemap>(
                *binding.signal_remap_);
    } else {
        // Null and empty remaps have identical binding semantics. Normalize
        // both to null so the attestation owns no mutable empty-vector alias.
        result.signal_remap_.reset();
    }
    return result;
}

const ProcessId& ProcessProgramView::id() const noexcept
{
    return facade_ != nullptr ? facade_->id : instance_->id;
}

const std::string& ProcessProgramView::name() const noexcept
{
    return facade_ != nullptr ? facade_->name : instance_->name;
}

const std::string& ProcessProgramView::language_standard() const noexcept
{
    return facade_ != nullptr ? facade_->language_standard
                              : common_->language_standard;
}

const std::string& ProcessProgramView::compatibility_profile() const noexcept
{
    return facade_ != nullptr ? facade_->compatibility_profile
                              : common_->compatibility_profile;
}

const std::size_t& ProcessProgramView::register_count() const noexcept
{
    if (overlay_ != nullptr) {
        return overlay_->register_count;
    }
    return facade_ != nullptr ? facade_->register_count
                              : common_->register_count;
}

const std::size_t& ProcessProgramView::string_register_count() const noexcept
{
    return facade_ != nullptr ? facade_->string_register_count
                              : common_->string_register_count;
}

const std::size_t& ProcessProgramView::container_register_count() const noexcept
{
    if (overlay_ != nullptr) {
        return overlay_->container_register_count;
    }
    return facade_ != nullptr ? facade_->container_register_count
                              : common_->container_register_count;
}

const std::vector<DebugLocal>& ProcessProgramView::debug_locals() const noexcept
{
    return facade_ != nullptr ? facade_->debug_locals : common_->debug_locals;
}

const std::vector<DebugStringLocal>&
ProcessProgramView::debug_string_locals() const noexcept
{
    return facade_ != nullptr ? facade_->debug_string_locals
                              : common_->debug_string_locals;
}

const std::vector<DebugContainerLocal>&
ProcessProgramView::debug_container_locals() const noexcept
{
    return facade_ != nullptr ? facade_->debug_container_locals
                              : common_->debug_container_locals;
}

const CopyOnWriteVector<ContainerType>&
ProcessProgramView::container_register_types() const noexcept
{
    if (overlay_ != nullptr) {
        return overlay_->container_register_types;
    }
    return facade_ != nullptr ? facade_->container_register_types
                              : common_->container_register_types;
}

const std::vector<Sensitivity>&
ProcessProgramView::static_sensitivity() const noexcept
{
    return facade_ != nullptr ? facade_->static_sensitivity
                              : instance_->static_sensitivity;
}

const CopyOnWriteVector<Process::StaticTriggerRegion>&
ProcessProgramView::static_trigger_regions() const noexcept
{
    return facade_ != nullptr ? facade_->static_trigger_regions
                              : common_->static_trigger_regions;
}

const OperationList& ProcessProgramView::operations() const
{
    if (overlay_ != nullptr) {
        return overlay_->operations;
    }
    if (facade_ != nullptr) {
        return facade_->operations;
    }
    return startup_write_bank_ != nullptr
        ? startup_write_bank_->operations() : instance_->operations;
}

const std::vector<Process::DriverRegion>&
ProcessProgramView::driver_regions() const noexcept
{
    return facade_ != nullptr ? facade_->driver_regions
                              : instance_->driver_regions;
}

const DriveStrength& ProcessProgramView::drive_strength() const noexcept
{
    return facade_ != nullptr ? facade_->drive_strength
                              : instance_->drive_strength;
}

const std::optional<SignalId>& ProcessProgramView::switch_source() const noexcept
{
    return facade_ != nullptr ? facade_->switch_source
                              : instance_->switch_source;
}

const std::optional<SignalId>& ProcessProgramView::switch_target() const noexcept
{
    return facade_ != nullptr ? facade_->switch_target
                              : instance_->switch_target;
}

const std::optional<SignalId>& ProcessProgramView::switch_control() const noexcept
{
    return facade_ != nullptr ? facade_->switch_control
                              : instance_->switch_control;
}

const std::uint64_t& ProcessProgramView::switch_source_offset() const noexcept
{
    return facade_ != nullptr ? facade_->switch_source_offset
                              : instance_->switch_source_offset;
}

const std::uint64_t& ProcessProgramView::switch_target_offset() const noexcept
{
    return facade_ != nullptr ? facade_->switch_target_offset
                              : instance_->switch_target_offset;
}

const std::uint64_t& ProcessProgramView::switch_width() const noexcept
{
    return facade_ != nullptr ? facade_->switch_width : instance_->switch_width;
}

const bool& ProcessProgramView::switch_active_high() const noexcept
{
    return facade_ != nullptr ? facade_->switch_active_high
                              : instance_->switch_active_high;
}

const bool& ProcessProgramView::switch_bidirectional() const noexcept
{
    return facade_ != nullptr ? facade_->switch_bidirectional
                              : instance_->switch_bidirectional;
}

const bool& ProcessProgramView::switch_resistive() const noexcept
{
    return facade_ != nullptr ? facade_->switch_resistive
                              : instance_->switch_resistive;
}

const CopyOnWriteVector<ValueKind>&
ProcessProgramView::register_value_kinds() const noexcept
{
    if (overlay_ != nullptr) {
        return overlay_->register_value_kinds;
    }
    return facade_ != nullptr ? facade_->register_value_kinds
                              : common_->register_value_kinds;
}

const bool& ProcessProgramView::initialize() const noexcept
{
    return facade_ != nullptr ? facade_->initialize : instance_->initialize;
}

const bool& ProcessProgramView::observed() const noexcept
{
    return facade_ != nullptr ? facade_->observed : instance_->observed;
}

const bool& ProcessProgramView::reactive() const noexcept
{
    return facade_ != nullptr ? facade_->reactive : instance_->reactive;
}

const std::optional<std::uint32_t>&
ProcessProgramView::program_owner() const noexcept
{
    return facade_ != nullptr ? facade_->program_owner
                              : instance_->program_owner;
}

const bool& ProcessProgramView::postponed() const noexcept
{
    return facade_ != nullptr ? facade_->postponed : instance_->postponed;
}

const bool& ProcessProgramView::final() const noexcept
{
    return facade_ != nullptr ? facade_->final : instance_->final;
}

const ExpressionProfileList&
ProcessProgramView::expression_profiles() const noexcept
{
    return facade_ != nullptr ? facade_->expression_profiles
                              : common_->expression_profiles;
}

const ProcessSchedulingDomain&
ProcessProgramView::scheduling_domain() const noexcept
{
    return facade_ != nullptr ? facade_->scheduling_domain
                              : common_->scheduling_domain;
}

Process ProcessProgramView::materialize() const
{
    return materialize_impl(true);
}

Process ProcessProgramView::materialize_transient() const
{
    return materialize_impl(false);
}

Process ProcessProgramView::materialize_impl(
    const bool cache_startup_body) const
{
    if (facade_ != nullptr && overlay_ == nullptr) {
        return *facade_;
    }
    Process process;
    process.id = id();
    process.name = name();
    process.language_standard = language_standard();
    process.compatibility_profile = compatibility_profile();
    process.register_count = register_count();
    process.string_register_count = string_register_count();
    process.container_register_count = container_register_count();
    process.debug_locals = debug_locals();
    process.debug_string_locals = debug_string_locals();
    process.debug_container_locals = debug_container_locals();
    process.container_register_types = container_register_types();
    process.static_sensitivity = static_sensitivity();
    process.static_trigger_regions = static_trigger_regions();
    process.operations = !cache_startup_body
            && startup_write_bank_ != nullptr
            && overlay_ == nullptr && facade_ == nullptr
        ? startup_write_bank_->materialize_operations()
        : operations();
    process.driver_regions = driver_regions();
    process.drive_strength = drive_strength();
    process.switch_source = switch_source();
    process.switch_target = switch_target();
    process.switch_control = switch_control();
    process.switch_source_offset = switch_source_offset();
    process.switch_target_offset = switch_target_offset();
    process.switch_width = switch_width();
    process.switch_active_high = switch_active_high();
    process.switch_bidirectional = switch_bidirectional();
    process.switch_resistive = switch_resistive();
    process.register_value_kinds = register_value_kinds();
    process.initialize = initialize();
    process.observed = observed();
    process.reactive = reactive();
    process.program_owner = program_owner();
    process.postponed = postponed();
    process.final = final();
    process.expression_profiles = expression_profiles();
    process.scheduling_domain = scheduling_domain();
    return process;
}

std::shared_ptr<const ProcessProgramTemplate>
ProcessProgramTemplatePool::intern(const Process& process)
{
    return intern(ProcessProgramView { process });
}

std::shared_ptr<const ProcessProgramTemplate>
ProcessProgramTemplatePool::intern(const ProcessProgramView& process)
{
    const auto* const body = process.operations().body_identity();
    auto& bucket = buckets_[body];
    for (const auto& candidate : bucket) {
        if (candidate->matches(process)) {
            return candidate;
        }
    }
    auto candidate = std::make_shared<const ProcessProgramTemplate>(process);
    bucket.push_back(candidate);
    return candidate;
}

void ProcessProgramTemplatePool::clear() noexcept
{
    buckets_.clear();
}

void Interpreter::Impl::ProcessColdState::set_program(
    Process process,
    std::shared_ptr<const ProcessProgramTemplate> common)
{
    auto& storage = program_storage();
    storage.program_template = std::move(common);
    storage.instance_program
        = ProcessInstanceProgram { std::move(process) };
}

void Interpreter::Impl::ProcessColdState::set_program(
    ProcessInstanceProgram instance,
    std::shared_ptr<const ProcessProgramTemplate> common)
{
    // The decoded row already carries its instance-owned bindings. Installing
    // it directly avoids constructing a full public Process facade.
    auto& storage = program_storage();
    storage.program_template = std::move(common);
    storage.instance_program = std::move(instance);
}

void Interpreter::Impl::ProcessColdState::inherit_fork_program(
    const ProcessColdState& parent,
    const ProcessId child_id,
    const InstructionIndex fork_instruction,
    const std::size_t child_index)
{
    auto& storage = program_storage();
    storage.program_template
        = parent.program_storage().program_template;
    storage.instance_program = parent.program_storage().instance_program;
    auto& instance = storage.instance_program;
    instance.id = child_id;
    instance.name += ".$fork[" + std::to_string(fork_instruction)
        + "].child[" + std::to_string(child_index) + "]";
    instance.initialize = false;
    instance.final = false;
}

ProcessProgramView
Interpreter::Impl::ProcessColdState::program() const noexcept
{
    const auto& storage = program_storage();
    return ProcessProgramView {
        *storage.program_template,
        storage.instance_program
    };
}

void Interpreter::Impl::ProcessColdState::synchronize_public_program_operations()
{
    auto& storage = program_storage();
    if (storage.public_program_facade) {
        storage.public_program_facade->operations
            = storage.instance_program.operations;
    }
}

const Process& Interpreter::Impl::ProcessColdState::public_program() const
{
    auto& storage = program_storage();
    if (!storage.public_program_facade) {
        storage.public_program_facade
            = std::make_unique<Process>(program().materialize());
    }
    return *storage.public_program_facade;
}

ProcessProgramView InterpreterProgramAccess::view(
    const Interpreter& interpreter, const ProcessId process)
{
    return interpreter.impl_->processes.program_view(process);
}

ProcessExecutorProgramBinding InterpreterProgramAccess::executor_binding(
    const ProcessProgramView& registered,
    const ProcessProgramView& generated,
    const ProcessId generated_process,
    std::shared_ptr<const ProcessSignalRemap> signal_remap)
{
    return ProcessProgramView::executor_binding(
        registered, generated, generated_process, std::move(signal_remap));
}

ProcessExecutorProgramBinding
InterpreterProgramAccess::fork_access_attestation(
    const ProcessExecutorProgramBinding& binding)
{
    return ProcessProgramView::fork_access_attestation(binding);
}

bool InterpreterProgramAccess::data_only_startup_write(
    const Interpreter& interpreter, const ProcessId process) noexcept
{
    const auto* const compact
        = interpreter.impl_->processes.compact_constant(process);
    return compact != nullptr && compact->startup_write_bank != nullptr;
}

std::size_t InterpreterProgramAccess::operation_count(
    const Interpreter& interpreter, const ProcessId process)
{
    return interpreter.impl_->processes.operation_count(process);
}

ProcessId InterpreterProgramAccess::add_program(
    Interpreter& interpreter,
    std::shared_ptr<const ProcessProgramTemplate> common,
    ProcessInstanceProgram instance)
{
    if (interpreter.impl_->validation_only) {
        throw std::logic_error {
            "cannot add an executable SimIR process to a validation-only interpreter"
        };
    }
    if (!common) {
        throw std::invalid_argument {
            "an instance program requires a shared template"
        };
    }
    const ProcessProgramView program { *common, instance };
    return interpreter.impl_->add_process_program_impl(
        program, std::move(common), nullptr, &instance, true);
}

ProcessId InterpreterProgramAccess::validate_program(
    Interpreter& interpreter,
    std::shared_ptr<const ProcessProgramTemplate> common,
    ProcessInstanceProgram instance)
{
    if (!interpreter.impl_->validation_only
        && !interpreter.impl_->processes.empty()) {
        throw std::logic_error(
            "cannot add a validation-only SimIR process after executable processes");
    }
    interpreter.impl_->validation_only = true;
    if (!common) {
        throw std::invalid_argument {
            "an instance program requires a shared template"
        };
    }
    const ProcessProgramView program { *common, instance };
    return interpreter.impl_->add_process_program_impl(
        program, std::move(common), nullptr, &instance, false);
}

void InterpreterProgramAccess::set_trusted_signal_driver_inventory(
    Interpreter& interpreter,
    std::shared_ptr<const SignalDriverInventory> inventory) noexcept
{
    interpreter.impl_->elaborated_signal_driver_inventory
        = std::move(inventory);
}

void InterpreterProgramAccess::set_trusted_text_report_hook(
    Interpreter& interpreter, Interpreter::ReportHook hook)
{
    interpreter.impl_->require_all_region_forwarding_role_journals_flushed();
    interpreter.impl_->report_hook.set_trusted_text_hook(std::move(hook));
}

} // namespace fsim::runtime::simir
