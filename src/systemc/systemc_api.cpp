// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc.hpp"

namespace sc_core {

void detail::bind_host(const fsim_sc_host_v1* host) noexcept{
    current_host = host;
}

void detail::check_status(const fsim_sc_status_v1 status, const char* operation){
    if (status != FSIM_SC_OK) {
        throw std::runtime_error{std::string{"fsim SystemC host failed to "} + operation};
    }
}

sc_event_or_list operator|(
    const sc_event& left, const sc_event& right){
    sc_event_or_list result{left};
    result |= right;
    return result;
}

sc_event_or_list operator|(
    sc_event_or_list left, const sc_event& right){
    left |= right;
    return left;
}

sc_event_or_list operator|(
    const sc_event& left, sc_event_or_list right){
    sc_event_or_list result{left};
    result |= right;
    return result;
}

sc_event_or_list operator|(
    sc_event_or_list left, const sc_event_or_list& right){
    left |= right;
    return left;
}

sc_event_and_list operator&(
    const sc_event& left, const sc_event& right){
    sc_event_and_list result{left};
    result &= right;
    return result;
}

sc_event_and_list operator&(
    sc_event_and_list left, const sc_event& right){
    left &= right;
    return left;
}

sc_event_and_list operator&(
    const sc_event& left, sc_event_and_list right){
    sc_event_and_list result{left};
    result &= right;
    return result;
}

sc_event_and_list operator&(
    sc_event_and_list left, const sc_event_and_list& right){
    left &= right;
    return left;
}

void detail::set_event_list_trigger(
    const std::vector<fsim_sc_handle_v1>& events,
    const fsim_sc_event_list_kind_v1 kind,
    const char* operation){
    if (current_host == nullptr
        || current_host->wait_event_list == nullptr) {
        throw std::logic_error{
            std::string{"sc_core::"} + operation
            + " requires an active fsim SystemC process"};
    }
    check_status(
        current_host->wait_event_list(
            current_host->context,
            events.data(),
            events.size(),
            kind),
        operation);
}

void wait(const sc_time delay){
    if (detail::current_host == nullptr || detail::current_host->wait_time == nullptr) {
        throw std::logic_error{"sc_core::wait requires an active fsim SystemC process"};
    }
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::check_status(
        detail::current_host->wait_time(detail::current_host->context, delay.value()),
        "wait for time");
}

void wait(const sc_event& event){
    if (detail::current_host == nullptr || detail::current_host->wait_event == nullptr) {
        throw std::logic_error{"sc_core::wait requires an active fsim SystemC process"};
    }
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::check_status(
        detail::current_host->wait_event(
            detail::current_host->context, event.native_handle()),
        "wait for event");
}

void wait(const sc_event_or_list& events){
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_OR_LIST,
        "wait for event OR list");
}

void wait(const sc_event_and_list& events){
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_AND_LIST,
        "wait for event AND list");
}

void wait(){
    if (detail::current_host == nullptr
        || detail::current_host->wait_static == nullptr) {
        throw std::logic_error{
            "plain wait() requires an active fsim SystemC thread"};
    }
    if (detail::current_process_kind == FSIM_SC_METHOD) {
        throw std::logic_error{
            "SC_METHOD cannot call wait; use next_trigger"};
    }
    detail::check_status(
        detail::current_host->wait_static(
            detail::current_host->context),
        "wait on static sensitivity");
}

void next_trigger(const sc_time delay){
    if (detail::current_host == nullptr
        || detail::current_host->wait_time == nullptr) {
        throw std::logic_error{
            "sc_core::next_trigger requires an active fsim SystemC process"};
    }
    detail::check_status(
        detail::current_host->wait_time(
            detail::current_host->context, delay.value()),
        "set next time trigger");
}

void next_trigger(const sc_event& event){
    if (detail::current_host == nullptr
        || detail::current_host->wait_event == nullptr) {
        throw std::logic_error{
            "sc_core::next_trigger requires an active fsim SystemC process"};
    }
    detail::check_status(
        detail::current_host->wait_event(
            detail::current_host->context, event.native_handle()),
        "set next event trigger");
}

void next_trigger(const sc_event_or_list& events){
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_OR_LIST,
        "set next event OR trigger");
}

void next_trigger(const sc_event_and_list& events){
    detail::set_event_list_trigger(
        events.native_handles(),
        FSIM_SC_EVENT_AND_LIST,
        "set next event AND trigger");
}

const char* sc_gen_unique_name(const char* base){
    if (base == nullptr) {
        throw std::invalid_argument{"sc_gen_unique_name base must not be null"};
    }
    static thread_local std::uint64_t counter = 0;
    static thread_local std::deque<std::string> names;
    names.emplace_back(std::string{base} + "_" + std::to_string(counter++));
    return names.back().c_str();
}

fsim_sc_handle_v1 detail::register_event(const char* name){
    if (current_host == nullptr || current_module == 0) {
        return 0;
    }
    if (current_host->register_event == nullptr) {
        throw std::logic_error{
            "SystemC event requires an event-capable elaboration host"};
    }
    fsim_sc_handle_v1 handle = 0;
    check_status(
        current_host->register_event(
            current_host->context,
            current_module,
            name,
            &handle),
        "register event");
    return handle;
}

fsim_sc_handle_v1 detail::register_primitive_channel(
    const char* name,
    const fsim_sc_channel_update_v1 update,
    void* user){
    if (current_host == nullptr || current_module == 0) {
        return 0;
    }
    if (current_host->register_primitive_channel == nullptr
        || update == nullptr || user == nullptr) {
        throw std::logic_error{
            "SystemC primitive channel requires a channel-capable "
            "elaboration host"};
    }
    fsim_sc_handle_v1 handle = 0;
    check_status(
        current_host->register_primitive_channel(
            current_host->context,
            current_module,
            name,
            update,
            user,
            &handle),
        "register primitive channel");
    return handle;
}

bool detail::object_event(const fsim_sc_handle_v1 object){
    if (current_host == nullptr || current_host->value_changed == nullptr
        || object == 0) {
        throw std::logic_error{
            "SystemC signal event query requires an active process"};
    }
    std::uint8_t result = 0;
    check_status(
        current_host->value_changed(
            current_host->context, object, &result),
        "query signal event");
    return result != 0;
}

void detail::bind_port(
    const fsim_sc_handle_v1 port,
    const fsim_sc_handle_v1 channel){
    if (port == 0 || channel == 0) {
        return;
    }
    if (current_host == nullptr || current_host->bind_port == nullptr) {
        throw std::logic_error{
            "SystemC port binding requires an active elaboration host"};
    }
    check_status(
        current_host->bind_port(
            current_host->context, port, channel),
        "bind port to channel");
}

void detail::bind_export(
    const fsim_sc_handle_v1 export_handle,
    const fsim_sc_handle_v1 target){
    if (export_handle == 0 || target == 0) {
        return;
    }
    if (current_host == nullptr
        || current_host->bind_export == nullptr) {
        throw std::logic_error{
            "SystemC export binding requires an active elaboration host"};
    }
    check_status(
        current_host->bind_export(
            current_host->context, export_handle, target),
        "bind export");
}

} // namespace sc_core
