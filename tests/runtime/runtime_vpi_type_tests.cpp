// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_object.hpp"

#include <stdexcept>

namespace fsim::tests::runtime {

namespace {

    void require_vpi_type(const bool condition, const char* const message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    fsim::runtime::SystemVerilogVpiTypeInfo scope_type(
        const fsim::runtime::SystemVerilogVpiLanguage language)
    {
        fsim::runtime::SystemVerilogVpiTypeInfo result;
        result.language = language;
        return result;
    }

} // namespace

void test_systemverilog_vpi_scalar_type_properties()
{
    using fsim::runtime::SystemVerilogVpiDirection;
    using fsim::runtime::SystemVerilogVpiLanguage;
    using fsim::runtime::SystemVerilogVpiLifetime;
    using fsim::runtime::SystemVerilogVpiNetKind;
    using fsim::runtime::SystemVerilogVpiObjectDescriptor;
    using fsim::runtime::SystemVerilogVpiObjectError;
    using fsim::runtime::SystemVerilogVpiObjectKind;
    using fsim::runtime::SystemVerilogVpiObjectRegistry;
    using fsim::runtime::SystemVerilogVpiTypeInfo;
    using fsim::runtime::SystemVerilogVpiValueCategory;

    SystemVerilogVpiObjectRegistry registry { 501 };
    const auto root = registry.create(SystemVerilogVpiObjectKind::Root, 0, "top");

    auto module_type = scope_type(SystemVerilogVpiLanguage::Verilog2005);
    const auto module = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Module,
        root.value,
        "legacy",
        std::nullopt,
        module_type,
    });
    const auto interface = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Interface,
        root.value,
        "bus_if",
        std::nullopt,
        scope_type(SystemVerilogVpiLanguage::SystemVerilog2017),
    });
    const auto program = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Program,
        root.value,
        "test",
        std::nullopt,
        scope_type(SystemVerilogVpiLanguage::SystemVerilog2017),
    });
    const auto package = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Package,
        root.value,
        "types_pkg",
        std::nullopt,
        scope_type(SystemVerilogVpiLanguage::SystemVerilog2017),
    });
    const auto generate = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::GenerateScope,
        module.value,
        "genblk[0]",
        std::nullopt,
        module_type,
    });
    require_vpi_type(
        module && interface && program && package && generate,
        "VPI type metadata accepts every Change 5 scope kind");

    SystemVerilogVpiTypeInfo port_type;
    port_type.category = SystemVerilogVpiValueCategory::Logic4;
    port_type.net_kind = SystemVerilogVpiNetKind::Wire;
    port_type.direction = SystemVerilogVpiDirection::Input;
    port_type.width = 8;
    const auto port = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Port,
        module.value,
        "data",
        std::nullopt,
        port_type,
    });

    auto net_type = port_type;
    net_type.direction = SystemVerilogVpiDirection::None;
    const auto net = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Net,
        module.value,
        "wire_data",
        std::nullopt,
        net_type,
    });

    SystemVerilogVpiTypeInfo variable_type;
    variable_type.category = SystemVerilogVpiValueCategory::Integer4;
    variable_type.lifetime = SystemVerilogVpiLifetime::Automatic;
    variable_type.width = 32;
    variable_type.is_signed = true;
    const auto variable = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Variable,
        program.value,
        "counter",
        std::nullopt,
        variable_type,
    });

    auto parameter_type = variable_type;
    parameter_type.lifetime = SystemVerilogVpiLifetime::Static;
    parameter_type.width = 16;
    parameter_type.is_constant = true;
    const auto parameter = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Parameter,
        module.value,
        "WIDTH",
        std::nullopt,
        parameter_type,
    });

    auto driver_type = net_type;
    driver_type.net_kind = SystemVerilogVpiNetKind::None;
    driver_type.is_constant = true;
    driver_type.driver_range
        = fsim::runtime::SystemVerilogVpiDriverRange { 2, 3, false };
    const auto driver = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Driver,
        net.value,
        "$driver_0",
        std::nullopt,
        driver_type,
    });

    SystemVerilogVpiTypeInfo event_type;
    event_type.category = SystemVerilogVpiValueCategory::Event;
    const auto event = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::NamedEvent,
        program.value,
        "done",
        std::nullopt,
        event_type,
    });
    require_vpi_type(
        port && net && variable && parameter && driver && event,
        "VPI type metadata accepts scalar ports, nets, variables, parameters, and events");

    const auto port_properties = registry.type_info(port.value);
    const auto net_properties = registry.type_info(net.value);
    const auto variable_properties = registry.type_info(variable.value);
    const auto parameter_properties = registry.type_info(parameter.value);
    const auto event_properties = registry.type_info(event.value);
    const auto driver_properties = registry.type_info(driver.value);
    require_vpi_type(
        port_properties
            && port_properties.value->language
                == SystemVerilogVpiLanguage::SystemVerilog2017
            && port_properties.value->category
                == SystemVerilogVpiValueCategory::Logic4
            && port_properties.value->net_kind
                == SystemVerilogVpiNetKind::Wire
            && port_properties.value->direction
                == SystemVerilogVpiDirection::Input
            && port_properties.value->width == 8
            && !port_properties.value->is_signed,
        "VPI port properties preserve language, category, net kind, direction, width, and signedness");
    require_vpi_type(
        net_properties
            && net_properties.value->net_kind == SystemVerilogVpiNetKind::Wire
            && net_properties.value->direction
                == SystemVerilogVpiDirection::None
            && variable_properties
            && variable_properties.value->lifetime
                == SystemVerilogVpiLifetime::Automatic
            && variable_properties.value->is_signed
            && parameter_properties
            && parameter_properties.value->is_constant
            && parameter_properties.value->width == 16
            && driver_properties && driver_properties.value->driver_range
            && *driver_properties.value->driver_range
                == fsim::runtime::SystemVerilogVpiDriverRange { 2, 3, false }
            && event_properties
            && event_properties.value->category
                == SystemVerilogVpiValueCategory::Event,
        "VPI scalar property queries preserve net, lifetime, constant, and event identity");
    require_vpi_type(
        registry.type_info(module.value).value->language
                == SystemVerilogVpiLanguage::Verilog2005
            && registry.type_info(interface.value).value->language
                == SystemVerilogVpiLanguage::SystemVerilog2017,
        "VPI scope properties preserve exact Verilog/SystemVerilog ownership");

    const auto expect_invalid = [&](const SystemVerilogVpiObjectKind kind,
                                    const char* const name,
                                    const SystemVerilogVpiTypeInfo& type) {
        return registry.create(SystemVerilogVpiObjectDescriptor {
            kind, root.value, name, std::nullopt, type });
    };
    require_vpi_type(
        expect_invalid(
            SystemVerilogVpiObjectKind::Interface,
            "legacy_if",
            scope_type(SystemVerilogVpiLanguage::Verilog2005))
                .error
            == SystemVerilogVpiObjectError::InvalidType,
        "VPI types reject Verilog ownership for SystemVerilog-only scopes");

    auto missing_direction = port_type;
    missing_direction.direction = SystemVerilogVpiDirection::None;
    auto missing_net_kind = net_type;
    missing_net_kind.net_kind = SystemVerilogVpiNetKind::None;
    auto mutable_parameter = parameter_type;
    mutable_parameter.is_constant = false;
    auto signed_real = variable_type;
    signed_real.category = SystemVerilogVpiValueCategory::Real;
    signed_real.width = 64;
    auto invalid_driver_range = driver_type;
    invalid_driver_range.driver_range
        = fsim::runtime::SystemVerilogVpiDriverRange { 7, 2, false };
    require_vpi_type(
        expect_invalid(
            SystemVerilogVpiObjectKind::Port,
            "bad_port",
            missing_direction)
                    .error
                == SystemVerilogVpiObjectError::InvalidType
            && expect_invalid(
                   SystemVerilogVpiObjectKind::Net,
                   "bad_net",
                   missing_net_kind)
                    .error
                == SystemVerilogVpiObjectError::InvalidType
            && expect_invalid(
                   SystemVerilogVpiObjectKind::Parameter,
                   "bad_parameter",
                   mutable_parameter)
                    .error
                == SystemVerilogVpiObjectError::InvalidType
            && expect_invalid(
                   SystemVerilogVpiObjectKind::Variable,
                   "bad_real",
                   signed_real)
                    .error
                == SystemVerilogVpiObjectError::InvalidType
            && expect_invalid(
                   SystemVerilogVpiObjectKind::Driver,
                   "bad_driver",
                   invalid_driver_range)
                    .error
                == SystemVerilogVpiObjectError::InvalidType,
        "VPI type validation rejects malformed direction, net, constant, and signed-real profiles");

    SystemVerilogVpiObjectRegistry other { 502 };
    const auto other_root = other.create(SystemVerilogVpiObjectKind::Root, 0, "other");
    require_vpi_type(
        other.type_info(port.value).error
                == SystemVerilogVpiObjectError::CrossSimulation
            && registry.type_info(other_root.value).error
                == SystemVerilogVpiObjectError::CrossSimulation,
        "VPI property queries preserve multi-simulation isolation");
}

} // namespace fsim::tests::runtime
