// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc.hpp"

namespace fsim::systemc {

hdl_instance::hdl_instance(const char* name)
    : host_(sc_core::detail::current_host) {
    if (host_ == nullptr || name == nullptr || *name == '\0'
        || sc_core::detail::current_module == 0
        || host_->register_foreign_child == nullptr
        || host_->connect_foreign_port == nullptr) {
        throw std::logic_error{
            "hdl_instance construction requires an active fsim "
            "SystemC hierarchy host"};
    }
    sc_core::detail::check_status(
        host_->register_foreign_child(
            host_->context,
            sc_core::detail::current_module,
            name,
            &handle_),
        "register HDL child instance");
    if (handle_ == 0) {
        throw std::runtime_error{
            "fsim SystemC host returned an invalid HDL child handle"};
    }
}

void hdl_instance::set_actual(
    const char* name, const std::int64_t value) {
    if (name == nullptr || *name == '\0'
        || host_->set_foreign_child_actual == nullptr) {
        throw std::invalid_argument{
            "HDL child construction actual name must be valid"};
    }
    sc_core::detail::check_status(
        host_->set_foreign_child_actual(
            host_->context, handle_, name, value),
        "set HDL child construction actual");
}

fsim_sc_handle_v1 hdl_instance::native_handle() const noexcept {
    return handle_;
}

} // namespace fsim::systemc
