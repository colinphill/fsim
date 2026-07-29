// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc.hpp"

namespace sc_core {

thread_local const fsim_sc_host_v1* detail::current_host = nullptr;
thread_local fsim_sc_handle_v1 detail::current_module = 0;
thread_local fsim_sc_process_kind_v1 detail::current_process_kind = FSIM_SC_THREAD;
thread_local fsim_sc_handle_v1 detail::construction_root_module = 0;
thread_local sc_module* detail::current_cpp_module = nullptr;
thread_local const char* detail::current_module_name = nullptr;

sc_module_name::State::~State() {
    detail::current_cpp_module = previous_cpp_module;
    detail::current_module_name = previous_name;
    if (restore_module) {
        detail::current_module = previous_module;
    }
}



    sc_time::sc_time(const double value, const sc_time_unit unit) {
        const auto scale = scale_for(unit);
        const auto femtoseconds =
            static_cast<long double>(value)
            * static_cast<long double>(scale);
        if (!std::isfinite(value) || value < 0.0
            || femtoseconds
                > static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max())) {
            throw std::out_of_range{"sc_time is outside the fsim time range"};
        }
        const auto integral = std::round(femtoseconds);
        const auto tolerance =
            static_cast<long double>(
                std::numeric_limits<double>::epsilon())
            * std::max(1.0L, std::fabs(femtoseconds)) * 4.0L;
        if (std::fabs(femtoseconds - integral) > tolerance) {
            throw std::invalid_argument{
                "sc_time must be exactly representable in femtoseconds"};
        }
        if (integral
            > static_cast<long double>(
                std::numeric_limits<std::uint64_t>::max())) {
            throw std::out_of_range{"sc_time is outside the fsim time range"};
        }
        femtoseconds_ = static_cast<std::uint64_t>(integral);
    }

detail::host_scope::host_scope(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module) noexcept
        : previous_host_(current_host),
          previous_module_(current_module) {
        current_host = host;
        current_module = module;
    }



    detail::host_scope::~host_scope() {
        current_host = previous_host_;
        current_module = previous_module_;
    }

detail::module_construction_scope::module_construction_scope(
        const fsim_sc_handle_v1 root_module) noexcept
        : previous_root_(construction_root_module),
          previous_cpp_module_(current_cpp_module) {
        construction_root_module = root_module;
        current_cpp_module = nullptr;
    }



    detail::module_construction_scope::~module_construction_scope() {
        construction_root_module = previous_root_;
        current_cpp_module = previous_cpp_module_;
    }

sc_event::sc_event() : handle_(detail::register_event(nullptr)) {}


    sc_event::sc_event(const char* name)
        : handle_(detail::register_event(name)) {}


    sc_event::sc_event(const fsim_sc_handle_v1 handle) noexcept : handle_(handle) {}



    void sc_event::notify() const {
        notify_impl(SC_ZERO_TIME, FSIM_SC_NOTIFY_IMMEDIATE);
    }



    void sc_event::notify(const sc_time delay) const {
        notify_impl(
            delay,
            delay.is_zero()
                ? FSIM_SC_NOTIFY_DELTA
                : FSIM_SC_NOTIFY_TIMED);
    }



    void sc_event::cancel() const {
        if (detail::current_host == nullptr
            || detail::current_host->cancel_event == nullptr) {
            throw std::logic_error{
                "sc_event::cancel requires an active fsim SystemC process"};
        }
        detail::check_status(
            detail::current_host->cancel_event(
                detail::current_host->context, handle_),
            "cancel event");
    }



    void sc_event::notify_delayed() const {
        notify_delayed(SC_ZERO_TIME);
    }



    void sc_event::notify_delayed(const sc_time delay) const {
        if (detail::current_host == nullptr
            || detail::current_host->notify_event_delayed == nullptr) {
            throw std::logic_error{
                "sc_event::notify_delayed requires an active fsim "
                "SystemC process"};
        }
        detail::check_status(
            detail::current_host->notify_event_delayed(
                detail::current_host->context,
                handle_,
                delay.value()),
            "notify event with notify_delayed");
    }



    [[nodiscard]] fsim_sc_handle_v1 sc_event::native_handle() const noexcept { return handle_; }

void sc_event::notify_impl(
        const sc_time delay,
        const fsim_sc_notification_kind_v1 kind) const {
        if (detail::current_host == nullptr
            || detail::current_host->notify_event_mode == nullptr) {
            throw std::logic_error{
                "sc_event::notify requires an active fsim SystemC process"};
        }
        detail::check_status(
            detail::current_host->notify_event_mode(
                detail::current_host->context,
                handle_,
                delay.value(),
                kind),
            "notify event");
    }


    sc_event_or_list::sc_event_or_list(const sc_event& event) {
        append(event.native_handle());
    }



    sc_event_or_list& sc_event_or_list::operator|=(const sc_event& event) {
        append(event.native_handle());
        return *this;
    }


    sc_event_or_list& sc_event_or_list::operator|=(const sc_event_or_list& events) {
        for (const auto event : events.events_) {
            append(event);
        }
        return *this;
    }



    [[nodiscard]] const std::vector<fsim_sc_handle_v1>&
    sc_event_or_list::native_handles() const noexcept {
        return events_;
    }

void sc_event_or_list::append(const fsim_sc_handle_v1 event) {
        if (std::find(events_.begin(), events_.end(), event)
            == events_.end()) {
            events_.push_back(event);
        }
    }


    sc_event_and_list::sc_event_and_list(const sc_event& event) {
        append(event.native_handle());
    }



    sc_event_and_list& sc_event_and_list::operator&=(const sc_event& event) {
        append(event.native_handle());
        return *this;
    }


    sc_event_and_list& sc_event_and_list::operator&=(const sc_event_and_list& events) {
        for (const auto event : events.events_) {
            append(event);
        }
        return *this;
    }



    [[nodiscard]] const std::vector<fsim_sc_handle_v1>&
    sc_event_and_list::native_handles() const noexcept {
        return events_;
    }

void sc_event_and_list::append(const fsim_sc_handle_v1 event) {
        if (std::find(events_.begin(), events_.end(), event)
            == events_.end()) {
            events_.push_back(event);
        }
    }

sc_module_name::sc_module_name(const char* name)
        : state_(std::make_shared<State>()) {
        if (name == nullptr || *name == '\0') {
            throw std::invalid_argument{
                "sc_module_name must not be empty"};
        }
        state_->name = name;
        state_->previous_module = detail::current_module;
        state_->previous_cpp_module = detail::current_cpp_module;
        state_->previous_name = detail::current_module_name;
        detail::current_module_name = state_->name.c_str();
        if (detail::current_host == nullptr
            || detail::current_module == 0) {
            return;
        }
        if (detail::construction_root_module
            == detail::current_module) {
            state_->handle = detail::current_module;
            detail::construction_root_module = 0;
            return;
        }
        if (detail::current_host->register_native_module == nullptr) {
            throw std::logic_error{
                "nested SystemC module construction requires an "
                "active hierarchy host"};
        }
        fsim_sc_handle_v1 child = 0;
        detail::check_status(
            detail::current_host->register_native_module(
                detail::current_host->context,
                detail::current_module,
                state_->name.c_str(),
                &child),
            "register native child module");
        if (child == 0) {
            throw std::runtime_error{
                "fsim SystemC host returned an invalid child handle"};
        }
        state_->handle = child;
        state_->restore_module = true;
        detail::current_module = child;
    }



    [[nodiscard]] const char* sc_module_name::c_str() const noexcept {
        return state_->name.c_str();
    }


    [[nodiscard]] sc_module_name::operator const char*() const noexcept {
        return c_str();
    }



    [[nodiscard]] fsim_sc_handle_v1 sc_module_name::native_handle() const noexcept {
        return state_->handle;
    }


    [[nodiscard]] sc_module* sc_module_name::previous_cpp_module() const noexcept {
        return state_->previous_cpp_module;
    }



    [[nodiscard]] bool sc_prim_channel::update_requested() const noexcept {
        return update_requested_;
    }



    void sc_prim_channel::request_update() {
        if (update_requested_) {
            return;
        }
        if (host_ == nullptr || handle_ == 0
            || host_->request_update == nullptr) {
            throw std::logic_error{
                "sc_prim_channel::request_update requires an active "
                "fsim SystemC kernel"};
        }
        detail::check_status(
            host_->request_update(host_->context, handle_),
            "request primitive-channel update");
        update_requested_ = true;
    }



    [[nodiscard]] fsim_sc_handle_v1 sc_prim_channel::native_handle() const noexcept {
        return handle_;
    }

sc_prim_channel::sc_prim_channel()
        : sc_prim_channel(nullptr) {}



    sc_prim_channel::sc_prim_channel(const char* name)
        : host_(detail::current_host),
          handle_(detail::register_primitive_channel(
              name, invoke_update, this)) {}



    void sc_prim_channel::update() {}

void sc_prim_channel::invoke_update(void* user) noexcept {
        auto* channel = static_cast<sc_prim_channel*>(user);
        if (channel == nullptr || channel->host_ == nullptr) {
            return;
        }
        detail::host_scope scope{channel->host_};
        try {
            channel->update();
        } catch (const std::exception& exception) {
            if (channel->host_->report != nullptr) {
                channel->host_->report(
                    channel->host_->context, 3, exception.what());
            }
        } catch (...) {
            if (channel->host_->report != nullptr) {
                channel->host_->report(
                    channel->host_->context,
                    3,
                    "SystemC primitive-channel update threw an "
                    "unknown exception");
            }
        }
        channel->update_requested_ = false;
    }

sc_sensitive::sc_sensitive(sc_module* owner) noexcept : owner_(owner) {}

sc_module::sc_module()
        : sensitive(this),
          name_(
              detail::current_module_name == nullptr
                  ? throw std::logic_error{
                        "sc_module construction requires an active "
                        "sc_module_name"}
                  : detail::current_module_name),
          handle_(detail::current_module) {
        attach_to_current_parent();
    }



    sc_module::sc_module(const sc_module_name name)
        : sensitive(this),
          name_(
              name.c_str() == nullptr
                  ? throw std::invalid_argument{
                        "sc_module_name must not be null"}
                  : name.c_str()),
          handle_(name.native_handle()) {
        attach_to_parent(name.previous_cpp_module());
    }



    [[nodiscard]] const char* sc_module::name() const noexcept { return name_.c_str(); }



    void sc_module::fsim_add_sensitivity(
        const fsim_sc_handle_v1 object,
        const fsim_sc_edge_kind_v1 edge) {
        if (!processes_.empty()) {
            processes_.back().sensitivity.push_back({object, edge});
        }
    }



    void sc_module::dont_initialize() noexcept {
        if (!processes_.empty()) {
            processes_.back().initialize = false;
        }
    }



    [[nodiscard]] fsim_sc_status_v1 sc_module::fsim_elaborate(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module) {
        if (host == nullptr || host->register_process == nullptr
            || host->add_sensitivity == nullptr) {
            return FSIM_SC_ABI_MISMATCH;
        }
        if (handle_ != 0 && handle_ != module) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        for (auto* child : children_) {
            if (child == nullptr || child->handle_ == 0) {
                return FSIM_SC_RUNTIME_ERROR;
            }
            const auto status =
                child->fsim_elaborate(host, child->handle_);
            if (status != FSIM_SC_OK) {
                return status;
            }
        }
        for (auto& process : processes_) {
            process.host = host;
            fsim_sc_handle_v1 handle = 0;
            auto status = host->register_process(
                host->context,
                module,
                process.name.c_str(),
                process.kind,
                invoke_process,
                &process,
                &handle);
            if (status != FSIM_SC_OK) {
                return status;
            }
            for (const auto& sensitivity : process.sensitivity) {
                status = host->add_sensitivity(
                    host->context,
                    handle,
                    sensitivity.object,
                    sensitivity.edge);
                if (status != FSIM_SC_OK) {
                    return status;
                }
            }
            if (!process.initialize) {
                if (host->set_process_initialize == nullptr) {
                    return FSIM_SC_ABI_MISMATCH;
                }
                status = host->set_process_initialize(
                    host->context, handle, 0);
                if (status != FSIM_SC_OK) {
                    return status;
                }
            }
        }
        return FSIM_SC_OK;
    }



    [[nodiscard]] fsim_sc_status_v1 sc_module::fsim_register_lifecycle(
        const fsim_sc_host_v1* host,
        const fsim_sc_handle_v1 module) {
        if (host == nullptr || host->register_lifecycle == nullptr
            || module == 0 || handle_ != module) {
            return FSIM_SC_ABI_MISMATCH;
        }
        lifecycle_host_ = host;
        return host->register_lifecycle(
            host->context,
            module,
            invoke_before_end_of_elaboration,
            invoke_end_of_elaboration,
            invoke_start_of_simulation,
            invoke_end_of_simulation,
            this);
    }

void sc_module::before_end_of_elaboration() {}


    void sc_module::end_of_elaboration() {}


    void sc_module::start_of_simulation() {}


    void sc_module::end_of_simulation() {}



    [[nodiscard]] const std::vector<sc_module::Process>& sc_module::fsim_processes() const noexcept {
        return processes_;
    }



    void sc_module::fsim_invoke_lifecycle(const LifecyclePhase phase) {
        if (phase == LifecyclePhase::end_of_simulation) {
            for (auto child = children_.rbegin();
                 child != children_.rend(); ++child) {
                (*child)->fsim_invoke_lifecycle(phase);
            }
            end_of_simulation();
            return;
        }
        switch (phase) {
        case LifecyclePhase::before_end_of_elaboration:
            before_end_of_elaboration();
            break;
        case LifecyclePhase::end_of_elaboration:
            end_of_elaboration();
            break;
        case LifecyclePhase::start_of_simulation:
            start_of_simulation();
            break;
        case LifecyclePhase::end_of_simulation:
            break;
        }
        for (auto* child : children_) {
            child->fsim_invoke_lifecycle(phase);
        }
    }



    void sc_module::invoke_lifecycle(
        void* user,
        const LifecyclePhase phase,
        const char* unknown_failure) noexcept {
        auto* module = static_cast<sc_module*>(user);
        if (module == nullptr || module->lifecycle_host_ == nullptr) {
            return;
        }
        detail::host_scope scope{module->lifecycle_host_};
        try {
            module->fsim_invoke_lifecycle(phase);
        } catch (const std::exception& exception) {
            if (module->lifecycle_host_->report != nullptr) {
                module->lifecycle_host_->report(
                    module->lifecycle_host_->context,
                    3,
                    exception.what());
            }
        } catch (...) {
            if (module->lifecycle_host_->report != nullptr) {
                module->lifecycle_host_->report(
                    module->lifecycle_host_->context,
                    3,
                    unknown_failure);
            }
        }
    }



    void sc_module::invoke_before_end_of_elaboration(
        void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::before_end_of_elaboration,
            "SystemC before_end_of_elaboration callback threw an "
            "unknown exception");
    }



    void sc_module::invoke_end_of_elaboration(void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::end_of_elaboration,
            "SystemC end_of_elaboration callback threw an "
            "unknown exception");
    }



    void sc_module::invoke_start_of_simulation(void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::start_of_simulation,
            "SystemC start_of_simulation callback threw an "
            "unknown exception");
    }



    void sc_module::invoke_end_of_simulation(void* user) noexcept {
        invoke_lifecycle(
            user,
            LifecyclePhase::end_of_simulation,
            "SystemC end_of_simulation callback threw an "
            "unknown exception");
    }



    void sc_module::attach_to_current_parent() {
        attach_to_parent(detail::current_cpp_module);
    }



    void sc_module::attach_to_parent(sc_module* parent) {
        if (parent != nullptr) {
            parent->children_.push_back(this);
        }
        detail::current_cpp_module = this;
    }



    void sc_module::invoke_process(void* user) noexcept {
        auto* process = static_cast<Process*>(user);
        if (process == nullptr || process->host == nullptr) {
            return;
        }
        detail::host_scope scope{process->host};
        const auto previous_kind = detail::current_process_kind;
        detail::current_process_kind = process->kind;
        try {
            process->entry();
        } catch (const std::exception& exception) {
            if (process->host->report != nullptr) {
                process->host->report(
                    process->host->context, 3, exception.what());
            }
        } catch (...) {
            if (process->host->report != nullptr) {
                process->host->report(
                    process->host->context,
                    3,
                    "SystemC process threw an unknown exception");
            }
        }
        detail::current_process_kind = previous_kind;
    }

} // namespace sc_core
