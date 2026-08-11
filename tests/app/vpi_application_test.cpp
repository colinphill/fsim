// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vpi_type_descriptor.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

using fsim::runtime::SystemVerilogVpiCallbackEvent;
using fsim::runtime::SystemVerilogVpiCallbackKind;
using fsim::runtime::SystemVerilogVpiObjectInfo;
using fsim::runtime::SystemVerilogVpiObjectKind;
using fsim::runtime::SystemVerilogVpiObjectRegistry;
using fsim::runtime::SystemVerilogVpiStoredValue;

constexpr std::size_t kWideWidth = 137;
constexpr std::size_t kMemoryWidth = 71;

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct EventCapture {
    SystemVerilogVpiCallbackKind kind {
        SystemVerilogVpiCallbackKind::ValueChange
    };
    std::string value;
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const EventCapture&, const EventCapture&) = default;
};

struct RunPoint {
    fsim::runtime::RunStatus status { fsim::runtime::RunStatus::completed };
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const RunPoint&, const RunPoint&) = default;
};

struct Capture {
    std::vector<EventCapture> value_events;
    std::vector<EventCapture> named_events;
    std::vector<EventCapture> memory_events;
    EventCapture start;
    EventCapture finish;
    RunPoint stopped;
    RunPoint finished;
    std::vector<std::pair<fsim::runtime::SimulationTick, std::uint64_t>>
        time_queries;
    std::string stored_at_five;
    std::string deposited_beneath_force;
    std::string stored_at_ten;
    std::string released_at_ten;
    std::string stored_at_fifteen;
    std::string memory_probe_at_ten;
    std::vector<std::string> unresolved_drivers_at_zero;
    std::vector<std::string> unresolved_drivers_at_five;
    std::size_t compiled_processes { };

    [[nodiscard]] bool observably_equal(const Capture& other) const
    {
        return value_events == other.value_events
            && named_events == other.named_events
            && memory_events == other.memory_events && start == other.start
            && finish == other.finish && stopped == other.stopped
            && finished == other.finished
            && time_queries == other.time_queries
            && stored_at_five == other.stored_at_five
            && deposited_beneath_force == other.deposited_beneath_force
            && stored_at_ten == other.stored_at_ten
            && released_at_ten == other.released_at_ten
            && stored_at_fifteen == other.stored_at_fifteen
            && memory_probe_at_ten == other.memory_probe_at_ten
            && unresolved_drivers_at_zero
            == other.unresolved_drivers_at_zero
            && unresolved_drivers_at_five
            == other.unresolved_drivers_at_five;
    }
};

[[nodiscard]] std::string make_bits(
    const std::initializer_list<std::size_t> ones)
{
    std::string result(kWideWidth, '0');
    for (const auto index : ones) {
        assert(index < result.size());
        result[index] = '1';
    }
    return result;
}

[[nodiscard]] SystemVerilogVpiStoredValue stored_value(
    const std::string_view bits)
{
    SystemVerilogVpiStoredValue result;
    result.payload = fsim::runtime::PackedLogic4::from_msb_string(bits);
    return result;
}

[[nodiscard]] std::string packed_bits(
    const SystemVerilogVpiStoredValue& value)
{
    const auto* packed
        = std::get_if<fsim::runtime::PackedLogic4>(&value.payload);
    assert(packed && packed->width() == kWideWidth);
    return packed->to_msb_string();
}

[[nodiscard]] const fsim::runtime::SystemVerilogVpiObjectState& object_state(
    const fsim::runtime::SystemVerilogVpiObjectStateSnapshot& snapshot,
    const fsim_vpi_handle_v1 object)
{
    const auto found = std::ranges::find_if(
        snapshot.objects, [object](const auto& candidate) {
            return candidate.source_handle == object;
        });
    assert(found != snapshot.objects.end());
    return *found;
}

[[nodiscard]] std::string stored_bits(
    const SystemVerilogVpiObjectRegistry& registry,
    const fsim_vpi_handle_v1 object)
{
    const auto snapshot = registry.snapshot_values();
    assert(snapshot);
    const auto& state = object_state(snapshot, object);
    assert(state.value);
    return packed_bits(*state.value);
}

[[nodiscard]] std::string parameter_bits(
    const SystemVerilogVpiObjectRegistry& registry,
    const fsim_vpi_handle_v1 object,
    const std::size_t expected_width)
{
    const auto snapshot = registry.snapshot_values();
    assert(snapshot);
    const auto& state = object_state(snapshot, object);
    assert(state.value && !state.forced_value);
    const auto* packed
        = std::get_if<fsim::runtime::PackedLogic4>(&state.value->payload);
    assert(packed && packed->width() == expected_width);
    return packed->to_msb_string();
}

[[nodiscard]] std::string forced_bits(
    const SystemVerilogVpiObjectRegistry& registry,
    const fsim_vpi_handle_v1 object)
{
    const auto snapshot = registry.snapshot_values();
    assert(snapshot);
    const auto& state = object_state(snapshot, object);
    assert(state.forced_value);
    return packed_bits(*state.forced_value);
}

void collect_hierarchy(
    SystemVerilogVpiObjectRegistry& registry,
    const fsim_vpi_handle_v1 parent,
    std::vector<SystemVerilogVpiObjectInfo>& objects)
{
    const auto iterator = registry.iterate_children(parent);
    assert(iterator);
    while (true) {
        const auto child = registry.scan(iterator.value);
        if (child.error == fsim::runtime::SystemVerilogVpiIteratorError::End) {
            break;
        }
        assert(child);
        const auto info = registry.lookup(child.value);
        assert(info);
        objects.push_back(*info.value);
        collect_hierarchy(registry, child.value, objects);
    }
    assert(registry.release_iterator(iterator.value)
        == fsim::runtime::SystemVerilogVpiIteratorError::None);
}

[[nodiscard]] RunPoint run_point(const fsim::runtime::RunResult& result)
{
    return { result.status, result.time, result.delta };
}

[[nodiscard]] fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vpi-application";
    config.project.top = "verilog:work.vpi_app";
    config.project.time_resolution = "2ps";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0 ? "cache-o0"
                                                           : "cache-o2");
    config.run.max_deltas = 1'000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::verilog;
    sources.standard = "2005";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] fsim::app::BuiltProject build_project(
    const fsim::project::Config& config)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(project && !diagnostics.has_error());
    return std::move(*project);
}

Capture execute(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view initial,
    const std::string_view middle,
    const std::string_view next,
    const std::string_view late,
    const std::string_view deposited,
    const std::string_view forced)
{
    auto project = build_project(config);
    auto foreign_project = build_project(config);
    fsim::app::Simulation simulation {
        std::move(project), config.run.max_deltas, engine
    };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    fsim::app::Simulation foreign_simulation {
        std::move(foreign_project), config.run.max_deltas, engine
    };

    auto& registry = simulation.systemverilog_vpi_objects();
    auto& callbacks = simulation.systemverilog_vpi_callbacks();
    auto& values = simulation.systemverilog_vpi_values();
    auto& control = simulation.systemverilog_vpi_control();
    auto& time = simulation.systemverilog_vpi_time();
    const auto identity = registry.simulation_identity();
    assert(identity != 0 && control.simulation_identity() == identity);
    constexpr fsim::runtime::SystemVerilogVpiTimeProfile expected_profile {
        -12, -12, 2
    };
    assert(time.profile() == expected_profile);
    assert(control.submit(
                      { fsim::runtime::SystemVerilogVpiControlOperation::Reset,
                          std::nullopt,
                          std::nullopt })
               .error
        == fsim::runtime::SystemVerilogVpiControlError::InvalidOperation);

    const auto root = registry.find("vpi_app");
    const auto module = registry.find("vpi_app.leaf");
    const auto target = registry.find("vpi_app.evolving");
    const auto unresolved = registry.find("vpi_app.unresolved");
    const auto input_port = registry.find("vpi_app.leaf.input_value");
    const auto output_port = registry.find("vpi_app.leaf.mirror");
    const auto integer = registry.find("vpi_app.signed_counter");
    const auto retained = registry.find("vpi_app.retained");
    const auto pulled = registry.find("vpi_app.pulled");
    const auto combined = registry.find("vpi_app.combined");
    const auto unique = registry.find("vpi_app.unique_net");
    const auto named_event = registry.find("vpi_app.fired");
    const auto generated_zero = registry.find("vpi_app.generated[0]");
    const auto generated_one = registry.find("vpi_app.generated[1]");
    const auto eight_parameter = registry.find("vpi_app.EIGHT_PARAM");
    const auto sixteen_parameter = registry.find("vpi_app.SIXTEEN_PARAM");
    const auto signed_parameter = registry.find("vpi_app.SIGNED_PARAM");
    const auto state_parameter = registry.find("vpi_app.STATE_PARAM");
    const auto implicit_parameter = registry.find("vpi_app.IMPLICIT_PARAM");
    const auto overridden_byte
        = registry.find("vpi_app.leaf.BYTE_PARAM");
    const auto default_byte
        = registry.find("vpi_app.leaf_default.BYTE_PARAM");
    const auto overridden_signed
        = registry.find("vpi_app.leaf.LEAF_SIGNED_PARAM");
    const auto default_signed
        = registry.find("vpi_app.leaf_default.LEAF_SIGNED_PARAM");
    const auto overridden_state
        = registry.find("vpi_app.leaf.LEAF_STATE_PARAM");
    const auto default_state
        = registry.find("vpi_app.leaf_default.LEAF_STATE_PARAM");
    const auto memory = registry.find("vpi_app.words");
    const auto memory_word_three = registry.find("vpi_app.words[3]");
    const auto memory_word_two = registry.find("vpi_app.words[2]");
    const auto memory_probe = registry.find("vpi_app.memory_probe");
    const auto strength_net = registry.find("vpi_app.strength_net");
    assert(root && module && target && unresolved && input_port && output_port
        && integer && retained && pulled && combined && unique && named_event
        && generated_zero && generated_one && eight_parameter
        && sixteen_parameter && signed_parameter && state_parameter
        && implicit_parameter
        && overridden_byte && default_byte && overridden_signed
        && default_signed && overridden_state && default_state && memory
        && memory_word_three && memory_word_two && memory_probe
        && strength_net);
    assert(root.value->kind == SystemVerilogVpiObjectKind::Root);
    assert(module.value->kind == SystemVerilogVpiObjectKind::Module
        && module.value->parent == root.value->handle);
    assert(target.value->kind == SystemVerilogVpiObjectKind::Variable
        && target.value->parent == root.value->handle);
    const auto target_type = registry.type_info(target.value->handle);
    const auto input_type = registry.type_info(input_port.value->handle);
    const auto output_type = registry.type_info(output_port.value->handle);
    assert(target_type && input_type && output_type);
    assert(target_type.value->language
            == fsim::runtime::SystemVerilogVpiLanguage::Verilog2005
        && target_type.value->category
            == fsim::runtime::SystemVerilogVpiValueCategory::Logic4
        && target_type.value->width == kWideWidth);
    const std::vector<fsim::runtime::SystemVerilogVpiRange> target_ranges {
        { 136, 0 }
    };
    assert(target_type.value->descriptor
        && target_type.value->descriptor->kind
            == fsim::runtime::SystemVerilogVpiDescriptorKind::PackedArray
        && target_type.value->descriptor->ranges == target_ranges
        && target_type.value->descriptor->children.size() == 1
        && target_type.value->descriptor->children.front().kind
            == fsim::runtime::SystemVerilogVpiDescriptorKind::Scalar
        && target_type.value->descriptor->children.front().category
            == fsim::runtime::SystemVerilogVpiValueCategory::Logic4
        && target_type.value->descriptor->children.front().width == 1);
    const auto target_layout
        = fsim::runtime::validate_systemverilog_vpi_descriptor(
            *target_type.value->descriptor);
    assert(target_layout && target_layout.value.fixed_bits == kWideWidth);
    assert(input_port.value->kind == SystemVerilogVpiObjectKind::Port
        && input_type.value->direction
            == fsim::runtime::SystemVerilogVpiDirection::Input
        && input_type.value->width == kWideWidth);
    assert(output_port.value->kind == SystemVerilogVpiObjectKind::Port
        && output_type.value->direction
            == fsim::runtime::SystemVerilogVpiDirection::Output
        && output_type.value->width == kWideWidth);
    const auto integer_type = registry.type_info(integer.value->handle);
    const auto retained_type = registry.type_info(retained.value->handle);
    const auto pulled_type = registry.type_info(pulled.value->handle);
    const auto combined_type = registry.type_info(combined.value->handle);
    const auto unique_type = registry.type_info(unique.value->handle);
    const auto named_event_type = registry.type_info(named_event.value->handle);
    assert(integer_type && retained_type && pulled_type && combined_type
        && unique_type && named_event_type);
    assert(integer.value->kind == SystemVerilogVpiObjectKind::Variable
        && integer_type.value->category
            == fsim::runtime::SystemVerilogVpiValueCategory::Integer4
        && integer_type.value->width == 32 && integer_type.value->is_signed);
    assert(retained.value->kind == SystemVerilogVpiObjectKind::Net
        && retained_type.value->net_kind
            == fsim::runtime::SystemVerilogVpiNetKind::TriReg);
    assert(pulled.value->kind == SystemVerilogVpiObjectKind::Net
        && pulled_type.value->net_kind
            == fsim::runtime::SystemVerilogVpiNetKind::Tri0);
    assert(combined.value->kind == SystemVerilogVpiObjectKind::Net
        && combined_type.value->net_kind
            == fsim::runtime::SystemVerilogVpiNetKind::Wand);
    assert(unique.value->kind == SystemVerilogVpiObjectKind::Net
        && unique_type.value->net_kind
            == fsim::runtime::SystemVerilogVpiNetKind::Uwire);
    assert(named_event.value->kind == SystemVerilogVpiObjectKind::NamedEvent
        && named_event_type.value->category
            == fsim::runtime::SystemVerilogVpiValueCategory::Event
        && named_event_type.value->width == 0);
    assert(generated_zero.value->kind
            == SystemVerilogVpiObjectKind::GenerateScope
        && generated_one.value->kind
            == SystemVerilogVpiObjectKind::GenerateScope);
    const auto memory_type = registry.type_info(memory.value->handle);
    const auto memory_word_type
        = registry.type_info(memory_word_three.value->handle);
    const std::vector<fsim::runtime::SystemVerilogVpiRange> memory_ranges {
        { 3, 1 }
    };
    assert(memory.value->kind == SystemVerilogVpiObjectKind::Memory
        && memory_type && memory_type.value->descriptor
        && memory_type.value->descriptor->kind
            == fsim::runtime::SystemVerilogVpiDescriptorKind::UnpackedArray
        && memory_type.value->descriptor->ranges == memory_ranges
        && memory_type.value->descriptor->children.size() == 1
        && memory_type.value->descriptor->children.front().width
            == kMemoryWidth
        && memory_word_three.value->kind
            == SystemVerilogVpiObjectKind::Variable
        && memory_word_three.value->parent == memory.value->handle
        && memory_word_type && memory_word_type.value->width == kMemoryWidth);

    const auto eight_type
        = registry.type_info(eight_parameter.value->handle);
    const auto sixteen_type
        = registry.type_info(sixteen_parameter.value->handle);
    const auto signed_type
        = registry.type_info(signed_parameter.value->handle);
    const auto state_type
        = registry.type_info(state_parameter.value->handle);
    const auto implicit_type
        = registry.type_info(implicit_parameter.value->handle);
    assert(eight_parameter.value->kind
            == SystemVerilogVpiObjectKind::Parameter
        && sixteen_parameter.value->kind
            == SystemVerilogVpiObjectKind::Parameter
        && signed_parameter.value->kind
            == SystemVerilogVpiObjectKind::Parameter
        && state_parameter.value->kind
            == SystemVerilogVpiObjectKind::Parameter
        && eight_parameter.value->parent == root.value->handle
        && overridden_byte.value->parent == module.value->handle);
    assert(eight_type && sixteen_type && signed_type && state_type
        && implicit_type);
    assert(eight_type.value->language
            == fsim::runtime::SystemVerilogVpiLanguage::Verilog2005
        && eight_type.value->category
            == fsim::runtime::SystemVerilogVpiValueCategory::Logic4
        && eight_type.value->width == 8 && !eight_type.value->is_signed
        && eight_type.value->is_constant
        && sixteen_type.value->width == 16
        && !sixteen_type.value->is_signed
        && sixteen_type.value->is_constant
        && signed_type.value->width == 16 && signed_type.value->is_signed
        && signed_type.value->is_constant
        && state_type.value->width == 12 && state_type.value->is_constant
        && implicit_type.value->width == 32
        && implicit_type.value->is_signed
        && implicit_type.value->is_constant);
    const std::vector<fsim::runtime::SystemVerilogVpiRange>
        eight_parameter_ranges { { 7, 0 } };
    assert(eight_type.value->descriptor
        && eight_type.value->descriptor->kind
            == fsim::runtime::SystemVerilogVpiDescriptorKind::PackedArray
        && eight_type.value->descriptor->ranges == eight_parameter_ranges);
    assert(parameter_bits(registry, eight_parameter.value->handle, 8)
            == "11111111"
        && parameter_bits(registry, sixteen_parameter.value->handle, 16)
            == "0000000011111111"
        && parameter_bits(registry, signed_parameter.value->handle, 16)
            == "1111111111111001"
        && parameter_bits(registry, state_parameter.value->handle, 12)
            == "10XZ01ZX1100"
        && parameter_bits(registry, implicit_parameter.value->handle, 32)
            == "00000000000000000000000000000101"
        && parameter_bits(registry, overridden_byte.value->handle, 8)
            == "10100101"
        && parameter_bits(registry, default_byte.value->handle, 8)
            == "00010001"
        && parameter_bits(registry, overridden_signed.value->handle, 16)
            == "1111111111110111"
        && parameter_bits(registry, default_signed.value->handle, 16)
            == "1111111111111110"
        && parameter_bits(registry, overridden_state.value->handle, 12)
            == "01ZX10XZ0011"
        && parameter_bits(registry, default_state.value->handle, 12)
            == "10XZ01ZX1100");
    const auto overridden_signed_type
        = registry.type_info(overridden_signed.value->handle);
    const auto default_signed_type
        = registry.type_info(default_signed.value->handle);
    assert(overridden_signed_type && default_signed_type
        && overridden_signed_type.value->width == 16
        && overridden_signed_type.value->is_signed
        && default_signed_type.value->width == 16
        && default_signed_type.value->is_signed);
    assert(values.apply(eight_parameter.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Deposit,
               stored_value("00000000"))
            == fsim::runtime::SystemVerilogVpiValueError::ReadOnly
        && values.apply(state_parameter.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Force,
               stored_value("000000000000"))
            == fsim::runtime::SystemVerilogVpiValueError::ReadOnly);

    std::vector<SystemVerilogVpiObjectInfo> hierarchy { *root.value };
    collect_hierarchy(registry, root.value->handle, hierarchy);
    const auto has_kind = [&](const SystemVerilogVpiObjectKind kind) {
        return std::ranges::any_of(hierarchy, [kind](const auto& object) {
            return object.kind == kind;
        });
    };
    assert(has_kind(SystemVerilogVpiObjectKind::Root)
        && has_kind(SystemVerilogVpiObjectKind::Module)
        && has_kind(SystemVerilogVpiObjectKind::Port)
        && has_kind(SystemVerilogVpiObjectKind::Net)
        && has_kind(SystemVerilogVpiObjectKind::Variable)
        && has_kind(SystemVerilogVpiObjectKind::Memory)
        && has_kind(SystemVerilogVpiObjectKind::Parameter)
        && has_kind(SystemVerilogVpiObjectKind::Process)
        && has_kind(SystemVerilogVpiObjectKind::Driver));
    const auto driver = std::ranges::find_if(
        hierarchy, [&](const auto& object) {
            return object.kind == SystemVerilogVpiObjectKind::Driver
                && object.parent == target.value->handle;
        });
    assert(driver != hierarchy.end());
    const auto driver_type = registry.type_info(driver->handle);
    assert(driver_type && driver_type.value->width == kWideWidth
        && driver_type.value->is_constant && driver_type.value->driver_range
        && driver_type.value->driver_range->offset == 0
        && driver_type.value->driver_range->width == kWideWidth
        && driver_type.value->driver_range->whole);
    const auto strength_driver = std::ranges::find_if(
        hierarchy, [&](const auto& object) {
            return object.kind == SystemVerilogVpiObjectKind::Driver
                && object.parent == strength_net.value->handle;
        });
    assert(strength_driver != hierarchy.end());
    const auto strength_value = registry.read_value(strength_driver->handle,
        fsim::runtime::SystemVerilogVpiValueFormat::Strength);
    assert(strength_value && strength_value.strength.drive.zero == fsim::runtime::SystemVerilogVpiStrengthRank::Pull
        && strength_value.strength.drive.one
            == fsim::runtime::SystemVerilogVpiStrengthRank::Supply);
    std::vector<fsim_vpi_handle_v1> unresolved_drivers;
    for (const auto& object : hierarchy) {
        if (object.kind == SystemVerilogVpiObjectKind::Driver
            && object.parent == unresolved.value->handle) {
            unresolved_drivers.push_back(object.handle);
        }
    }
    assert(unresolved_drivers.size() == 3);
    const auto capture_unresolved_drivers = [&] {
        std::vector<std::string> result;
        for (const auto object : unresolved_drivers) {
            result.push_back(stored_bits(registry, object));
        }
        std::ranges::sort(result);
        return result;
    };

    const auto released_iterator
        = registry.iterate_children(root.value->handle);
    assert(released_iterator
        && registry.release_iterator(released_iterator.value)
            == fsim::runtime::SystemVerilogVpiIteratorError::None);
    const auto replacement_iterator
        = registry.iterate_children(root.value->handle);
    assert(replacement_iterator
        && registry.scan(released_iterator.value).error
            == fsim::runtime::SystemVerilogVpiIteratorError::StaleHandle
        && registry.release_iterator(replacement_iterator.value)
            == fsim::runtime::SystemVerilogVpiIteratorError::None);

    auto& foreign_registry
        = foreign_simulation.systemverilog_vpi_objects();
    assert(foreign_registry.simulation_identity() != identity
        && foreign_registry.lookup(target.value->handle).error
            == fsim::runtime::SystemVerilogVpiObjectError::CrossSimulation);
    const auto foreign_callback
        = foreign_simulation.systemverilog_vpi_callbacks().register_callback(
            { SystemVerilogVpiCallbackKind::ValueChange,
                target.value->handle,
                std::nullopt,
                91,
                [](const SystemVerilogVpiCallbackEvent&) { } });
    assert(foreign_callback.error
            == fsim::runtime::SystemVerilogVpiCallbackError::CrossSimulation
        && foreign_callback.object_error
            == fsim::runtime::SystemVerilogVpiObjectError::CrossSimulation
        && foreign_simulation.systemverilog_vpi_values().apply(
               target.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Deposit,
               stored_value(initial))
            == fsim::runtime::SystemVerilogVpiValueError::CrossSimulation);

    const auto capture_event = [&](const SystemVerilogVpiCallbackEvent& event) {
        assert(event.simulation_identity == identity);
        EventCapture captured { event.kind, { }, event.time.ticks,
            event.time.delta };
        if (event.value) {
            captured.value = packed_bits(*event.value);
        }
        return captured;
    };
    const auto start_callback = callbacks.register_callback(
        { SystemVerilogVpiCallbackKind::StartOfSimulation,
            std::nullopt,
            std::nullopt,
            1,
            [&](const SystemVerilogVpiCallbackEvent& event) {
                capture.start = capture_event(event);
            } });
    const auto value_callback = callbacks.register_callback(
        { SystemVerilogVpiCallbackKind::ValueChange,
            target.value->handle,
            std::nullopt,
            2,
            [&](const SystemVerilogVpiCallbackEvent& event) {
                assert(event.object == target.value->handle && event.value);
                capture.value_events.push_back(capture_event(event));
            } });
    const auto finish_callback = callbacks.register_callback(
        { SystemVerilogVpiCallbackKind::EndOfSimulation,
            std::nullopt,
            std::nullopt,
            3,
            [&](const SystemVerilogVpiCallbackEvent& event) {
                capture.finish = capture_event(event);
            } });
    const auto named_event_callback = callbacks.register_callback(
        { SystemVerilogVpiCallbackKind::ValueChange,
            named_event.value->handle,
            std::nullopt,
            4,
            [&](const SystemVerilogVpiCallbackEvent& event) {
                assert(event.object == named_event.value->handle
                    && !event.value);
                capture.named_events.push_back(capture_event(event));
            } });
    const auto memory_callback = callbacks.register_callback(
        { SystemVerilogVpiCallbackKind::ValueChange,
            memory_word_two.value->handle,
            std::nullopt,
            5,
            [&](const SystemVerilogVpiCallbackEvent& event) {
                assert(event.object == memory_word_two.value->handle
                    && event.value);
                const auto* packed = std::get_if<fsim::runtime::PackedLogic4>(
                    &event.value->payload);
                assert(packed && packed->width() == kMemoryWidth);
                capture.memory_events.push_back(
                    { event.kind, packed->to_msb_string(), event.time.ticks,
                        event.time.delta });
            } });
    assert(start_callback && value_callback && finish_callback
        && named_event_callback && memory_callback);

    assert(values.apply(target.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Force,
               stored_value(forced))
        == fsim::runtime::SystemVerilogVpiValueError::None);
    const auto memory_deposit
        = std::string(kMemoryWidth - 4U, '0') + "1011";
    assert(values.apply(memory_word_three.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Force,
               stored_value(memory_deposit))
            == fsim::runtime::SystemVerilogVpiValueError::ReadOnly
        && values.apply(memory_word_three.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Release)
            == fsim::runtime::SystemVerilogVpiValueError::NotForced);
    const auto signal = simulation.find_signal("vpi_app.evolving");
    assert(signal && simulation.signal_is_forced(*signal)
        && simulation.read_signal(*signal).to_msb_string() == forced
        && forced_bits(registry, target.value->handle) == forced);

    const auto zero_run = simulation.run(0);
    assert(zero_run.status == fsim::runtime::RunStatus::time_limit
        && zero_run.time == 0);
    const auto driven_strength = registry.read_value(strength_driver->handle,
        fsim::runtime::SystemVerilogVpiValueFormat::Strength);
    assert(driven_strength
        && driven_strength.strength.state == fsim::runtime::Logic4::one
        && driven_strength.strength.drive == strength_value.strength.drive);
    capture.unresolved_drivers_at_zero = capture_unresolved_drivers();
    assert(std::ranges::find(capture.unresolved_drivers_at_zero, initial)
        != capture.unresolved_drivers_at_zero.end());

    const auto first_run = simulation.run(5);
    assert(first_run.status == fsim::runtime::RunStatus::time_limit
        && first_run.time == 5);
    capture.stored_at_five = stored_bits(registry, target.value->handle);
    capture.unresolved_drivers_at_five = capture_unresolved_drivers();
    assert(capture.stored_at_five == middle
        && simulation.read_signal(*signal).to_msb_string() == forced
        && stored_bits(registry, driver->handle) == middle
        && std::ranges::find(capture.unresolved_drivers_at_five, next)
            != capture.unresolved_drivers_at_five.end()
        && capture.unresolved_drivers_at_zero
            != capture.unresolved_drivers_at_five);

    assert(values.apply(target.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Deposit,
               stored_value(deposited))
        == fsim::runtime::SystemVerilogVpiValueError::None);
    assert(values.apply(memory_word_three.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Deposit,
               stored_value(memory_deposit))
        == fsim::runtime::SystemVerilogVpiValueError::None);
    capture.deposited_beneath_force
        = stored_bits(registry, target.value->handle);
    assert(capture.deposited_beneath_force == deposited
        && forced_bits(registry, target.value->handle) == forced
        && simulation.read_signal(*signal).to_msb_string() == forced);

    const auto second_run = simulation.run(10);
    assert(second_run.status == fsim::runtime::RunStatus::time_limit
        && second_run.time == 10);
    capture.stored_at_ten = stored_bits(registry, target.value->handle);
    const auto at_ten
        = time.query(fsim::runtime::SystemVerilogVpiTimeFormat::IntegerTicks);
    assert(at_ten && at_ten.ticks == simulation.now()
        && at_ten.delta == simulation.delta() && at_ten.value.high == 0
        && at_ten.value.low == 20);
    capture.time_queries.emplace_back(at_ten.ticks, at_ten.delta);
    capture.memory_probe_at_ten
        = parameter_bits(registry, memory_probe.value->handle, kMemoryWidth);
    assert(capture.stored_at_ten == next
        && forced_bits(registry, target.value->handle) == forced
        && simulation.read_signal(*signal).to_msb_string() == forced
        && stored_bits(registry, driver->handle) == next
        && capture.memory_probe_at_ten == memory_deposit);

    assert(values.apply(target.value->handle,
               fsim::runtime::SystemVerilogVpiWriteKind::Release)
        == fsim::runtime::SystemVerilogVpiValueError::None);
    capture.released_at_ten
        = simulation.read_signal(*signal).to_msb_string();
    assert(!simulation.signal_is_forced(*signal)
        && capture.released_at_ten == next);

    const auto stop = control.submit(
        { fsim::runtime::SystemVerilogVpiControlOperation::Stop,
            std::nullopt,
            std::nullopt });
    assert(stop);
    const auto stopped = simulation.run();
    capture.stopped = run_point(stopped);
    assert(stopped.status == fsim::runtime::RunStatus::stopped
        && stopped.time == 10
        && control.status(stop.value).status
            == fsim::runtime::SystemVerilogVpiControlStatus::Applied
        && control.state()
            == fsim::runtime::SystemVerilogVpiControlState::Stopped
        && control.resume()
            == fsim::runtime::SystemVerilogVpiControlError::None);

    const auto third_run = simulation.run(15);
    assert(third_run.status == fsim::runtime::RunStatus::time_limit
        && third_run.time == 15);
    capture.stored_at_fifteen = stored_bits(registry, target.value->handle);
    const auto at_fifteen
        = time.query(fsim::runtime::SystemVerilogVpiTimeFormat::IntegerTicks);
    assert(at_fifteen && at_fifteen.ticks == simulation.now()
        && at_fifteen.delta == simulation.delta()
        && at_fifteen.value.high == 0 && at_fifteen.value.low == 30);
    capture.time_queries.emplace_back(at_fifteen.ticks, at_fifteen.delta);
    assert(capture.stored_at_fifteen == late
        && simulation.read_signal(*signal).to_msb_string() == late);

    const auto finish = control.submit(
        { fsim::runtime::SystemVerilogVpiControlOperation::Finish,
            std::nullopt,
            std::nullopt });
    assert(finish);
    const auto finished = simulation.run();
    capture.finished = run_point(finished);
    assert(finished.status == fsim::runtime::RunStatus::stopped
        && finished.time == 15 && simulation.finished()
        && control.status(finish.value).status
            == fsim::runtime::SystemVerilogVpiControlStatus::Applied
        && control.state()
            == fsim::runtime::SystemVerilogVpiControlState::Finished
        && control.resume()
            == fsim::runtime::SystemVerilogVpiControlError::Terminal);

    assert(capture.start.kind
            == SystemVerilogVpiCallbackKind::StartOfSimulation
        && capture.start.time == 0);
    assert(capture.finish.kind
            == SystemVerilogVpiCallbackKind::EndOfSimulation
        && capture.finish.time == 15);
    if (capture.value_events.size() != 3) {
        std::cerr << "unexpected VPI value events:";
        for (const auto& event : capture.value_events) {
            std::cerr << ' ' << static_cast<unsigned>(event.kind) << '@'
                      << event.time << ':' << event.delta << '=' << event.value;
        }
        std::cerr << '\n';
        assert(capture.value_events.size() == 3);
    }
    assert(capture.value_events[0].kind
            == SystemVerilogVpiCallbackKind::ValueChange
        && capture.value_events[0].value == forced
        && capture.value_events[0].time == 0);
    assert(capture.value_events[1].kind
            == SystemVerilogVpiCallbackKind::ValueChange
        && capture.value_events[1].value == next
        && capture.value_events[1].time == 10);
    assert(capture.value_events[2].kind
            == SystemVerilogVpiCallbackKind::ValueChange
        && capture.value_events[2].value == late
        && capture.value_events[2].time == 15);
    assert(capture.memory_events.size() == 2
        && capture.memory_events[0].time == 0
        && capture.memory_events[0].value
            == std::string(kMemoryWidth - 4U, '0') + "10XZ"
        && capture.memory_events[1].time == 5
        && capture.memory_events[1].value
            == std::string(kMemoryWidth - 4U, '0') + "01ZX");
    assert(callbacks.status(start_callback.value).status
            == fsim::runtime::SystemVerilogVpiCallbackStatus::Fired
        && callbacks.status(finish_callback.value).status
            == fsim::runtime::SystemVerilogVpiCallbackStatus::Fired
        && callbacks.status(value_callback.value).status
            == fsim::runtime::SystemVerilogVpiCallbackStatus::Active
        && callbacks.remove_callback(value_callback.value)
            == fsim::runtime::SystemVerilogVpiCallbackError::None
        && callbacks.status(value_callback.value).status
            == fsim::runtime::SystemVerilogVpiCallbackStatus::Removed);
    return capture;
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path()
        / ("fsim-vpi-application-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(temporary.path);
    const auto source = temporary.path / "vpi-application.v";
    const auto initial = make_bits({ 0, 68, 136 });
    const auto middle = make_bits({ 1, 37, 97, 135 });
    const auto next = make_bits({ 2, 41, 89, 134 });
    const auto late = make_bits({ 3, 47, 83, 133 });
    const auto deposited = make_bits({ 4, 53, 79, 132 });
    auto forced = make_bits({ 5, 59, 73, 131 });
    forced[0] = 'X';
    forced[1] = 'Z';
    const std::string unknown(kWideWidth, 'x');
    {
        std::ofstream output(source, std::ios::binary);
        output << "module vpi_leaf #(\n"
               << "  parameter [7:0] BYTE_PARAM = 8'h11,\n"
               << "  parameter signed [15:0] LEAF_SIGNED_PARAM = -16'sd2,\n"
               << "  parameter [11:0] LEAF_STATE_PARAM = 12'b10xz01zx1100\n"
               << ") (input [136:0] input_value, output [136:0] mirror);\n"
               << "  assign mirror = input_value;\n"
               << "endmodule\n"
               << "module vpi_app #(\n"
               << "  parameter [7:0] EIGHT_PARAM = 8'hff,\n"
               << "  parameter [15:0] SIXTEEN_PARAM = 16'h00ff,\n"
               << "  parameter signed [15:0] SIGNED_PARAM = -16'sd7,\n"
               << "  parameter [11:0] STATE_PARAM = 12'b10xz01zx1100\n"
               << ");\n"
               << "  reg [136:0] evolving;\n"
               << "  localparam IMPLICIT_PARAM = 5;\n"
               << "  reg [136:0] hidden_a;\n"
               << "  reg [136:0] hidden_b;\n"
               << "  wire [136:0] unresolved;\n"
               << "  wire [136:0] mirror;\n"
               << "  wire [136:0] default_mirror;\n"
               << "  integer signed_counter;\n"
               << "  trireg retained;\n"
               << "  tri0 pulled;\n"
               << "  wand combined;\n"
               << "  uwire unique_net;\n"
               << "  event fired;\n"
               << "  reg event_seen;\n"
               << "  reg [70:0] words [3:1];\n"
               << "  reg [70:0] memory_probe;\n"
               << "  wire strength_net;\n"
               << "  assign (pull0, supply1) strength_net = 1'b1;\n"
               << "  always @(fired) event_seen = 1'b1;\n"
               << "  vpi_leaf #(\n"
               << "    .BYTE_PARAM(8'ha5),\n"
               << "    .LEAF_SIGNED_PARAM(-16'sd9),\n"
               << "    .LEAF_STATE_PARAM(12'b01zx10xz0011)\n"
               << "  ) leaf(evolving, mirror);\n"
               << "  vpi_leaf leaf_default(evolving, default_mirror);\n"
               << "  genvar lane;\n"
               << "  generate\n"
               << "    for (lane = 0; lane < 2; lane = lane + 1) begin : generated\n"
               << "      reg leaf_value;\n"
               << "      initial leaf_value = lane;\n"
               << "    end\n"
               << "  endgenerate\n"
               << "  assign unresolved = hidden_a;\n"
               << "  assign unresolved = hidden_b;\n"
               << "  assign unresolved = 137'b" << unknown << ";\n"
               << "  initial begin\n"
               << "    words[3] = 71'b"
               << std::string(kMemoryWidth - 4U, '0') << "1101;\n"
               << "    words[2] = 71'b"
               << std::string(kMemoryWidth - 4U, '0') << "10xz;\n"
               << "    #5 words[2] = 71'b"
               << std::string(kMemoryWidth - 4U, '0') << "01zx;\n"
               << "    #5 memory_probe = words[3];\n"
               << "  end\n"
               << "  initial begin\n"
               << "    signed_counter = -1;\n"
               << "    -> fired;\n"
               << "    hidden_a = 137'b" << initial << ";\n"
               << "    hidden_b = 137'b" << middle << ";\n"
               << "    #5 hidden_a = 137'b" << next << ";\n"
               << "  end\n"
               << "  initial begin\n"
               << "    evolving = 137'b" << initial << ";\n"
               << "    #5 evolving = 137'b" << middle << ";\n"
               << "    #5 evolving = 137'b" << next << ";\n"
               << "    #5 evolving = 137'b" << late << ";\n"
               << "    #5 $finish;\n"
               << "  end\n"
               << "endmodule\n";
        assert(output.good());
    }

    const auto interpreter = execute(
        make_config(temporary.path, source, fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::interpreter,
        initial, middle, next, late, deposited, forced);
    const auto compiled_o0 = execute(
        make_config(temporary.path, source, fsim::project::Optimization::o0),
        fsim::app::SimulationEngine::debug,
        initial, middle, next, late, deposited, forced);
    const auto compiled_o2 = execute(
        make_config(temporary.path, source, fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::compiled,
        initial, middle, next, late, deposited, forced);
    assert(interpreter.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled_o0.compiled_processes > 0
        && compiled_o2.compiled_processes > 0);
#else
    assert(compiled_o0.compiled_processes == 0
        && compiled_o2.compiled_processes == 0);
#endif
    assert(interpreter.observably_equal(compiled_o0)
        && interpreter.observably_equal(compiled_o2));
    assert(interpreter.named_events.size() == 1
        && interpreter.named_events.front().value.empty());
    return 0;
}
