// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_system.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace fsim::runtime {

struct SystemVerilogVpiSystemRegistry::Entry {
    SystemVerilogVpiSystemRegistrationHandle handle;
    SystemVerilogVpiSystemRegistration registration;
};

struct SystemVerilogVpiSystemRegistry::CallRecord {
    SystemVerilogVpiSystemCallHandle handle;
    std::shared_ptr<Entry> entry;
    SystemVerilogVpiSystemCallState state {
        SystemVerilogVpiSystemCallState::Active
    };
    SystemVerilogVpiSystemPhase phase {
        SystemVerilogVpiSystemPhase::None
    };
    SystemVerilogVpiObjectInfo scope;
    std::vector<SystemVerilogVpiStoredValue> arguments;
    std::vector<SystemVerilogVpiSystemArgumentHandle> argument_handles;
    std::uintptr_t user_data { };
    std::optional<SystemVerilogVpiStoredValue> result;
    SystemVerilogVpiSystemError execution_error {
        SystemVerilogVpiSystemError::None
    };
};

namespace {

    constexpr std::size_t maximum_name_size = 1'024;
    constexpr std::size_t maximum_diagnostic_size = 4'096;
    constexpr std::size_t maximum_argument_count = 65'536;
    std::atomic<std::uint64_t> next_system_registry_owner { 1 };

    using Routine = SystemVerilogVpiRoutineDescriptor;
    using RoutineKind = SystemVerilogVpiRoutineKind;
    using HandlePolicy = SystemVerilogVpiRoutineHandlePolicy;
    constexpr auto hierarchy_service = FSIM_VPI_SERVICE_HIERARCHY;
    constexpr auto value_service = FSIM_VPI_SERVICE_VALUE;
    constexpr auto time_service = FSIM_VPI_SERVICE_TIME;
    constexpr auto callback_service = FSIM_VPI_SERVICE_CALLBACK;
    constexpr auto control_service = FSIM_VPI_SERVICE_CONTROL;
    constexpr auto system_task_service = FSIM_VPI_SERVICE_SYSTEM_TASK;
    constexpr auto io_service = FSIM_VPI_SERVICE_IO;
    constexpr auto user_data_service = FSIM_VPI_SERVICE_USER_DATA;
    constexpr auto lifecycle_service = FSIM_VPI_SERVICE_LIFECYCLE;
    constexpr std::array<Routine, 42> standard_routines { {
        { RoutineKind::RegisterCallback, "vpi_register_cb", callback_service,
            HandlePolicy::Optional, true, true },
        { RoutineKind::RemoveCallback, "vpi_remove_cb", callback_service,
            HandlePolicy::Required, false, true },
        { RoutineKind::GetCallbackInfo, "vpi_get_cb_info", callback_service,
            HandlePolicy::Required, false, true },
        { RoutineKind::RegisterSystemTaskFunction, "vpi_register_systf",
            system_task_service, HandlePolicy::None, true, true },
        { RoutineKind::GetSystemTaskFunctionInfo, "vpi_get_systf_info",
            system_task_service, HandlePolicy::Required, false, true },
        { RoutineKind::Handle, "vpi_handle", hierarchy_service,
            HandlePolicy::Optional, true, false },
        { RoutineKind::HandleByName, "vpi_handle_by_name", hierarchy_service,
            HandlePolicy::Optional, true, false },
        { RoutineKind::HandleByIndex, "vpi_handle_by_index", hierarchy_service,
            HandlePolicy::Required, true, false },
        { RoutineKind::HandleByMultiIndex, "vpi_handle_by_multi_index",
            hierarchy_service, HandlePolicy::Required, true, false },
        { RoutineKind::HandleMulti, "vpi_handle_multi", hierarchy_service,
            HandlePolicy::Optional, true, false },
        { RoutineKind::Iterate, "vpi_iterate", hierarchy_service,
            HandlePolicy::Optional, true, false },
        { RoutineKind::Scan, "vpi_scan", hierarchy_service,
            HandlePolicy::Required, true, false },
        { RoutineKind::GetProperty, "vpi_get", hierarchy_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::GetProperty64, "vpi_get64", hierarchy_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::GetStringProperty, "vpi_get_str", hierarchy_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::GetDelays, "vpi_get_delays", value_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::PutDelays, "vpi_put_delays", value_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::GetValue, "vpi_get_value", value_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::PutValue, "vpi_put_value", value_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::GetTime, "vpi_get_time", time_service,
            HandlePolicy::Optional, false, false },
        { RoutineKind::GetVlogInfo, "vpi_get_vlog_info", lifecycle_service,
            HandlePolicy::None, false, false },
        { RoutineKind::CheckError, "vpi_chk_error", lifecycle_service,
            HandlePolicy::None, false, false },
        { RoutineKind::FreeObject, "vpi_free_object", hierarchy_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::ReleaseHandle, "vpi_release_handle", hierarchy_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::CompareObjects, "vpi_compare_objects", hierarchy_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::GetData, "vpi_get_data", value_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::PutData, "vpi_put_data", value_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::GetUserData, "vpi_get_userdata", user_data_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::PutUserData, "vpi_put_userdata", user_data_service,
            HandlePolicy::Required, false, false },
        { RoutineKind::Control, "vpi_control", control_service,
            HandlePolicy::None, false, false },
        { RoutineKind::VariableControl, "vpi_vcontrol", control_service,
            HandlePolicy::None, false, false },
        { RoutineKind::Print, "vpi_printf", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::VariablePrint, "vpi_vprintf", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::Flush, "vpi_flush", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::McdOpen, "vpi_mcd_open", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::McdClose, "vpi_mcd_close", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::McdName, "vpi_mcd_name", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::McdPrint, "vpi_mcd_printf", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::McdVariablePrint, "vpi_mcd_vprintf", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::McdFlush, "vpi_mcd_flush", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::FileOpen, "vpi_fopen", io_service,
            HandlePolicy::None, false, false },
        { RoutineKind::GetFile, "vpi_get_file", io_service,
            HandlePolicy::None, false, false },
    } };
    constexpr std::size_t maximum_routine_text_size = 1U << 20U;

    constexpr bool valid_status(const fsim_vpi_status_v1 status) noexcept
    {
        return status >= FSIM_VPI_STATUS_OK
            && status <= FSIM_VPI_STATUS_INTERNAL_ERROR;
    }

    void report_routine_error(
        const fsim_vpi_host_v2& host,
        const char* const code,
        const char* const message) noexcept
    {
        const auto expected_pointer_bits
            = static_cast<std::uint32_t>(sizeof(void*) * 8U);
        if (host.v1.abi_version != FSIM_VPI_HOST_ABI_VERSION_V2
            || host.v1.struct_size < sizeof(fsim_vpi_host_v2)
            || host.v1.pointer_bits != expected_pointer_bits
            || host.v1.flags != 0U || host.v1.context == nullptr
            || host.v1.report == nullptr) {
            return;
        }
        const fsim_vpi_error_view_v1 error {
            FSIM_VPI_ERROR_ERROR,
            static_cast<std::uint32_t>(std::char_traits<char>::length(code)),
            code,
            static_cast<std::uint32_t>(std::char_traits<char>::length(message)),
            message,
        };
        try {
            host.v1.report(host.v1.context, &error);
        } catch (...) {
        }
    }

    bool valid_kind(const SystemVerilogVpiSystemCallableKind kind) noexcept
    {
        switch (kind) {
        case SystemVerilogVpiSystemCallableKind::Task:
        case SystemVerilogVpiSystemCallableKind::Function:
            return true;
        }
        return false;
    }

    bool valid_name(const std::string_view name) noexcept
    {
        if (name.size() < 2U || name.size() > maximum_name_size
            || name.front() != '$') {
            return false;
        }
        const auto first = static_cast<unsigned char>(name[1]);
        if (std::isalpha(first) == 0 && name[1] != '_') {
            return false;
        }
        for (const char character : name.substr(2)) {
            const auto byte = static_cast<unsigned char>(character);
            if (std::isalnum(byte) == 0 && character != '_'
                && character != '$') {
                return false;
            }
        }
        return true;
    }

    bool valid_return_type(const SystemVerilogVpiTypeInfo& type) noexcept
    {
        switch (type.category) {
        case SystemVerilogVpiValueCategory::Bit2:
        case SystemVerilogVpiValueCategory::Logic4:
        case SystemVerilogVpiValueCategory::Logic9:
        case SystemVerilogVpiValueCategory::Integer2:
        case SystemVerilogVpiValueCategory::Integer4:
        case SystemVerilogVpiValueCategory::Time:
            return type.width != 0U;
        case SystemVerilogVpiValueCategory::Real:
            return type.width == 64U;
        case SystemVerilogVpiValueCategory::ShortReal:
            return type.width == 32U;
        case SystemVerilogVpiValueCategory::String:
            return true;
        case SystemVerilogVpiValueCategory::None:
        case SystemVerilogVpiValueCategory::Event:
            return false;
        }
        return false;
    }

    bool valid_scope_kind(const SystemVerilogVpiObjectKind kind) noexcept
    {
        switch (kind) {
        case SystemVerilogVpiObjectKind::Root:
        case SystemVerilogVpiObjectKind::Module:
        case SystemVerilogVpiObjectKind::Interface:
        case SystemVerilogVpiObjectKind::Program:
        case SystemVerilogVpiObjectKind::Package:
        case SystemVerilogVpiObjectKind::GenerateScope:
        case SystemVerilogVpiObjectKind::Class:
            return true;
        case SystemVerilogVpiObjectKind::Port:
        case SystemVerilogVpiObjectKind::Net:
        case SystemVerilogVpiObjectKind::Variable:
        case SystemVerilogVpiObjectKind::Parameter:
        case SystemVerilogVpiObjectKind::Memory:
        case SystemVerilogVpiObjectKind::Array:
        case SystemVerilogVpiObjectKind::ClassProperty:
        case SystemVerilogVpiObjectKind::NamedEvent:
        case SystemVerilogVpiObjectKind::Process:
        case SystemVerilogVpiObjectKind::Assertion:
        case SystemVerilogVpiObjectKind::Driver:
        case SystemVerilogVpiObjectKind::Constant:
        case SystemVerilogVpiObjectKind::Concatenation:
        case SystemVerilogVpiObjectKind::Operation:
        case SystemVerilogVpiObjectKind::MinTypMax:
            return false;
        default:
            return false;
        }
    }

    std::string bounded_diagnostic(std::string diagnostic) noexcept
    {
        if (diagnostic.size() > maximum_diagnostic_size) {
            diagnostic.resize(maximum_diagnostic_size);
        }
        return diagnostic;
    }

    std::string safe_diagnostic(const std::string_view diagnostic) noexcept
    {
        std::string result;
        try {
            result.assign(diagnostic.substr(0, maximum_diagnostic_size));
        } catch (...) {
            result.clear();
        }
        return result;
    }

    SystemVerilogVpiSystemExecuteResult blank_execution_error(
        const SystemVerilogVpiSystemError error) noexcept
    {
        return { { },
            error,
            SystemVerilogVpiSystemPhase::None,
            SystemVerilogVpiSystemCallableKind::Task,
            std::nullopt,
            std::nullopt,
            { } };
    }

    SystemVerilogVpiSystemExecuteResult execution_result(
        const SystemVerilogVpiSystemRegistry::Entry& entry,
        const SystemVerilogVpiSystemCallHandle call,
        const SystemVerilogVpiSystemError error,
        const SystemVerilogVpiSystemPhase phase,
        std::optional<SystemVerilogVpiStoredValue> value,
        std::string diagnostic) noexcept
    {
        return { call,
            error,
            phase,
            entry.registration.kind,
            entry.registration.return_type,
            std::move(value),
            bounded_diagnostic(std::move(diagnostic)) };
    }

    SystemVerilogVpiSystemError handle_error(
        const std::uint64_t owner,
        const std::uint64_t expected_owner,
        const bool has_payload) noexcept
    {
        if (owner == 0U || !has_payload) {
            return SystemVerilogVpiSystemError::InvalidHandle;
        }
        if (owner != expected_owner) {
            return SystemVerilogVpiSystemError::CrossRegistry;
        }
        return SystemVerilogVpiSystemError::None;
    }

} // namespace

std::span<const SystemVerilogVpiRoutineDescriptor>
systemverilog_vpi_2023_routines() noexcept
{
    return standard_routines;
}

const SystemVerilogVpiRoutineDescriptor*
find_systemverilog_vpi_2023_routine(
    const SystemVerilogVpiRoutineKind kind) noexcept
{
    const auto index = static_cast<std::size_t>(kind);
    return index < standard_routines.size()
        && standard_routines[index].kind == kind
        ? &standard_routines[index] : nullptr;
}

const SystemVerilogVpiRoutineDescriptor*
find_systemverilog_vpi_2023_routine(const std::string_view name) noexcept
{
    const auto found = std::ranges::find(
        standard_routines, name, &SystemVerilogVpiRoutineDescriptor::name);
    return found == standard_routines.end() ? nullptr : &*found;
}

SystemVerilogVpiRoutineResult invoke_systemverilog_vpi_2023_routine(
    const fsim_vpi_host_v2& host,
    const SystemVerilogVpiRoutineKind routine,
    const fsim_vpi_service_request_v1& request) noexcept
{
    const auto* descriptor = find_systemverilog_vpi_2023_routine(routine);
    if (descriptor == nullptr) {
        report_routine_error(host, "FSIM-VPI-ROUTINE-001",
            "unknown SystemVerilog-2023 VPI routine");
        return { { }, SystemVerilogVpiRoutineError::InvalidRoutine };
    }
    const auto expected_pointer_bits
        = static_cast<std::uint32_t>(sizeof(void*) * 8U);
    if (host.v1.abi_version != FSIM_VPI_HOST_ABI_VERSION_V2
        || host.v1.struct_size < sizeof(fsim_vpi_host_v2)
        || host.v1.pointer_bits != expected_pointer_bits
        || host.v1.flags != 0U || host.v1.simulation_identity == 0U
        || host.service_context == nullptr || host.invoke_service == nullptr) {
        report_routine_error(host, "FSIM-VPI-ROUTINE-002",
            "invalid VPI host service boundary");
        return { { }, SystemVerilogVpiRoutineError::InvalidHost };
    }
    if (request.struct_size < sizeof(fsim_vpi_service_request_v1)
        || request.operation != descriptor->service
        || request.text_size > maximum_routine_text_size
        || (request.text_size != 0U && request.text == nullptr)
        || (descriptor->handle_policy
                == SystemVerilogVpiRoutineHandlePolicy::None
            && request.handle != 0U)) {
        report_routine_error(host, "FSIM-VPI-ROUTINE-003",
            "invalid VPI routine request");
        return { { }, SystemVerilogVpiRoutineError::InvalidRequest };
    }
    if (descriptor->handle_policy
            == SystemVerilogVpiRoutineHandlePolicy::Required
        && request.handle == 0U) {
        report_routine_error(host, "FSIM-VPI-ROUTINE-004",
            "required VPI routine handle is missing");
        return { { }, SystemVerilogVpiRoutineError::InvalidHandle };
    }

    fsim_vpi_service_result_v1 result {
        static_cast<std::uint32_t>(sizeof(fsim_vpi_service_result_v1)),
        FSIM_VPI_STATUS_INTERNAL_ERROR,
        0U,
        0U,
        0U,
        0U,
        0U,
    };
    fsim_vpi_status_v1 status { FSIM_VPI_STATUS_INTERNAL_ERROR };
    try {
        status = host.invoke_service(host.service_context, &request, &result);
    } catch (...) {
        report_routine_error(host, "FSIM-VPI-ROUTINE-005",
            "VPI service callback raised an exception");
        return { { }, SystemVerilogVpiRoutineError::CallbackException };
    }
    if (!valid_status(status) || status != FSIM_VPI_STATUS_OK) {
        report_routine_error(host, "FSIM-VPI-ROUTINE-006",
            "VPI service callback rejected the request");
        return { result, SystemVerilogVpiRoutineError::CallbackFailure };
    }
    if (result.struct_size < sizeof(fsim_vpi_service_result_v1)
        || !valid_status(result.status) || result.reserved != 0U
        || result.status != FSIM_VPI_STATUS_OK
        || (descriptor->returns_handle && result.handle == 0U)) {
        report_routine_error(host, "FSIM-VPI-ROUTINE-007",
            "VPI service callback returned a malformed result");
        return { result, SystemVerilogVpiRoutineError::MalformedResult };
    }
    return { result, SystemVerilogVpiRoutineError::None };
}

SystemVerilogVpiSystemRegistry::SystemVerilogVpiSystemRegistry(
    const SystemVerilogVpiObjectRegistry& objects) noexcept
    : objects_(&objects)
    , owner_(next_system_registry_owner.fetch_add(
          1, std::memory_order_relaxed))
{
}

SystemVerilogVpiSystemRegistry::~SystemVerilogVpiSystemRegistry()
{
    teardown();
}

bool SystemVerilogVpiSystemRegistry::valid() const noexcept
{
    std::scoped_lock lock(mutex_);
    return objects_ != nullptr && owner_ != 0U && objects_->valid()
        && !closed_;
}

std::uint64_t SystemVerilogVpiSystemRegistry::simulation_identity()
    const noexcept
{
    std::scoped_lock lock(mutex_);
    return objects_ != nullptr && owner_ != 0U && !closed_
        ? objects_->simulation_identity()
        : 0U;
}

SystemVerilogVpiSystemRegisterResult
SystemVerilogVpiSystemRegistry::register_callable(
    SystemVerilogVpiSystemRegistration registration)
{
    if (objects_ == nullptr || owner_ == 0U || !objects_->valid()) {
        return { { },
            SystemVerilogVpiSystemError::InvalidRegistry,
            "system callable registry has no live simulation" };
    }
    if (!valid_kind(registration.kind)) {
        return { { },
            SystemVerilogVpiSystemError::InvalidKind,
            "system callable kind is not supported" };
    }
    if (!valid_name(registration.name)) {
        return { { },
            SystemVerilogVpiSystemError::InvalidName,
            "system callable name must be a bounded $identifier" };
    }

    const bool is_function = registration.kind
        == SystemVerilogVpiSystemCallableKind::Function;
    if (!registration.compiletf || !registration.calltf
        || (is_function
            && (!registration.sizetf || !registration.return_type
                || !valid_return_type(*registration.return_type)))
        || (!is_function
            && (registration.sizetf || registration.return_type))) {
        return { { },
            SystemVerilogVpiSystemError::InvalidProfile,
            "system task/function callbacks and return profile disagree" };
    }

    try {
        auto key = registration.name;
        std::scoped_lock lock(mutex_);
        if (closed_) {
            return { { },
                SystemVerilogVpiSystemError::Closed,
                "system callable registry has been torn down" };
        }
        if (sealed_) {
            return { { },
                SystemVerilogVpiSystemError::LateRegistration,
                "system callable registration phase is sealed" };
        }
        if (registrations_.contains(key)) {
            return { { },
                SystemVerilogVpiSystemError::DuplicateName,
                "system callable name is already registered" };
        }
        if (next_registration_ == 0U
            || next_registration_
                == std::numeric_limits<std::uint64_t>::max()) {
            return { { },
                SystemVerilogVpiSystemError::ResourceLimit,
                "system callable registration handle space is exhausted" };
        }

        const SystemVerilogVpiSystemRegistrationHandle handle {
            owner_, next_registration_
        };
        auto entry = std::make_shared<Entry>(
            Entry { handle, std::move(registration) });
        const auto [name_iterator, inserted] = registrations_.emplace(std::move(key), entry);
        if (!inserted) {
            return { { },
                SystemVerilogVpiSystemError::DuplicateName,
                "system callable name is already registered" };
        }
        try {
            registration_handles_.emplace(handle.id, entry);
        } catch (...) {
            registrations_.erase(name_iterator);
            throw;
        }
        ++next_registration_;
        return { handle, SystemVerilogVpiSystemError::None, { } };
    } catch (const std::bad_alloc&) {
        return { { },
            SystemVerilogVpiSystemError::ResourceLimit,
            "system callable registration allocation failed" };
    } catch (...) {
        return { { },
            SystemVerilogVpiSystemError::ResourceLimit,
            "system callable registration could not be published" };
    }
}

SystemVerilogVpiSystemExecuteResult
SystemVerilogVpiSystemRegistry::execute(
    const std::string_view name,
    const fsim_vpi_handle_v1 scope,
    std::vector<SystemVerilogVpiStoredValue> arguments,
    const std::uintptr_t call_user_data)
{
    std::shared_ptr<Entry> entry;
    try {
        std::scoped_lock lock(mutex_);
        if (closed_) {
            return blank_execution_error(SystemVerilogVpiSystemError::Closed);
        }
        const auto iterator = registrations_.find(std::string { name });
        if (iterator == registrations_.end()) {
            return blank_execution_error(SystemVerilogVpiSystemError::NotFound);
        }
        entry = iterator->second;
    } catch (...) {
        return blank_execution_error(SystemVerilogVpiSystemError::ResourceLimit);
    }

    if (arguments.size() > maximum_argument_count) {
        return execution_result(
            *entry,
            { },
            SystemVerilogVpiSystemError::InvalidArguments,
            SystemVerilogVpiSystemPhase::None,
            std::nullopt,
            "system callable argument count exceeds the runtime limit");
    }
    const auto scope_lookup = objects_->lookup(scope);
    if (!scope_lookup) {
        const auto error = scope_lookup.error
                == SystemVerilogVpiObjectError::CrossSimulation
            ? SystemVerilogVpiSystemError::CrossSimulation
            : SystemVerilogVpiSystemError::InvalidScope;
        return execution_result(
            *entry,
            { },
            error,
            SystemVerilogVpiSystemPhase::None,
            std::nullopt,
            "system callable invocation scope is not live in this simulation");
    }
    if (!valid_scope_kind(scope_lookup.value->kind)) {
        return execution_result(
            *entry,
            { },
            SystemVerilogVpiSystemError::InvalidScope,
            SystemVerilogVpiSystemPhase::None,
            std::nullopt,
            "system callable invocation object is not a scope");
    }

    std::shared_ptr<CallRecord> record;
    try {
        std::scoped_lock lock(mutex_);
        if (closed_) {
            return blank_execution_error(SystemVerilogVpiSystemError::Closed);
        }
        if (next_call_ == 0U
            || next_call_ == std::numeric_limits<std::uint64_t>::max()) {
            return execution_result(
                *entry,
                { },
                SystemVerilogVpiSystemError::ResourceLimit,
                SystemVerilogVpiSystemPhase::None,
                std::nullopt,
                "system call handle space is exhausted");
        }
        const SystemVerilogVpiSystemCallHandle handle { owner_, next_call_ };
        record = std::make_shared<CallRecord>();
        record->handle = handle;
        record->entry = entry;
        record->scope = *scope_lookup.value;
        record->arguments = std::move(arguments);
        record->user_data = call_user_data;
        record->argument_handles.reserve(record->arguments.size());
        for (std::size_t index = 0; index < record->arguments.size(); ++index) {
            record->argument_handles.push_back(
                { owner_, handle.id, static_cast<std::uint64_t>(index + 1U) });
        }
        calls_.emplace(handle.id, record);
        ++next_call_;
    } catch (...) {
        return execution_result(
            *entry,
            { },
            SystemVerilogVpiSystemError::ResourceLimit,
            SystemVerilogVpiSystemPhase::None,
            std::nullopt,
            "system call record allocation failed");
    }

    const SystemVerilogVpiSystemInvocation invocation {
        entry->registration.kind,
        entry->registration.name,
        entry->registration.return_type,
        record->scope,
        record->arguments,
        record->argument_handles,
        entry->handle,
        record->handle,
        entry->registration.user_data,
        record->user_data
    };

    const auto set_phase = [&](const SystemVerilogVpiSystemPhase phase) {
        std::scoped_lock lock(mutex_);
        record->phase = phase;
    };
    const auto registry_closed = [&]() {
        std::scoped_lock lock(mutex_);
        return closed_;
    };
    const auto finish = [&](
                            SystemVerilogVpiSystemError error,
                            const SystemVerilogVpiSystemPhase phase,
                            std::string diagnostic) {
        std::optional<SystemVerilogVpiStoredValue> value;
        try {
            std::scoped_lock lock(mutex_);
            if (closed_ && error == SystemVerilogVpiSystemError::None) {
                error = SystemVerilogVpiSystemError::Closed;
                diagnostic = safe_diagnostic(
                    "system callable registry was torn down during execution");
            }
            record->phase = phase;
            record->execution_error = error;
            record->state = error == SystemVerilogVpiSystemError::None
                ? SystemVerilogVpiSystemCallState::Completed
                : SystemVerilogVpiSystemCallState::Failed;
            value = record->result;
        } catch (...) {
            error = SystemVerilogVpiSystemError::ResourceLimit;
            diagnostic = safe_diagnostic(
                "system call result could not be retained");
            record->execution_error = error;
            record->state = SystemVerilogVpiSystemCallState::Failed;
            value.reset();
        }
        return execution_result(
            *entry,
            record->handle,
            error,
            phase,
            std::move(value),
            std::move(diagnostic));
    };

    set_phase(SystemVerilogVpiSystemPhase::Compile);
    try {
        auto result = entry->registration.compiletf(invocation);
        if (!result.accepted) {
            return finish(
                SystemVerilogVpiSystemError::CompileRejected,
                SystemVerilogVpiSystemPhase::Compile,
                std::move(result.diagnostic));
        }
    } catch (const std::exception& error) {
        return finish(
            SystemVerilogVpiSystemError::CallbackException,
            SystemVerilogVpiSystemPhase::Compile,
            safe_diagnostic(error.what()));
    } catch (...) {
        return finish(
            SystemVerilogVpiSystemError::CallbackException,
            SystemVerilogVpiSystemPhase::Compile,
            safe_diagnostic("compiletf raised a non-standard exception"));
    }
    if (registry_closed()) {
        return finish(
            SystemVerilogVpiSystemError::Closed,
            SystemVerilogVpiSystemPhase::Compile,
            safe_diagnostic(
                "system callable registry was torn down by compiletf"));
    }

    if (entry->registration.kind
        == SystemVerilogVpiSystemCallableKind::Function) {
        set_phase(SystemVerilogVpiSystemPhase::Size);
        try {
            auto result = entry->registration.sizetf(invocation);
            if (!result.accepted) {
                return finish(
                    SystemVerilogVpiSystemError::SizeRejected,
                    SystemVerilogVpiSystemPhase::Size,
                    std::move(result.diagnostic));
            }
            if (!entry->registration.return_type
                || result.width != entry->registration.return_type->width) {
                return finish(
                    SystemVerilogVpiSystemError::SizeMismatch,
                    SystemVerilogVpiSystemPhase::Size,
                    safe_diagnostic(
                        "sizetf width disagrees with the registered return profile"));
            }
        } catch (const std::exception& error) {
            return finish(
                SystemVerilogVpiSystemError::CallbackException,
                SystemVerilogVpiSystemPhase::Size,
                safe_diagnostic(error.what()));
        } catch (...) {
            return finish(
                SystemVerilogVpiSystemError::CallbackException,
                SystemVerilogVpiSystemPhase::Size,
                safe_diagnostic("sizetf raised a non-standard exception"));
        }
        if (registry_closed()) {
            return finish(
                SystemVerilogVpiSystemError::Closed,
                SystemVerilogVpiSystemPhase::Size,
                safe_diagnostic(
                    "system callable registry was torn down by sizetf"));
        }
    }

    set_phase(SystemVerilogVpiSystemPhase::Call);
    try {
        auto result = entry->registration.calltf(invocation);
        if (!result.accepted) {
            return finish(
                SystemVerilogVpiSystemError::CallRejected,
                SystemVerilogVpiSystemPhase::Call,
                std::move(result.diagnostic));
        }
    } catch (const std::exception& error) {
        return finish(
            SystemVerilogVpiSystemError::CallbackException,
            SystemVerilogVpiSystemPhase::Call,
            safe_diagnostic(error.what()));
    } catch (...) {
        return finish(
            SystemVerilogVpiSystemError::CallbackException,
            SystemVerilogVpiSystemPhase::Call,
            safe_diagnostic("calltf raised a non-standard exception"));
    }

    if (entry->registration.kind
        == SystemVerilogVpiSystemCallableKind::Function) {
        bool result_missing { };
        {
            std::scoped_lock lock(mutex_);
            result_missing = !record->result.has_value();
        }
        if (result_missing) {
            return finish(
                SystemVerilogVpiSystemError::ResultNotPublished,
                SystemVerilogVpiSystemPhase::Call,
                safe_diagnostic(
                    "system function calltf did not publish a typed result"));
        }
    }
    return finish(
        SystemVerilogVpiSystemError::None,
        SystemVerilogVpiSystemPhase::Call,
        { });
}

SystemVerilogVpiSystemError
SystemVerilogVpiSystemRegistry::publish_result(
    const SystemVerilogVpiSystemCallHandle call,
    SystemVerilogVpiStoredValue value)
{
    const auto malformed = handle_error(
        call.owner, owner_, call.id != 0U);
    if (malformed != SystemVerilogVpiSystemError::None) {
        return malformed;
    }
    std::scoped_lock lock(mutex_);
    if (closed_) {
        return SystemVerilogVpiSystemError::Closed;
    }
    const auto iterator = calls_.find(call.id);
    if (iterator == calls_.end()) {
        return SystemVerilogVpiSystemError::StaleHandle;
    }
    auto& record = *iterator->second;
    if (record.state != SystemVerilogVpiSystemCallState::Active
        || record.phase != SystemVerilogVpiSystemPhase::Call
        || record.entry->registration.kind
            != SystemVerilogVpiSystemCallableKind::Function
        || !record.entry->registration.return_type) {
        return SystemVerilogVpiSystemError::InvalidResult;
    }
    if (record.result) {
        return SystemVerilogVpiSystemError::ResultAlreadyPublished;
    }
    if (!validate_systemverilog_vpi_stored_value(
            *record.entry->registration.return_type, value)) {
        return SystemVerilogVpiSystemError::ResultTypeMismatch;
    }
    try {
        record.result = std::move(value);
    } catch (...) {
        return SystemVerilogVpiSystemError::ResourceLimit;
    }
    return SystemVerilogVpiSystemError::None;
}

SystemVerilogVpiSystemArgumentResult
SystemVerilogVpiSystemRegistry::argument(
    const SystemVerilogVpiSystemCallHandle call,
    const std::size_t ordinal) const
{
    const auto malformed = handle_error(
        call.owner, owner_, call.id != 0U);
    if (malformed != SystemVerilogVpiSystemError::None) {
        return { { }, call, ordinal, std::nullopt, malformed };
    }
    std::scoped_lock lock(mutex_);
    if (closed_) {
        return { { }, call, ordinal, std::nullopt,
            SystemVerilogVpiSystemError::Closed };
    }
    const auto iterator = calls_.find(call.id);
    if (iterator == calls_.end()) {
        return { { }, call, ordinal, std::nullopt,
            SystemVerilogVpiSystemError::StaleHandle };
    }
    const auto& record = *iterator->second;
    if (ordinal >= record.arguments.size()) {
        return { { }, call, ordinal, std::nullopt,
            SystemVerilogVpiSystemError::InvalidHandle };
    }
    try {
        return { record.argument_handles[ordinal],
            call,
            ordinal,
            record.arguments[ordinal],
            SystemVerilogVpiSystemError::None };
    } catch (...) {
        return { { }, call, ordinal, std::nullopt,
            SystemVerilogVpiSystemError::ResourceLimit };
    }
}

SystemVerilogVpiSystemArgumentResult
SystemVerilogVpiSystemRegistry::argument(
    const SystemVerilogVpiSystemArgumentHandle argument_handle) const
{
    const auto malformed = handle_error(
        argument_handle.owner,
        owner_,
        argument_handle.call != 0U && argument_handle.ordinal != 0U);
    if (malformed != SystemVerilogVpiSystemError::None) {
        return { argument_handle, { }, 0, std::nullopt, malformed };
    }
    const SystemVerilogVpiSystemCallHandle call_handle {
        argument_handle.owner, argument_handle.call
    };
    if (argument_handle.ordinal
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return { argument_handle, call_handle, 0, std::nullopt,
            SystemVerilogVpiSystemError::InvalidHandle };
    }
    auto result = argument(
        call_handle,
        static_cast<std::size_t>(argument_handle.ordinal - 1U));
    if (result && result.handle != argument_handle) {
        result.error = SystemVerilogVpiSystemError::InvalidHandle;
        result.value.reset();
    }
    return result;
}

SystemVerilogVpiSystemCallResult
SystemVerilogVpiSystemRegistry::call(
    const SystemVerilogVpiSystemCallHandle call_handle) const
{
    const auto malformed = handle_error(
        call_handle.owner, owner_, call_handle.id != 0U);
    if (malformed != SystemVerilogVpiSystemError::None) {
        SystemVerilogVpiSystemCallResult result;
        result.handle = call_handle;
        result.error = malformed;
        return result;
    }
    std::scoped_lock lock(mutex_);
    if (closed_) {
        SystemVerilogVpiSystemCallResult result;
        result.handle = call_handle;
        result.error = SystemVerilogVpiSystemError::Closed;
        return result;
    }
    const auto iterator = calls_.find(call_handle.id);
    if (iterator == calls_.end()) {
        SystemVerilogVpiSystemCallResult result;
        result.handle = call_handle;
        result.error = SystemVerilogVpiSystemError::StaleHandle;
        return result;
    }
    const auto& record = *iterator->second;
    try {
        return { record.handle,
            record.entry->handle,
            record.state,
            record.entry->registration.kind,
            record.scope.handle,
            record.arguments.size(),
            record.entry->registration.user_data,
            record.user_data,
            record.result,
            record.execution_error,
            SystemVerilogVpiSystemError::None };
    } catch (...) {
        SystemVerilogVpiSystemCallResult result;
        result.handle = call_handle;
        result.error = SystemVerilogVpiSystemError::ResourceLimit;
        return result;
    }
}

SystemVerilogVpiSystemError
SystemVerilogVpiSystemRegistry::release_call(
    const SystemVerilogVpiSystemCallHandle call)
{
    const auto malformed = handle_error(
        call.owner, owner_, call.id != 0U);
    if (malformed != SystemVerilogVpiSystemError::None) {
        return malformed;
    }
    std::scoped_lock lock(mutex_);
    if (closed_) {
        return SystemVerilogVpiSystemError::Closed;
    }
    const auto iterator = calls_.find(call.id);
    if (iterator == calls_.end()) {
        return SystemVerilogVpiSystemError::StaleHandle;
    }
    if (iterator->second->state == SystemVerilogVpiSystemCallState::Active) {
        return SystemVerilogVpiSystemError::ActiveCall;
    }
    calls_.erase(iterator);
    return SystemVerilogVpiSystemError::None;
}

SystemVerilogVpiSystemError
SystemVerilogVpiSystemRegistry::unregister_callable(
    const SystemVerilogVpiSystemRegistrationHandle registration)
{
    const auto malformed = handle_error(
        registration.owner, owner_, registration.id != 0U);
    if (malformed != SystemVerilogVpiSystemError::None) {
        return malformed;
    }
    std::scoped_lock lock(mutex_);
    if (closed_) {
        return SystemVerilogVpiSystemError::Closed;
    }
    const auto iterator = registration_handles_.find(registration.id);
    if (iterator == registration_handles_.end()) {
        return SystemVerilogVpiSystemError::StaleHandle;
    }
    const auto entry = iterator->second;
    const auto name = registrations_.find(entry->registration.name);
    if (name != registrations_.end() && name->second == entry) {
        registrations_.erase(name);
    }
    registration_handles_.erase(iterator);
    return SystemVerilogVpiSystemError::None;
}

SystemVerilogVpiSystemError
SystemVerilogVpiSystemRegistry::seal_registrations()
{
    std::scoped_lock lock(mutex_);
    if (closed_) {
        return SystemVerilogVpiSystemError::Closed;
    }
    sealed_ = true;
    return SystemVerilogVpiSystemError::None;
}

bool SystemVerilogVpiSystemRegistry::registrations_sealed() const
{
    std::scoped_lock lock(mutex_);
    return sealed_;
}

void SystemVerilogVpiSystemRegistry::teardown() noexcept
{
    std::scoped_lock lock(mutex_);
    if (closed_) {
        return;
    }
    closed_ = true;
    sealed_ = true;
    calls_.clear();
    registrations_.clear();
    registration_handles_.clear();
}

std::size_t SystemVerilogVpiSystemRegistry::registrations() const
{
    std::scoped_lock lock(mutex_);
    return registrations_.size();
}

std::size_t SystemVerilogVpiSystemRegistry::calls() const
{
    std::scoped_lock lock(mutex_);
    return calls_.size();
}

} // namespace fsim::runtime
