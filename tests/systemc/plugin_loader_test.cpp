// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/plugin_loader.hpp"
#include "fsim/systemc/scv.hpp"

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// FSIM-CONFORMANCE CF-SC-PLUGIN-001 source=SRC-SYSTEMC expectation=execute
// FSIM-CONFORMANCE CF-SC-LIFECYCLE-001 source=SRC-SYSTEMC expectation=execute
// FSIM-CONFORMANCE CF-SC-FAIL-N01 source=SRC-SYSTEMC expectation=reject

std::vector<std::string> registrations;
bool parameter_registered = false;

fsim_sc_status_v1 record_factory(
    void*,
    const char* name,
    fsim_sc_module_elaborate_v1 factory,
    fsim_sc_module_destroy_v1 destroy,
    void*)
{
    if (name == nullptr || factory == nullptr || destroy == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    registrations.emplace_back(name);
    return FSIM_SC_OK;
}

fsim_sc_status_v1 record_parameter(
    void*,
    const char* factory,
    const char* name,
    fsim_sc_construction_type_v1 type,
    std::uint8_t has_default,
    std::int64_t default_value)
{
    if (factory == nullptr || name == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    if (std::string_view { factory } == "a_parameterized") {
        parameter_registered = std::string_view { name } == "WIDTH"
            && type == FSIM_SC_CONSTRUCTION_POSITIVE
            && has_default == 1
            && default_value == 12;
        return parameter_registered
            ? FSIM_SC_OK
            : FSIM_SC_INVALID_ARGUMENT;
    }
    return FSIM_SC_OK;
}

class TestExecutionContext final
    : public fsim::runtime::simir::ProcessExecutionContext {
public:
    [[nodiscard]] fsim::runtime::PackedLogic4 read_signal(
        fsim::runtime::simir::SignalId) const override
    {
        return fsim::runtime::PackedLogic4 { };
    }

    void write_blocking(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override
    {
    }
    void write_blocking_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override
    {
    }
    void write_update(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override
    {
    }
    void write_update_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override
    {
    }
    void write_after(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        fsim::runtime::SimulationTick) override
    {
    }
    void write_after_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        fsim::runtime::SimulationTick) override
    {
    }
    void write_inertial(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        const fsim::runtime::simir::TransitionDelays&) override
    {
    }
    void write_inertial_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        const fsim::runtime::simir::TransitionDelays&) override
    {
    }
};

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 7);
    const auto throwing_plugin_path = std::filesystem::path { argv[1] };
    const auto error_plugin_path = std::filesystem::path { argv[2] };
    const auto empty_plugin_path = std::filesystem::path { argv[3] };
    const auto macro_plugin_path = std::filesystem::path { argv[4] };
    const auto duplicate_macro_plugin_path = std::filesystem::path { argv[5] };
    const auto no_factory_plugin_path = std::filesystem::path { argv[6] };

    fsim_sc_host_v1 host { };
    host.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    host.struct_size = static_cast<decltype(host.struct_size)>(sizeof(host) + 64);
    host.scv_compatibility_identity = fsim_scv_compatibility_identity();

    fsim_sc_registrar_v1 registrar { };
    registrar.abi_version = FSIM_SYSTEMC_ABI_VERSION;
    registrar.struct_size = static_cast<decltype(registrar.struct_size)>(
        sizeof(registrar) + 64);
    registrar.register_elaboration_factory = record_factory;
    registrar.register_factory_parameter = record_parameter;

    std::string error;
    registrations.clear();
    parameter_registered = false;
    {
        const auto plugin = fsim::systemc::Plugin::load(
            macro_plugin_path, host, registrar, error);
        assert(plugin && error.empty());
        assert((registrations == std::vector<std::string> {
                    "PlainModule",
                    "a_parameterized",
                    "m_alias_one",
                    "m_alias_two",
                }));
        assert(parameter_registered);
    }

    auto incompatible_host = host;
    incompatible_host.abi_version = FSIM_SYSTEMC_ABI_VERSION + 1;
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        macro_plugin_path, incompatible_host, registrar, error));
    assert(error == "SystemC host/registrar ABI mismatch");

    auto truncated_host = host;
    truncated_host.struct_size = sizeof(truncated_host) - 1U;
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        macro_plugin_path.parent_path() / "unopened-truncated-host",
        truncated_host,
        registrar,
        error));
    assert(error == "SystemC host/registrar ABI mismatch");

    auto truncated_registrar = registrar;
    truncated_registrar.struct_size = sizeof(truncated_registrar) - 1U;
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        macro_plugin_path.parent_path() / "unopened-truncated-registrar",
        host,
        truncated_registrar,
        error));
    assert(error == "SystemC host/registrar ABI mismatch");

    auto wrong_scv_identity = std::string { fsim_scv_compatibility_identity() };
    const auto scv_version = wrong_scv_identity.find("|scv=2.0.1|");
    assert(scv_version != std::string::npos);
    wrong_scv_identity.replace(scv_version, 11, "|scv=2.0.2|");
    auto incompatible_scv_host = host;
    incompatible_scv_host.scv_compatibility_identity = wrong_scv_identity.c_str();
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        macro_plugin_path, incompatible_scv_host, registrar, error));
    assert(error == "FSIM-SCV-C001 SCV version mismatch");

    error.clear();
    assert(!fsim::systemc::Plugin::load(
        empty_plugin_path, host, registrar, error));
    assert(
        error.find("does not export fsim_plugin_init_v1")
        != std::string::npos);

    registrations.clear();
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        duplicate_macro_plugin_path, host, registrar, error));
    assert(error == "SystemC plug-in initialization failed with status 1");
    assert(registrations.empty());

    error.clear();
    auto hierarchy = fsim::systemc::HierarchyRegistry::load(macro_plugin_path, error);
    assert(hierarchy && error.empty());
    assert(hierarchy->factory_count() == 4);
    const auto parameters = hierarchy->factory_parameters("a_parameterized");
    assert(parameters && parameters->size() == 1);
    assert(parameters->front().name == "WIDTH");
    assert(parameters->front().default_value == 12);
    const auto module = hierarchy->instantiate("a_parameterized", "top", 0, error);
    assert(module && error.empty());
    assert(module->processes.size() == 1);
    assert(module->processes.front().name == "$accellera_kernel");
    const std::array roots { module->handle };
    hierarchy->complete_elaboration(roots);
    hierarchy->start_simulation(roots);
    hierarchy->end_simulation(roots);
    hierarchy.reset();

    error.clear();
    const auto no_factory = fsim::systemc::HierarchyRegistry::load(
        no_factory_plugin_path, error);
    assert(no_factory && error.empty());
    assert(no_factory->factory_count() == 0);

    error.clear();
    assert(!fsim::systemc::Plugin::load(
        macro_plugin_path.parent_path() / "missing-plugin-image",
        host,
        registrar,
        error));
    assert(!error.empty());

    error.clear();
    auto error_hierarchy = fsim::systemc::HierarchyRegistry::load(error_plugin_path, error);
    assert(error_hierarchy && error.empty());
    assert(!error_hierarchy->instantiate(
        "status_failure", "status", 0, error));
    assert(error.find("failed with status") != std::string::npos);
    error.clear();
    assert(!error_hierarchy->instantiate(
        "null_failure", "null", 0, error));
    assert(error.find("returned a null module object") != std::string::npos);
    error.clear();
    assert(!error_hierarchy->instantiate(
        "throw_failure", "throw", 0, error));
    assert(
        error.find("intentional construction exception")
        != std::string::npos);
    error.clear();
    const auto process_failure = error_hierarchy->instantiate(
        "process_throw", "process", 0, error);
    assert(process_failure && error.empty());
    TestExecutionContext context;
    bool contained = false;
    try {
        (void)error_hierarchy->invoke_process(
            process_failure->processes.front().handle, context);
    } catch (const std::runtime_error& exception) {
        contained = std::string_view { exception.what() }.find(
                        "intentional process exception")
            != std::string_view::npos;
    }
    assert(contained);
    error_hierarchy.reset();

    registrations.clear();
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        throwing_plugin_path, host, registrar, error));
    assert(
        error
        == "SystemC plug-in initialization threw an exception: "
           "intentional plug-in failure");
    assert(registrations.empty());

    std::cout << "SystemC plug-in loader tests passed\n";
}
