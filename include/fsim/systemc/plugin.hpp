// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/channels.hpp"

#include "fsim/systemc/marshalling.hpp"

#include <concepts>

namespace fsim::systemc {

/// An elaboration-time placeholder for a VHDL or Verilog/SystemVerilog child.
///
/// Construct this as a member of an sc_module, connect its typed ports to
/// objects registered on that module, and select the actual HDL design unit
/// with an explicit binding for the resulting hierarchy path in fsim.toml.
/// @deprecated Use SC_FSIM_HDL_MODULE and ordinary SystemC port binding.
class hdl_instance final {
public:
    explicit hdl_instance(const char* name);

    hdl_instance(const hdl_instance&) = delete;
    hdl_instance& operator=(const hdl_instance&) = delete;

    /// Supply one named, immutable scalar construction actual to the
    /// manifest-selected VHDL or Verilog/SystemVerilog child.
    void set_actual(const char* name, std::int64_t value);

    template <typename T>
    void bind_input(
        const char* name, const sc_core::sc_in<T>& object) {
        connect<T>(name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename T>
    void bind_input(
        const char* name, const sc_core::sc_inout<T>& object) {
        connect<T>(name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename T>
    void bind_input(
        const char* name,
        const sc_core::sc_signal_in_if<T>& object) {
        connect<T>(name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename Interface>
        requires sc_core::detail::
            signal_interface_traits<Interface>::supported
    void bind_input(
        const char* name,
        const sc_core::sc_export<Interface>& object) {
        using value_type = typename sc_core::detail::
            signal_interface_traits<Interface>::value_type;
        connect<value_type>(
            name, FSIM_SC_INPUT, object.native_handle());
    }

    template <typename T>
    void bind_output(
        const char* name, const sc_core::sc_out<T>& object) {
        connect<T>(name, FSIM_SC_OUTPUT, object.native_handle());
    }

    template <typename T>
    void bind_output(
        const char* name,
        const sc_core::sc_signal_inout_if<T>& object) {
        connect<T>(name, FSIM_SC_OUTPUT, object.native_handle());
    }

    template <typename Interface>
        requires (
            sc_core::detail::
                signal_interface_traits<Interface>::supported
            && sc_core::detail::
                signal_interface_traits<Interface>::writable)
    void bind_output(
        const char* name,
        const sc_core::sc_export<Interface>& object) {
        using value_type = typename sc_core::detail::
            signal_interface_traits<Interface>::value_type;
        connect<value_type>(
            name, FSIM_SC_OUTPUT, object.native_handle());
    }

    template <typename T>
    void bind_inout(
        const char* name, const sc_core::sc_inout<T>& object) {
        connect<T>(name, FSIM_SC_INOUT, object.native_handle());
    }

    template <typename T>
    void bind_inout(
        const char* name,
        const sc_core::sc_signal_inout_if<T>& object) {
        connect<T>(name, FSIM_SC_INOUT, object.native_handle());
    }

    template <typename Interface>
        requires (
            sc_core::detail::
                signal_interface_traits<Interface>::supported
            && sc_core::detail::
                signal_interface_traits<Interface>::writable)
    void bind_inout(
        const char* name,
        const sc_core::sc_export<Interface>& object) {
        using value_type = typename sc_core::detail::
            signal_interface_traits<Interface>::value_type;
        connect<value_type>(
            name, FSIM_SC_INOUT, object.native_handle());
    }

    [[nodiscard]] fsim_sc_handle_v1
    native_handle() const noexcept;

private:
    template <typename T>
    void connect(
        const char* name,
        const fsim_sc_port_direction_v1 direction,
        const fsim_sc_handle_v1 object) {
        using traits =
            sc_core::detail::value_traits<std::remove_cv_t<T>>;
        static_assert(
            traits::supported,
            "this fsim SystemC value type is not supported");
        if (name == nullptr || *name == '\0' || object == 0) {
            throw std::invalid_argument{
                "HDL child port name and bound object must be valid"};
        }
        sc_core::detail::check_status(
            host_->connect_foreign_port(
                host_->context,
                handle_,
                name,
                direction,
                traits::encoding,
                traits::width,
                object),
            "connect HDL child port");
    }

    const fsim_sc_host_v1* host_{};
    fsim_sc_handle_v1 handle_{};
};

/// A module-shaped elaboration proxy whose implementation is selected by an
/// explicit full-instance-path HDL binding in the fsim manifest.
///
/// Declare only sc_in, sc_out, and sc_inout members in a derived proxy. Bind
/// those ports with ordinary SystemC syntax from the containing module.
class hdl_module : public sc_core::sc_module {
public:
    hdl_module();
    explicit hdl_module(sc_core::sc_module_name name);

    hdl_module(const hdl_module&) = delete;
    hdl_module& operator=(const hdl_module&) = delete;

    /// Supply one named immutable scalar actual to the manifest-selected HDL
    /// design unit.
    void set_actual(const char* name, std::int64_t value);

protected:
    explicit hdl_module(const char* implementation);

private:
    void mark(const char* implementation = nullptr);

    void before_end_of_elaboration() final {}
    void end_of_elaboration() final {}
    void start_of_simulation() final {}
    void end_of_simulation() final {}

    const fsim_sc_host_v1* host_{};
    fsim_sc_handle_v1 handle_{};
};

template <std::size_t Size>
struct hdl_implementation_name {
    char value[Size]{};

    consteval hdl_implementation_name(const char (&name)[Size]) {
        for (std::size_t index = 0; index < Size; ++index) {
            value[index] = name[index];
        }
    }
};

template <hdl_implementation_name Implementation>
class hdl_module_type : public hdl_module {
public:
    hdl_module_type()
        : hdl_module(Implementation.value) {}
};

struct factory_parameter {
    const char* name{};
    fsim_sc_construction_type_v1 type{
        FSIM_SC_CONSTRUCTION_INTEGER};
    bool has_default{};
    std::int64_t default_value{};
};

template <typename... Parameters>
    requires (
        std::same_as<std::remove_cvref_t<Parameters>,
                     factory_parameter> && ...)
[[nodiscard]] constexpr auto make_factory_parameters(
    Parameters&&... parameters) {
    return std::array<factory_parameter, sizeof...(Parameters)>{
        std::forward<Parameters>(parameters)...};
}

/// Read a canonical value declared by the active factory's construction
/// schema. This is valid only while an fsim module constructor is running.
template <typename T>
[[nodiscard]] T construction_value(const char* name) {
    static_assert(
        std::is_integral_v<T>,
        "SystemC construction values currently support integral types");
    const auto* host = sc_core::detail::current_host;
    if (host == nullptr || sc_core::detail::current_module == 0
        || name == nullptr || *name == '\0'
        || host->get_construction_value == nullptr) {
        throw std::logic_error{
            "construction_value requires an active typed fsim factory"};
    }
    std::int64_t value{};
    sc_core::detail::check_status(
        host->get_construction_value(
            host->context,
            sc_core::detail::current_module,
            name,
            &value),
        "read SystemC construction value");
    if constexpr (std::is_same_v<T, bool>) {
        if (value != 0 && value != 1) {
            throw std::out_of_range{
                "Boolean construction value is not 0 or 1"};
        }
    } else if constexpr (std::is_signed_v<T>) {
        if (value < static_cast<std::int64_t>(
                        std::numeric_limits<T>::min())
            || value > static_cast<std::int64_t>(
                           std::numeric_limits<T>::max())) {
            throw std::out_of_range{
                "SystemC construction value does not fit target type"};
        }
    } else {
        if (value < 0
            || static_cast<std::uint64_t>(value)
                > static_cast<std::uint64_t>(
                    std::numeric_limits<T>::max())) {
            throw std::out_of_range{
                "SystemC construction value does not fit target type"};
        }
    }
    return static_cast<T>(value);
}

template <typename Module>
struct module_factory_state {
    static fsim_sc_status_v1 elaborate(
        void* user,
        const char* instance_name,
        const fsim_sc_handle_v1 module,
        fsim_sc_handle_v1,
        void** result) noexcept {
        static_assert(
            std::is_base_of_v<sc_core::sc_module, Module>,
            "registered SystemC module must derive from sc_module");
        const auto* host =
            static_cast<const fsim_sc_host_v1*>(user);
        if (host == nullptr || instance_name == nullptr
            || *instance_name == '\0' || result == nullptr) {
            return FSIM_SC_INVALID_ARGUMENT;
        }
        try {
            sc_core::detail::host_scope scope{host, module};
            sc_core::detail::module_construction_scope
                construction{module};
            auto object = std::make_unique<Module>(
                sc_core::sc_module_name{instance_name});
            const auto status = object->fsim_elaborate(host, module);
            if (status != FSIM_SC_OK) {
                return status;
            }
            const auto lifecycle_status =
                object->fsim_register_lifecycle(host, module);
            if (lifecycle_status != FSIM_SC_OK) {
                return lifecycle_status;
            }
            *result = object.release();
            return FSIM_SC_OK;
        } catch (const std::exception& exception) {
            if (host->report != nullptr) {
                host->report(host->context, 3, exception.what());
            }
            return FSIM_SC_RUNTIME_ERROR;
        } catch (...) {
            if (host->report != nullptr) {
                host->report(
                    host->context,
                    3,
                    "unknown SystemC module-construction failure");
            }
            return FSIM_SC_RUNTIME_ERROR;
        }
    }

    static void destroy(void*, void* module) noexcept {
        delete static_cast<Module*>(module);
    }
};

template <typename Module>
/// @deprecated New plug-ins should use SC_FSIM_EXPORT or SC_FSIM_EXPORT_AS.
[[nodiscard]] fsim_sc_status_v1 register_module_factory(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar,
    const char* name,
    const std::span<const factory_parameter> parameters) noexcept {
    if (host == nullptr || registrar == nullptr || name == nullptr
        || *name == '\0'
        || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host->struct_size < sizeof(fsim_sc_host_v1)
        || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
        || registrar->register_elaboration_factory == nullptr
        || registrar->register_factory_parameter == nullptr
        || host->register_port == nullptr
        || host->register_process == nullptr
        || host->add_sensitivity == nullptr
        || host->read_value == nullptr
        || host->write_value == nullptr
        || host->report == nullptr
        || host->set_process_initialize == nullptr
        || host->register_event == nullptr
        || host->notify_event_mode == nullptr
        || host->cancel_event == nullptr
        || host->wait_event_list == nullptr
        || host->notify_event_delayed == nullptr
        || host->register_primitive_channel == nullptr
        || host->request_update == nullptr
        || host->register_signal == nullptr
        || host->value_changed == nullptr
        || host->bind_port == nullptr
        || host->register_native_module == nullptr
        || host->register_lifecycle == nullptr
        || host->register_export == nullptr
        || host->bind_export == nullptr
        || host->register_foreign_child == nullptr
        || host->connect_foreign_port == nullptr
        || host->wait_static == nullptr
        || host->set_foreign_child_actual == nullptr
        || host->get_construction_value == nullptr
        || host->mark_hdl_module == nullptr
        || host->set_hdl_module_actual == nullptr
        || host->set_hdl_module_implementation == nullptr) {
        return FSIM_SC_ABI_MISMATCH;
    }
    auto status = registrar->register_elaboration_factory(
        registrar->context,
        name,
        module_factory_state<Module>::elaborate,
        module_factory_state<Module>::destroy,
        const_cast<fsim_sc_host_v1*>(host));
    if (status != FSIM_SC_OK) {
        return status;
    }
    for (const auto& parameter : parameters) {
        status = registrar->register_factory_parameter(
            registrar->context,
            name,
            parameter.name,
            parameter.type,
            parameter.has_default ? 1 : 0,
            parameter.default_value);
        if (status != FSIM_SC_OK) {
            return status;
        }
    }
    return FSIM_SC_OK;
}

template <typename Module>
/// @deprecated New plug-ins should use SC_FSIM_EXPORT or SC_FSIM_EXPORT_AS.
[[nodiscard]] fsim_sc_status_v1 register_module_factory(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar,
    const char* name) noexcept {
    return register_module_factory<Module>(
        host, registrar, name, {});
}

namespace detail {

template <typename Module>
[[nodiscard]] fsim_sc_status_v1 register_exported_factory(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar,
    const char* name,
    const std::span<const factory_parameter> parameters) noexcept {
    if (host == nullptr || registrar == nullptr || name == nullptr
        || *name == '\0'
        || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
        || host->struct_size < sizeof(fsim_sc_host_v1)
        || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
        || registrar->register_elaboration_factory == nullptr
        || registrar->register_factory_parameter == nullptr) {
        return FSIM_SC_ABI_MISMATCH;
    }
    auto status = registrar->register_elaboration_factory(
        registrar->context,
        name,
        module_factory_state<Module>::elaborate,
        module_factory_state<Module>::destroy,
        const_cast<fsim_sc_host_v1*>(host));
    for (const auto& parameter : parameters) {
        if (status != FSIM_SC_OK) {
            break;
        }
        status = registrar->register_factory_parameter(
            registrar->context,
            name,
            parameter.name,
            parameter.type,
            parameter.has_default ? 1 : 0,
            parameter.default_value);
    }
    return status;
}

struct export_descriptor {
    const char* public_name{};
    fsim_sc_status_v1 (*register_factory)(
        const fsim_sc_host_v1*,
        fsim_sc_registrar_v1*,
        const char*) noexcept {};
    export_descriptor* next{};
};

void add_export_descriptor(export_descriptor* descriptor) noexcept;

template <typename Module>
struct export_registration final {
    explicit export_registration(const char* public_name) noexcept
        : descriptor_{
              public_name,
              register_export,
              nullptr} {
        add_export_descriptor(&descriptor_);
    }

private:
    static fsim_sc_status_v1 register_export(
        const fsim_sc_host_v1* host,
        fsim_sc_registrar_v1* registrar,
        const char* public_name) noexcept {
        if constexpr (requires {
                          Module::fsim_factory_parameters;
                      }) {
            const auto& parameters =
                Module::fsim_factory_parameters;
            return register_exported_factory<Module>(
                host,
                registrar,
                public_name,
                std::span<const factory_parameter>{parameters});
        } else {
            return register_exported_factory<Module>(
                host, registrar, public_name, {});
        }
    }

    export_descriptor descriptor_;
};

} // namespace detail

} // namespace fsim::systemc

#define SC_MODULE(name) struct name : public ::sc_core::sc_module
#define SC_FSIM_HDL_MODULE(name) \
    struct name \
        : public ::fsim::systemc::hdl_module_type< \
              ::fsim::systemc::hdl_implementation_name{#name}>
#define SC_CTOR(name) \
    explicit name( \
        [[maybe_unused]] ::sc_core::sc_module_name fsim_module_name)
#define SC_HAS_PROCESS(name)
#define SC_METHOD(function_name) \
    this->fsim_register_process( \
        #function_name, FSIM_SC_METHOD, [this]() { this->function_name(); })
#define SC_THREAD(function_name) \
    this->fsim_register_process( \
        #function_name, FSIM_SC_THREAD, [this]() { this->function_name(); })
#define SC_CTHREAD(function_name, edge_expression) \
    do { \
        this->fsim_register_process( \
            #function_name, FSIM_SC_CTHREAD, [this]() { this->function_name(); }); \
        this->sensitive << (edge_expression); \
    } while (false)

#define FSIM_SC_DETAIL_CONCAT_INNER(left, right) left##right
#define FSIM_SC_DETAIL_CONCAT(left, right) \
    FSIM_SC_DETAIL_CONCAT_INNER(left, right)
#define FSIM_SC_DETAIL_EXPORT(type, public_name, identifier) \
    namespace { \
    [[maybe_unused]] const \
        ::fsim::systemc::detail::export_registration<type> \
        FSIM_SC_DETAIL_CONCAT( \
            fsim_sc_export_registration_, identifier){public_name}; \
    }
#define SC_FSIM_EXPORT_AS(type, public_name) \
    FSIM_SC_DETAIL_EXPORT(type, public_name, __LINE__)
#define SC_FSIM_EXPORT(type) SC_FSIM_EXPORT_AS(type, #type)
