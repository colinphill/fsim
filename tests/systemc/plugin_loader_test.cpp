// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/plugin_loader.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool factory_registered = false;
bool elaboration_factory_registered = false;
bool fiber_factory_registered = false;
bool factory_parameter_registered = false;
std::vector<std::string> macro_registration_order;

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
    const auto owned_name = std::string{name};
    const bool valid = factory != nullptr && destroy != nullptr
        && (owned_name == "bridge" || owned_name == "fiber_bridge");
    elaboration_factory_registered =
        elaboration_factory_registered || (valid && owned_name == "bridge");
    fiber_factory_registered =
        fiber_factory_registered || (valid && owned_name == "fiber_bridge");
    return valid ? FSIM_SC_OK : FSIM_SC_INVALID_ARGUMENT;
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

fsim_sc_status_v1 record_macro_factory(
    void*,
    const char* name,
    fsim_sc_module_elaborate_v1 factory,
    fsim_sc_module_destroy_v1 destroy,
    void*) {
    if (name == nullptr || factory == nullptr || destroy == nullptr) {
        return FSIM_SC_INVALID_ARGUMENT;
    }
    macro_registration_order.emplace_back(name);
    return FSIM_SC_OK;
}

fsim_sc_status_v1 accept_macro_parameter(
    void*,
    const char*,
    const char*,
    fsim_sc_construction_type_v1,
    std::uint8_t,
    std::int64_t) {
    return FSIM_SC_OK;
}

class TestExecutionContext final
    : public fsim::runtime::simir::ProcessExecutionContext {
public:
    [[nodiscard]] fsim::runtime::PackedLogic4 read_signal(
        fsim::runtime::simir::SignalId) const override {
        return fsim::runtime::PackedLogic4{};
    }

    void write_blocking(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override {}

    void write_blocking_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override {}

    void write_update(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override {}

    void write_update_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override {}

    void write_after(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        fsim::runtime::SimulationTick) override {}

    void write_after_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        fsim::runtime::SimulationTick) override {}

    void write_inertial(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        const fsim::runtime::simir::TransitionDelays&) override {}

    void write_inertial_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        const fsim::runtime::simir::TransitionDelays&) override {}
};

enum LifecycleEvent : std::uint32_t {
    constructed = 1,
    before_elaboration = 2,
    elaborated = 3,
    started = 4,
    fiber_entered = 5,
    fiber_stopped = 6,
    ended = 7,
    destroyed = 8,
};

} // namespace

int main(int argc, char** argv) {
    // FSIM-CONFORMANCE CF-SC-PLUGIN-001 source=SRC-SYSTEMC expectation=execute
    // FSIM-CONFORMANCE CF-SC-LIFECYCLE-001 source=SRC-SYSTEMC expectation=execute
    assert(argc == 8);
    const auto plugin_path = std::filesystem::path{argv[1]};
    const auto throwing_plugin_path = std::filesystem::path{argv[2]};
    const auto error_plugin_path = std::filesystem::path{argv[3]};
    const auto empty_plugin_path = std::filesystem::path{argv[4]};
    const auto macro_plugin_path = std::filesystem::path{argv[5]};
    const auto duplicate_macro_plugin_path =
        std::filesystem::path{argv[6]};
    const auto no_factory_plugin_path =
        std::filesystem::path{argv[7]};
    assert(!fsim::platform::DynamicLibrary::is_loaded(plugin_path));

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
    {
        const auto plugin = fsim::systemc::Plugin::load(
            plugin_path, host, registrar, error);
        assert(plugin != nullptr);
        assert(error.empty());
        assert(factory_registered);
        assert(elaboration_factory_registered);
        assert(fiber_factory_registered);
        assert(factory_parameter_registered);
        assert(fsim::platform::DynamicLibrary::is_loaded(plugin_path));
    }
    assert(!fsim::platform::DynamicLibrary::is_loaded(plugin_path));

    auto incompatible_host = host;
    incompatible_host.abi_version = FSIM_SYSTEMC_ABI_VERSION + 1;
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        plugin_path, incompatible_host, registrar, error));
    assert(error == "SystemC host/registrar ABI mismatch");
    assert(!fsim::platform::DynamicLibrary::is_loaded(plugin_path));

    error.clear();
    assert(!fsim::systemc::Plugin::load(
        empty_plugin_path, host, registrar, error));
    assert(
        error.find("does not export fsim_plugin_init_v1")
        != std::string::npos);
    assert(!fsim::platform::DynamicLibrary::is_loaded(empty_plugin_path));

    auto macro_registrar = registrar;
    macro_registrar.register_elaboration_factory =
        record_macro_factory;
    macro_registrar.register_factory_parameter =
        accept_macro_parameter;
    macro_registration_order.clear();
    error.clear();
    {
        const auto macro_plugin = fsim::systemc::Plugin::load(
            macro_plugin_path, host, macro_registrar, error);
        assert(macro_plugin != nullptr);
        assert(error.empty());
        assert((macro_registration_order
                == std::vector<std::string>{
                    "PlainModule",
                    "a_parameterized",
                    "content_proxy_root",
                    "m_alias_one",
                    "m_alias_two",
                    "process_proxy_root",
                    "proxy_root",
                    "unbound_proxy_root"}));
    }

    macro_registration_order.clear();
    error.clear();
    assert(!fsim::systemc::Plugin::load(
        duplicate_macro_plugin_path,
        host,
        macro_registrar,
        error));
    assert(
        error == "SystemC plug-in initialization failed with status 1");
    assert(macro_registration_order.empty());

    error.clear();
    const auto macro_hierarchy =
        fsim::systemc::HierarchyRegistry::load(
            macro_plugin_path, error);
    assert(macro_hierarchy != nullptr);
    assert(error.empty());
    assert(macro_hierarchy->factory_count() == 8);
    assert(macro_hierarchy->has_factory("PlainModule"));
    assert(macro_hierarchy->has_factory("m_alias_one"));
    assert(macro_hierarchy->has_factory("m_alias_two"));
    const auto macro_parameters =
        macro_hierarchy->factory_parameters("a_parameterized");
    assert(macro_parameters && macro_parameters->size() == 1);
    assert(macro_parameters->front().name == "WIDTH");
    assert(macro_parameters->front().default_value == 12);
    const auto proxy_root = macro_hierarchy->instantiate(
        "proxy_root", "top", 0, error);
    assert(proxy_root);
    assert(error.empty());
    assert(proxy_root->native_children.empty());
    assert(proxy_root->foreign_children.size() == 1);
    const auto& proxy = proxy_root->foreign_children.front();
    assert(proxy.module_facade);
    assert(proxy.name == "proxy");
    assert(proxy.ports.size() == 3);
    assert(proxy.construction_actuals.size() == 1);
    assert(proxy.construction_actuals.front().first == "WIDTH");
    assert(proxy.construction_actuals.front().second == 8);
    const auto proxy_info =
        macro_hierarchy->find_object(proxy_root->handle, "top.proxy");
    assert(proxy_info);
    assert(proxy_info->kind
           == fsim::systemc::HierarchyObjectKind::module);
    const auto proxy_children =
        macro_hierarchy->child_objects(proxy.handle);
    assert(proxy_children.size() == 3);
    assert(std::ranges::all_of(
        proxy_children,
        [](const auto& child) {
            return child.kind
                == fsim::systemc::HierarchyObjectKind::port;
        }));

    error.clear();
    const auto unbound = macro_hierarchy->instantiate(
        "unbound_proxy_root", "bad", 0, error);
    assert(!unbound);
    assert(error.find("has unbound port") != std::string::npos);

    error.clear();
    const auto invalid_content = macro_hierarchy->instantiate(
        "content_proxy_root", "bad_content", 0, error);
    assert(!invalid_content);
    assert(error.find("may contain only ports") != std::string::npos);

    error.clear();
    const auto invalid_process = macro_hierarchy->instantiate(
        "process_proxy_root", "bad_process", 0, error);
    assert(!invalid_process);
    assert(error.find("cannot contain processes") != std::string::npos);

    error.clear();
    const auto no_factory_hierarchy =
        fsim::systemc::HierarchyRegistry::load(
            no_factory_plugin_path, error);
    assert(no_factory_hierarchy != nullptr);
    assert(error.empty());
    assert(no_factory_hierarchy->factory_count() == 0);

    error.clear();
    assert(!fsim::systemc::Plugin::load(
        plugin_path.parent_path() / "missing-plugin-image",
        host,
        registrar,
        error));
    assert(!error.empty());

    auto probe = fsim::platform::DynamicLibrary::open(plugin_path, error);
    assert(probe && error.empty());
    using ResetLifecycle = void (*)();
    using LifecycleCount = std::size_t (*)();
    using LifecycleEventAt = std::uint32_t (*)(std::size_t);
    using IgnoreDuplicateRegistration = void (*)(std::uint8_t);
    const auto reset_lifecycle = reinterpret_cast<ResetLifecycle>(
        probe->symbol("fsim_test_reset_lifecycle_v1", error));
    assert(reset_lifecycle != nullptr && error.empty());
    const auto lifecycle_count = reinterpret_cast<LifecycleCount>(
        probe->symbol("fsim_test_lifecycle_count_v1", error));
    assert(lifecycle_count != nullptr && error.empty());
    const auto lifecycle_event = reinterpret_cast<LifecycleEventAt>(
        probe->symbol("fsim_test_lifecycle_event_v1", error));
    assert(lifecycle_event != nullptr && error.empty());
    const auto ignore_duplicate_registration =
        reinterpret_cast<IgnoreDuplicateRegistration>(probe->symbol(
            "fsim_test_ignore_duplicate_registration_v1", error));
    assert(ignore_duplicate_registration != nullptr && error.empty());
    const auto lifecycle_events = [&]() {
        std::vector<std::uint32_t> result;
        result.reserve(lifecycle_count());
        for (std::size_t index = 0; index < lifecycle_count(); ++index) {
            result.push_back(lifecycle_event(index));
        }
        return result;
    };
    reset_lifecycle();

    // FSIM-CONFORMANCE CF-SC-FAIL-N01 source=SRC-SYSTEMC expectation=reject
    // A plug-in cannot hide a rejected callback by returning success. The
    // buffered transaction rejects the entire set before any caller registrar
    // callback is replayed, and hierarchy staging remains externally empty.
    factory_registered = false;
    elaboration_factory_registered = false;
    fiber_factory_registered = false;
    factory_parameter_registered = false;
    ignore_duplicate_registration(1);
    const auto ignored_duplicate = fsim::systemc::Plugin::load(
        plugin_path, host, registrar, error);
    assert(!ignored_duplicate);
    assert(
        error == "SystemC plug-in initialization ignored an invalid or "
                 "duplicate factory registration");
    assert(!factory_registered);
    assert(!elaboration_factory_registered);
    assert(!fiber_factory_registered);
    assert(!factory_parameter_registered);
    error.clear();
    const auto rejected_hierarchy =
        fsim::systemc::HierarchyRegistry::load(plugin_path, error);
    assert(!rejected_hierarchy);
    assert(
        error == "SystemC plug-in initialization ignored an invalid or "
                 "duplicate factory registration");
    ignore_duplicate_registration(0);

    error.clear();
    auto hierarchy = fsim::systemc::HierarchyRegistry::load(
        plugin_path, error);
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
    const auto bridge_info = hierarchy->object_info(bridge->handle);
    assert(
        bridge_info
        && bridge_info->parent == 0
        && bridge_info->name == "top.u_bridge"
        && bridge_info->path == "top.u_bridge"
        && bridge_info->kind
            == fsim::systemc::HierarchyObjectKind::module);
    const auto bridge_children =
        hierarchy->child_objects(bridge->handle);
    assert(bridge_children.size() == 4);
    assert(std::is_sorted(
        bridge_children.begin(), bridge_children.end(),
        [](const auto& left, const auto& right) {
            return left.handle < right.handle;
        }));
    assert(
        bridge_children[0].name == "clock"
        && bridge_children[0].kind
            == fsim::systemc::HierarchyObjectKind::port);
    assert(
        hierarchy->find_object(bridge->handle, "value")->handle
        == bridge->ports[1].handle);
    assert(
        hierarchy->find_object(
            bridge->handle, "top.u_bridge.evaluate")->handle
        == bridge->processes[0].handle);
    assert(!hierarchy->find_object(bridge->handle, "missing"));
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
    const auto nested_info =
        hierarchy->object_info(nested_bridge->handle);
    assert(nested_info && nested_info->parent == bridge->handle);
    const auto updated_bridge_children =
        hierarchy->child_objects(bridge->handle);
    assert(updated_bridge_children.size() == 5);
    assert(
        hierarchy->find_object(
            nested_bridge->handle, "value")->handle
        == nested_bridge->ports[1].handle);
    assert(
        hierarchy->find_object(
            bridge->handle, "top.u_bridge.u_nested.value")->handle
        == nested_bridge->ports[1].handle);

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

    const std::array<fsim_sc_handle_v1, 2> terminal_roots{
        bridge->handle, nested_bridge->handle};
    hierarchy->complete_elaboration(terminal_roots);
    hierarchy->start_simulation(terminal_roots);
    hierarchy->end_simulation(terminal_roots);
    hierarchy.reset();
    assert((lifecycle_events() == std::vector<std::uint32_t>{
        constructed,
        constructed,
        before_elaboration,
        before_elaboration,
        elaborated,
        elaborated,
        started,
        started,
        ended,
        ended,
        destroyed,
        destroyed,
    }));

    // Two registries retain independent host/root/fiber state while sharing
    // one loaded image. Move-assignment must fully tear down the destination
    // before taking ownership of the source, including a suspended stack and
    // an implicit terminal callback.
    reset_lifecycle();
    auto first_registry =
        fsim::systemc::HierarchyRegistry::load(plugin_path, error);
    assert(first_registry && error.empty());
    auto second_registry =
        fsim::systemc::HierarchyRegistry::load(plugin_path, error);
    assert(second_registry && error.empty());
    const auto first_fiber = first_registry->instantiate(
        "fiber_bridge", "first", 0, error);
    assert(first_fiber && error.empty());
    const auto second_fiber = second_registry->instantiate(
        "fiber_bridge", "second", 0, error);
    assert(second_fiber && error.empty());
    const std::array first_root{first_fiber->handle};
    const std::array second_root{second_fiber->handle};
    first_registry->complete_elaboration(first_root);
    first_registry->start_simulation(first_root);
    second_registry->complete_elaboration(second_root);
    second_registry->start_simulation(second_root);
#if defined(FSIM_HAS_BOOST_CONTEXT)
    TestExecutionContext execution_context;
    const auto suspended = first_registry->invoke_process(
        first_fiber->processes.front().handle, execution_context);
    assert(suspended.kind == fsim::systemc::MethodSuspendKind::wait_for);
    assert(suspended.delay_ticks == 5);
#endif
    *first_registry = std::move(*second_registry);
    assert(!second_registry->has_factory("fiber_bridge"));
    first_registry.reset();
    second_registry.reset();
#if defined(FSIM_HAS_BOOST_CONTEXT)
    assert((lifecycle_events() == std::vector<std::uint32_t>{
        constructed,
        constructed,
        before_elaboration,
        elaborated,
        started,
        before_elaboration,
        elaborated,
        started,
        fiber_entered,
        fiber_stopped,
        ended,
        destroyed,
        ended,
        destroyed,
    }));
#else
    assert((lifecycle_events() == std::vector<std::uint32_t>{
        constructed,
        constructed,
        before_elaboration,
        elaborated,
        started,
        before_elaboration,
        elaborated,
        started,
        ended,
        destroyed,
        ended,
        destroyed,
    }));
#endif
    assert(fsim::platform::DynamicLibrary::is_loaded(plugin_path));
    probe.reset();
    assert(!fsim::platform::DynamicLibrary::is_loaded(plugin_path));

    // Construction status/null/exception failures roll back every pending
    // handle. Escaped process and destructor exceptions are contained while
    // the error-image remains live, and teardown still unloads it.
    error.clear();
    auto error_registry =
        fsim::systemc::HierarchyRegistry::load(error_plugin_path, error);
    assert(error_registry && error.empty());
    const auto status_failure = error_registry->instantiate(
        "status_failure", "status", 0, error);
    assert(!status_failure);
    assert(error.find("failed with status") != std::string::npos);
    assert(!error_registry->object_info(1));
    error.clear();
    const auto null_failure = error_registry->instantiate(
        "null_failure", "null", 0, error);
    assert(!null_failure);
    assert(error.find("returned a null module object") != std::string::npos);
    error.clear();
    const auto throw_failure = error_registry->instantiate(
        "throw_failure", "throw", 0, error);
    assert(!throw_failure);
    assert(
        error.find("intentional construction exception")
        != std::string::npos);
    error.clear();
    const auto destroy_failure = error_registry->instantiate(
        "destroy_throw", "destroy", 0, error);
    assert(destroy_failure && error.empty());
    const auto process_failure = error_registry->instantiate(
        "process_throw", "process", 0, error);
    assert(process_failure && error.empty());
    TestExecutionContext error_context;
    bool contained_process_exception = false;
    try {
        (void)error_registry->invoke_process(
            process_failure->processes.front().handle,
            error_context);
    } catch (const std::runtime_error& exception) {
        contained_process_exception =
            std::string_view{exception.what()}.find(
                "intentional process exception")
            != std::string_view::npos;
    }
    assert(contained_process_exception);
    error_registry.reset();
    assert(!fsim::platform::DynamicLibrary::is_loaded(error_plugin_path));

    factory_registered = false;
    elaboration_factory_registered = false;
    fiber_factory_registered = false;
    error.clear();
    const auto throwing_plugin =
        fsim::systemc::Plugin::load(
            throwing_plugin_path, host, registrar, error);
    assert(throwing_plugin == nullptr);
    assert(
        error
        == "SystemC plug-in initialization threw an exception: "
           "intentional plug-in failure");
    assert(!factory_registered);
    assert(!elaboration_factory_registered);
    assert(!fiber_factory_registered);
    assert(!fsim::platform::DynamicLibrary::is_loaded(throwing_plugin_path));
    std::cout << "SystemC plug-in loader tests passed\n";
}
