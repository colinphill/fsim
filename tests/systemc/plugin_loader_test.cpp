// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/plugin_loader.hpp"
#include "fsim/systemc/hierarchy.hpp"

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool factory_registered = false;
bool elaboration_factory_registered = false;
bool factory_parameter_registered = false;

fsim_sc_status_v1 register_factory(
    void*,
    const char* name,
    fsim_sc_module_factory_v1 factory,
    fsim_sc_module_destroy_v1 destroy,
    void*) {
    factory_registered =
        std::string{name} == "sample" && factory != nullptr && destroy != nullptr;
    return factory_registered ? FSIM_SC_OK : FSIM_SC_INVALID_ARGUMENT;
}

fsim_sc_status_v1 register_elaboration_factory(
    void*,
    const char* name,
    fsim_sc_module_elaborate_v1 factory,
    fsim_sc_module_destroy_v1 destroy,
    void*) {
    elaboration_factory_registered =
        std::string{name} == "bridge"
        && factory != nullptr && destroy != nullptr;
    return elaboration_factory_registered
        ? FSIM_SC_OK
        : FSIM_SC_INVALID_ARGUMENT;
}

fsim_sc_status_v1 register_factory_parameter(
    void*,
    const char* factory,
    const char* name,
    fsim_sc_construction_type_v1 type,
    std::uint8_t has_default,
    std::int64_t default_value) {
    factory_parameter_registered =
        std::string{factory} == "bridge"
        && std::string{name} == "WIDTH"
        && type == FSIM_SC_CONSTRUCTION_POSITIVE
        && has_default == 1
        && default_value == 8;
    return factory_parameter_registered
        ? FSIM_SC_OK
        : FSIM_SC_INVALID_ARGUMENT;
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 3);

    fsim_sc_host_v1 host{};
    host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    // Model a future caller with larger tables. The v1 loader copies only the
    // known prefix, so it must clamp the size advertised to a v1 plug-in.
    host.struct_size =
        static_cast<decltype(host.struct_size)>(sizeof(host) + 64);

    fsim_sc_registrar_v1 registrar{};
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size =
        static_cast<decltype(registrar.struct_size)>(
            sizeof(registrar) + 64);
    registrar.register_factory = register_factory;
    registrar.register_elaboration_factory =
        register_elaboration_factory;
    registrar.register_factory_parameter =
        register_factory_parameter;

    std::string error;
    const auto plugin =
        fsim::systemc::Plugin::load(std::filesystem::path{argv[1]}, host, registrar, error);
    assert(plugin != nullptr);
    assert(error.empty());
    assert(factory_registered);
    assert(elaboration_factory_registered);
    assert(factory_parameter_registered);

    error.clear();
    auto hierarchy = fsim::systemc::HierarchyRegistry::load(
        std::filesystem::path{argv[1]}, error);
    assert(hierarchy != nullptr);
    assert(error.empty());
    assert(hierarchy->has_factory("sample"));
    assert(!hierarchy->has_elaboration_factory("sample"));
    assert(hierarchy->has_elaboration_factory("bridge"));
    const auto bridge_parameters =
        hierarchy->factory_parameters("bridge");
    assert(bridge_parameters);
    assert(bridge_parameters->size() == 1);
    assert(bridge_parameters->front().name == "WIDTH");
    assert(
        bridge_parameters->front().type
        == FSIM_SC_CONSTRUCTION_POSITIVE);
    assert(bridge_parameters->front().default_value == 8);
    const auto legacy =
        hierarchy->instantiate("sample", "top.u_legacy", 0, error);
    assert(!legacy);
    assert(
        error.find("legacy registration form")
        != std::string::npos);
    error.clear();
    const auto bridge =
        hierarchy->instantiate("bridge", "top.u_bridge", 0, error);
    assert(bridge);
    assert(error.empty());
    assert(bridge->factory == "bridge");
    assert(bridge->instance == "top.u_bridge");
    assert(bridge->handle != 0);
    assert(bridge->ports.size() == 2);
    assert(bridge->ports[0].name == "clock");
    assert(bridge->ports[0].direction == FSIM_SC_INPUT);
    assert(bridge->ports[0].encoding == FSIM_SC_BIT2);
    assert(bridge->ports[0].width == 1);
    assert(bridge->ports[1].name == "value");
    assert(bridge->ports[1].direction == FSIM_SC_OUTPUT);
    assert(bridge->ports[1].encoding == FSIM_SC_UNSIGNED);
    assert(bridge->ports[1].width == 8);
    assert(bridge->processes.size() == 1);
    assert(bridge->processes[0].name == "evaluate");
    assert(bridge->processes[0].kind == FSIM_SC_METHOD);
    assert(!bridge->processes[0].initialize);
    assert(bridge->processes[0].sensitivity.size() == 1);
    assert(
        bridge->processes[0].sensitivity[0].object
        == bridge->ports[0].handle);
    assert(
        bridge->processes[0].sensitivity[0].edge
        == FSIM_SC_POSEDGE);
    assert(bridge->foreign_children.size() == 1);
    assert(bridge->foreign_children[0].name == "u_hdl");
    assert((
        bridge->foreign_children[0].construction_actuals
        == std::vector<std::pair<std::string, std::int64_t>>{
            {"WIDTH", 8}}));
    assert(bridge->foreign_children[0].ports.size() == 2);
    assert(
        bridge->foreign_children[0].ports[0].object
        == bridge->ports[0].handle);
    assert(
        bridge->foreign_children[0].ports[1].object
        == bridge->ports[1].handle);
    const auto nested_bridge = hierarchy->instantiate(
        "bridge",
        "top.u_bridge.u_nested",
        bridge->handle,
        std::array<std::pair<std::string, std::int64_t>, 1>{
            std::pair<std::string, std::int64_t>{"WIDTH", 4}},
        error);
    assert(nested_bridge);
    assert(nested_bridge->parent == bridge->handle);
    assert(nested_bridge->handle != bridge->handle);
    assert(nested_bridge->ports[1].width == 4);
    assert((
        nested_bridge->construction_values
        == std::vector<std::pair<std::string, std::int64_t>>{
            {"WIDTH", 4}}));

    error.clear();
    const auto invalid_width = hierarchy->instantiate(
        "bridge",
        "top.invalid_width",
        0,
        std::array<std::pair<std::string, std::int64_t>, 1>{
            std::pair<std::string, std::int64_t>{"WIDTH", 0}},
        error);
    assert(!invalid_width);
    assert(error.find("violates its declared type") != std::string::npos);
    error.clear();
    const auto unknown_actual = hierarchy->instantiate(
        "bridge",
        "top.unknown",
        0,
        std::array<std::pair<std::string, std::int64_t>, 1>{
            std::pair<std::string, std::int64_t>{"MISSING", 1}},
        error);
    assert(!unknown_actual);
    assert(
        error == "unknown SystemC construction parameter 'MISSING'");
    error.clear();
    const auto duplicate_actual = hierarchy->instantiate(
        "bridge",
        "top.duplicate",
        0,
        std::array<std::pair<std::string, std::int64_t>, 2>{
            std::pair<std::string, std::int64_t>{"WIDTH", 4},
            std::pair<std::string, std::int64_t>{"WIDTH", 5}},
        error);
    assert(!duplicate_actual);
    assert(error.find("more than one actual") != std::string::npos);

    factory_registered = false;
    elaboration_factory_registered = false;
    error.clear();
    const auto throwing_plugin =
        fsim::systemc::Plugin::load(
            std::filesystem::path{argv[2]}, host, registrar, error);
    assert(throwing_plugin == nullptr);
    assert(
        error
        == "SystemC plug-in initialization threw an exception: "
           "intentional plug-in failure");
    assert(!factory_registered);
    assert(!elaboration_factory_registered);
    std::cout << "SystemC plug-in loader tests passed\n";
}
