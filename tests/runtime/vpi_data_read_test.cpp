// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_data_read.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using namespace fsim::runtime;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

SystemVerilogVpiTypeInfo logic_type()
{
    SystemVerilogVpiTypeInfo type;
    type.category = SystemVerilogVpiValueCategory::Logic4;
    type.width = 4U;
    return type;
}

SystemVerilogVpiStoredValue logic_value(const std::string_view text)
{
    return { PackedLogic4::from_msb_string(text), std::nullopt };
}

SystemVerilogVpiObjectResult create_logic(
    SystemVerilogVpiObjectRegistry& objects,
    const fsim_vpi_handle_v1 parent,
    const std::string_view name)
{
    SystemVerilogVpiObjectDescriptor descriptor;
    descriptor.kind = SystemVerilogVpiObjectKind::Variable;
    descriptor.parent = parent;
    descriptor.name = name;
    descriptor.source = SystemVerilogVpiSourceLocation {
        "rtl/reader_fixture.sv", 11U, 7U
    };
    descriptor.type = logic_type();
    return objects.create(descriptor);
}

void test_identities_and_initialization()
{
    static_assert(static_cast<std::int32_t>(
                      SystemVerilogVpiDataReadObjectKind::TraverseObject)
            == 800
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadObjectKind::Collection)
            == 810
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadObjectKind::ObjectCollection)
            == 811
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadObjectKind::TraverseCollection)
            == 812
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadProperty::IsLoaded)
            == 820
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadProperty::BelongsToExtension)
            == 824
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadAccess::LimitedInteractive)
            == 830
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadAccess::PostProcess)
            == 832
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadIteration::DataLoaded)
            == 850
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadControl::MinimumTime)
            == 860
        && static_cast<std::int32_t>(
               SystemVerilogVpiDataReadControl::Time)
            == 874);

    SystemVerilogVpiObjectRegistry objects { 901U };
    SystemVerilogVpiDataReadService invalid_access(objects,
        static_cast<SystemVerilogVpiDataReadAccess>(999));
    SystemVerilogVpiDataReadService missing_database(objects,
        SystemVerilogVpiDataReadAccess::Interactive);
    SystemVerilogVpiDataReadLimits zero_limits;
    zero_limits.maximum_value_changes = 0U;
    SystemVerilogVpiDataReadService invalid_limits(objects,
        SystemVerilogVpiDataReadAccess::LimitedInteractive,
        { }, { }, zero_limits);
    require(!invalid_access.valid() && !missing_database.valid()
            && !invalid_limits.valid(),
        "reader initialization rejects unknown modes, missing history databases, and zero bounds");
}

void test_live_loading_traversal_and_collections()
{
    SystemVerilogVpiObjectRegistry objects { 902U };
    const auto top = objects.create(
        SystemVerilogVpiObjectKind::Root, 0U, "top");
    const auto dut = objects.create(
        SystemVerilogVpiObjectKind::Module, top.value, "dut");
    const auto nested_scope = objects.create(
        SystemVerilogVpiObjectKind::Module, dut.value, "nested");
    const auto state = create_logic(objects, dut.value, "state");
    const auto nested = create_logic(objects, nested_scope.value, "nested_state");
    require(top && dut && nested_scope && state && nested,
        "reader fixture publishes hierarchy, source, and type identities");
    require(objects.bind_value(state.value, logic_value("0000"))
                == SystemVerilogVpiValueError::None
            && objects.bind_value(nested.value, logic_value("0011"))
                == SystemVerilogVpiValueError::None,
        "reader fixture binds canonical initial values");

    SystemVerilogVpiDataReadPosition now { 0U, 0U };
    SystemVerilogVpiDataReadService reader(objects,
        SystemVerilogVpiDataReadAccess::LimitedInteractive,
        { }, [&] { return now; });
    require(reader.valid() && !reader.closed()
            && reader.extension_identity() != 0U
            && reader.database_name().empty()
            && reader.access()
                == SystemVerilogVpiDataReadAccess::LimitedInteractive,
        "limited interactive reader retains its simulation-owned extension identity");

    require(reader.load_init(std::nullopt, dut.value, 1U)
                == SystemVerilogVpiDataReadError::None
            && reader.property(
                    SystemVerilogVpiDataReadProperty::IsLoaded,
                    state.value).value
            && !reader.property(
                    SystemVerilogVpiDataReadProperty::IsLoaded,
                    nested.value).value,
        "bounded scope loading selects value objects without loading structural scopes or deeper levels");
    require(reader.load_init(std::nullopt, dut.value, 0U)
                == SystemVerilogVpiDataReadError::None,
        "zero-level scope selection recursively loads the remaining value objects");
    const auto loaded = reader.loaded_objects(dut.value);
    require(loaded && loaded.objects.size() == 2U
            && loaded.objects[0] == state.value
            && loaded.objects[1] == nested.value,
        "loaded-object iteration is hierarchy filtered and declaration ordered");

    now = { 5U, 0U };
    require(objects.update_bound_value(state.value, logic_value("0001"))
                == SystemVerilogVpiValueError::None,
        "live reader observes the first kernel value change");
    now = { 7U, 2U };
    require(objects.update_bound_value(nested.value, logic_value("0111"))
                == SystemVerilogVpiValueError::None,
        "live reader preserves delta identity in nested hierarchy changes");
    now = { 9U, 0U };
    require(objects.update_bound_value(state.value, logic_value("0010"))
                == SystemVerilogVpiValueError::None,
        "live reader observes a second ordered kernel value change");

    const auto state_traverse = reader.create_traverse(state.value);
    require(state_traverse
            && reader.property(
                   SystemVerilogVpiDataReadProperty::HasDataValueChange,
                   state_traverse.value).value
            && reader.time(state_traverse.value,
                   SystemVerilogVpiDataReadControl::MinimumTime).value
                == SystemVerilogVpiDataReadPosition { 0U, 0U }
            && reader.time(state_traverse.value,
                   SystemVerilogVpiDataReadControl::MaximumTime).value
                == SystemVerilogVpiDataReadPosition { 9U, 0U }
            && reader.value(state_traverse.value).value
                == logic_value("0000"),
        "traverse creation starts at the retained minimum with exact value and range metadata");
    const auto next = reader.go_to(state_traverse.value,
        SystemVerilogVpiDataReadControl::NextValueChange);
    const auto jumped = reader.go_to(state_traverse.value,
        SystemVerilogVpiDataReadControl::Time,
        SystemVerilogVpiDataReadPosition { 6U, 0U });
    const auto before_history = reader.go_to(state_traverse.value,
        SystemVerilogVpiDataReadControl::Time,
        SystemVerilogVpiDataReadPosition { 0U, 0U });
    require(next && jumped && before_history
            && reader.time(next.value).value
                == SystemVerilogVpiDataReadPosition { 5U, 0U }
            && reader.value(next.value).value == logic_value("0001")
            && reader.time(jumped.value).value
                == SystemVerilogVpiDataReadPosition { 6U, 0U }
            && reader.value(jumped.value).value == logic_value("0001")
            && !reader.property(
                    SystemVerilogVpiDataReadProperty::HasValueChange,
                    jumped.value).value
            && !reader.property(
                    SystemVerilogVpiDataReadProperty::HasNoValue,
                    before_history.value).value,
        "forward walking and time jumps retain the value at or before the requested position without inventing a change");
    const auto maximum = reader.go_to(state_traverse.value,
        SystemVerilogVpiDataReadControl::MaximumTime);
    const auto previous = reader.go_to(maximum.value,
        SystemVerilogVpiDataReadControl::PreviousValueChange);
    require(maximum && previous
            && reader.value(maximum.value).value == logic_value("0010")
            && reader.value(previous.value).value == logic_value("0001")
            && reader.go_to(maximum.value,
                   SystemVerilogVpiDataReadControl::NextValueChange).error
                == SystemVerilogVpiDataReadError::NoValueChange,
        "maximum and reverse traversal stop cleanly at retained history boundaries");

    auto objects_collection = reader.create_object_collection();
    objects_collection = reader.create_object_collection(
        objects_collection.value, state.value);
    objects_collection = reader.create_object_collection(
        objects_collection.value, nested.value);
    const auto variables = reader.filter(objects_collection.value,
        { SystemVerilogVpiObjectKind::Variable, std::nullopt, true });
    require(objects_collection && variables
            && reader.members(objects_collection.value).objects.size() == 2U
            && reader.members(variables.value).objects.size() == 2U,
        "design collections add unique objects and filter by ordinary VPI type without mutating the source");

    const auto nested_traverse = reader.create_traverse(nested.value);
    auto traverses = reader.create_traverse_collection();
    traverses = reader.create_traverse_collection(
        traverses.value, state_traverse.value);
    traverses = reader.create_traverse_collection(
        traverses.value, nested_traverse.value);
    const auto group_minimum = reader.go_to(traverses.value,
        SystemVerilogVpiDataReadControl::MinimumTime);
    const auto group_next = reader.go_to(group_minimum.value,
        SystemVerilogVpiDataReadControl::NextValueChange);
    const auto changed = reader.filter(group_next.value,
        { std::nullopt,
            SystemVerilogVpiDataReadProperty::HasValueChange, true });
    require(group_minimum && group_next && changed
            && reader.time(group_next.value).value
                == SystemVerilogVpiDataReadPosition { 5U, 0U }
            && reader.members(group_next.value).traverses.size() == 2U
            && reader.members(changed.value).traverses.size() == 1U,
        "collection traversal advances to the earliest member change and filtering selects only members changing there");

    require(reader.unload(state.value)
                == SystemVerilogVpiDataReadError::None
            && reader.create_traverse(state.value).error
                == SystemVerilogVpiDataReadError::NotLoaded
            && reader.value(maximum.value).value == logic_value("0010"),
        "unloading prevents new traverse creation while existing traverse handles remain readable");
    require(reader.close() == SystemVerilogVpiDataReadError::None
            && reader.close() == SystemVerilogVpiDataReadError::Closed
            && reader.load(state.value)
                == SystemVerilogVpiDataReadError::Closed
            && reader.value(previous.value).value == logic_value("0001"),
        "closing prevents new load activity while already-created reader objects remain usable");
    require(reader.release(objects_collection.value)
                == SystemVerilogVpiDataReadError::None
            && reader.members(objects_collection.value).error
                == SystemVerilogVpiDataReadError::ReleasedHandle,
        "reader objects have explicit released-handle lifetimes");
}

void test_history_modes_failures_and_transactionality()
{
    SystemVerilogVpiObjectRegistry objects { 903U };
    const auto top = objects.create(
        SystemVerilogVpiObjectKind::Root, 0U, "top");
    const auto first = create_logic(objects, top.value, "first");
    const auto second = create_logic(objects, top.value, "second");
    require(first && second
            && objects.bind_value(first.value, logic_value("0000"))
                == SystemVerilogVpiValueError::None
            && objects.bind_value(second.value, logic_value("1111"))
                == SystemVerilogVpiValueError::None,
        "history-mode fixture binds two readable objects");

    SystemVerilogVpiDataReadLimits one_object;
    one_object.maximum_loaded_objects = 1U;
    SystemVerilogVpiDataReadService bounded(objects,
        SystemVerilogVpiDataReadAccess::LimitedInteractive,
        { }, { }, one_object);
    auto both = bounded.create_object_collection();
    both = bounded.create_object_collection(both.value, first.value);
    both = bounded.create_object_collection(both.value, second.value);
    require(bounded.load(both.value)
                == SystemVerilogVpiDataReadError::ResourceLimit
            && !bounded.property(
                    SystemVerilogVpiDataReadProperty::IsLoaded,
                    first.value).value
            && !bounded.property(
                    SystemVerilogVpiDataReadProperty::IsLoaded,
                    second.value).value,
        "collection load rejects its complete over-limit transaction without partial publication");

    SystemVerilogVpiDataReadPosition interactive_time { 1U, 0U };
    SystemVerilogVpiDataReadService interactive(objects,
        SystemVerilogVpiDataReadAccess::Interactive, "history-db",
        [&] { return interactive_time; });
    interactive_time = { 2U, 0U };
    require(objects.update_bound_value(first.value, logic_value("0101"))
                == SystemVerilogVpiValueError::None,
        "interactive reader observes history before selection");
    interactive_time = { 3U, 0U };
    require(interactive.load(first.value)
                == SystemVerilogVpiDataReadError::None,
        "interactive reader loads an object after its earlier history exists");
    const auto interactive_traverse = interactive.create_traverse(first.value);
    require(interactive_traverse
            && interactive.time(interactive_traverse.value,
                   SystemVerilogVpiDataReadControl::MinimumTime).value
                == SystemVerilogVpiDataReadPosition { 2U, 0U },
        "interactive access retains pre-load value-change history");

    SystemVerilogVpiDataReadService database(objects,
        SystemVerilogVpiDataReadAccess::PostProcess, "decoded-wave-db");
    require(database.publish_value_change(first.value, { 10U, 0U },
                logic_value("1000"))
                == SystemVerilogVpiDataReadError::None
            && database.publish_value_change(first.value, { 12U, 4U },
                   logic_value("1100"))
                == SystemVerilogVpiDataReadError::None
            && database.publish_value_change(first.value, { 11U, 0U },
                   logic_value("1010"))
                == SystemVerilogVpiDataReadError::OutOfOrder
            && database.publish_value_change(second.value, { 1U, 0U },
                   logic_value("1"))
                == SystemVerilogVpiDataReadError::TypeMismatch,
        "post-process import validates type and monotonically ordered time before publishing a sample");
    require(database.load(first.value)
                == SystemVerilogVpiDataReadError::None,
        "post-process data becomes traversable only after explicit load");
    const auto database_traverse = database.create_traverse(first.value);
    const auto before_database = database.go_to(database_traverse.value,
        SystemVerilogVpiDataReadControl::Time,
        SystemVerilogVpiDataReadPosition { 5U, 0U });
    require(database_traverse && before_database
            && database.value(database_traverse.value).value
                == logic_value("1000")
            && database.property(
                   SystemVerilogVpiDataReadProperty::HasNoValue,
                   before_database.value).value
            && database.value(before_database.value).error
                == SystemVerilogVpiDataReadError::NoValue
            && database.property(
                   SystemVerilogVpiDataReadProperty::BelongsToExtension,
                   database_traverse.value).value,
        "post-process traverse reads canonical imported values and extension ownership");
    require(database.property(
                SystemVerilogVpiDataReadProperty::BelongsToExtension,
                interactive_traverse.value).error
                == SystemVerilogVpiDataReadError::CrossExtension
            && database.go_to(database_traverse.value,
                   static_cast<SystemVerilogVpiDataReadControl>(999)).error
                == SystemVerilogVpiDataReadError::InvalidControl
            && database.filter(database_traverse.value,
                   { std::nullopt, std::nullopt, true }).error
                == SystemVerilogVpiDataReadError::InvalidProperty,
        "reader calls distinguish cross-extension handles, unknown controls, and malformed filters");
}

} // namespace

int main()
{
    test_identities_and_initialization();
    test_live_loading_traversal_and_collections();
    test_history_modes_failures_and_transactionality();
}
