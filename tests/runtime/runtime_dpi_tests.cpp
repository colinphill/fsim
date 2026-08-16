// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_callback.hpp"
#include "fsim/runtime/dpi_marshalling.hpp"
#include "fsim/runtime/dpi_plugin.hpp"
#include "fsim/runtime/dpi_scope.hpp"
#include "fsim/runtime/dpi_task.hpp"
#include "runtime_test_support.hpp"

#include <bit>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

extern "C" std::size_t fsim_dpi_abi_c_descriptor_size();
extern "C" const char* fsim_dpi_abi_c_descriptor_symbol();

namespace fsim::tests::runtime {

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string { message });
        }
    }

} // namespace

void test_systemverilog_dpi_scalar_marshalling()
{
    using namespace fsim::runtime;
    using Domain = SystemVerilogDpiScalarDomain;
    using Error = SystemVerilogDpiMarshallingError;

    const auto logic = PackedLogic4::from_msb_string(
        "1XZ00101101001011010010110100101101001011010010110100101101001010");
    const auto marshalled = marshal_systemverilog_dpi_scalar(
        logic, Domain::Logic4);
    require(
        marshalled && marshalled.value.width == 65
            && marshalled.value.aval.size() == 3
            && marshalled.value.bval.size() == 3,
        "wide four-state DPI input owns exact 32-bit aval/bval planes");
    const auto round_trip = unmarshal_systemverilog_dpi_scalar(
        marshalled.value, logic.width());
    require(
        round_trip && round_trip.value == logic,
        "wide four-state DPI scalar round-trips X/Z and word boundaries");

    const auto bits = PackedLogic4::from_msb_string("10100101");
    const auto bit_payload = marshal_systemverilog_dpi_scalar(
        bits, Domain::Bit2);
    require(
        bit_payload && bit_payload.value.aval.size() == 1
            && bit_payload.value.aval.front() == UINT32_C(0xa5)
            && bit_payload.value.bval.empty()
            && unmarshal_systemverilog_dpi_scalar(
                   bit_payload.value, 8)
                    .value
                == bits,
        "two-state DPI scalar uses one exact aval plane");
    require(
        marshal_systemverilog_dpi_scalar(
            PackedLogic4::from_msb_string("10X1"),
            Domain::Bit2)
                .error
            == Error::UnknownValue,
        "two-state DPI input rejects X/Z instead of coercing it");

    auto wrong_plane = marshalled.value;
    wrong_plane.bval.pop_back();
    auto dirty_high_bits = bit_payload.value;
    dirty_high_bits.aval.front() |= UINT32_C(0x80000000);
    require(
        unmarshal_systemverilog_dpi_scalar(
            wrong_plane, logic.width())
                    .error
                == Error::PlaneSize
            && unmarshal_systemverilog_dpi_scalar(
                   dirty_high_bits, 8)
                    .error
                == Error::UnusedBits
            && unmarshal_systemverilog_dpi_scalar(
                   bit_payload.value, 7)
                    .error
                == Error::WidthMismatch,
        "DPI scalar output validates plane size, high bits, and exact width");
    require(
        marshal_systemverilog_dpi_scalar(
            PackedLogic4 { }, Domain::Logic4)
                    .error
                == Error::EmptyWidth
            && marshal_systemverilog_dpi_scalar(
                   logic, Domain::Logic4, 64)
                    .error
                == Error::WidthLimit,
        "DPI scalar marshalling enforces empty and bounded-width contracts");
}

void test_systemverilog_dpi_real_string_chandle_marshalling()
{
    using namespace fsim::runtime;
    using Error = SystemVerilogDpiMarshallingError;
    using Mode = SystemVerilogDpiTransferMode;
    using RealKind = SystemVerilogDpiRealKind;

    const auto negative_zero = marshal_systemverilog_dpi_real(
        SystemVerilogScalarValue::real(-0.0), Mode::Inout);
    require(
        negative_zero
            && negative_zero.value.bits == UINT64_C(0x8000000000000000)
            && unmarshal_systemverilog_dpi_real(
                   negative_zero.value, RealKind::Real, Mode::Inout)
                    .value.bits
                == negative_zero.value.bits,
        "DPI real marshalling preserves exact binary64 negative-zero bits");
    auto shortreal = marshal_systemverilog_dpi_real(
        SystemVerilogScalarValue::shortreal(1.25F), Mode::Output);
    require(
        shortreal && (shortreal.value.bits >> 32U) == 0
            && unmarshal_systemverilog_dpi_real(
                   shortreal.value, RealKind::ShortReal, Mode::Output)
                    .value.as_shortreal()
                == 1.25F,
        "DPI shortreal marshalling preserves exact bounded binary32 bits");
    shortreal.value.bits |= UINT64_C(0x100000000);
    require(
        unmarshal_systemverilog_dpi_real(
            shortreal.value, RealKind::ShortReal, Mode::Output)
                    .error
                == Error::UnusedBits
            && marshal_systemverilog_dpi_real(
                   SystemVerilogScalarValue::real(
                       std::numeric_limits<double>::infinity()),
                   Mode::Input)
                    .error
                == Error::Nonfinite
            && unmarshal_systemverilog_dpi_real(
                   negative_zero.value, RealKind::Realtime, Mode::Inout)
                    .error
                == Error::KindMismatch,
        "DPI real outputs reject dirty bits, nonfinite values, and kind mismatch");

    const std::string unicode = "Gr\xC3\xBC\xC3\x9F\x65";
    const auto string_value = marshal_systemverilog_dpi_string(
        unicode, Mode::Ref);
    require(
        string_value && string_value.value.bytes == unicode
            && unmarshal_systemverilog_dpi_string(
                   string_value.value, Mode::Ref)
                    .value.bytes
                == unicode,
        "DPI strings preserve valid UTF-8 bytes through ref writeback");
    const std::string embedded_nul { "a\0b", 3 };
    require(
        marshal_systemverilog_dpi_string(
            embedded_nul, Mode::Input)
                    .error
                == Error::EmbeddedNul
            && marshal_systemverilog_dpi_string(
                   std::string { "\xC0\x80", 2 }, Mode::Input)
                    .error
                == Error::InvalidUtf8
            && marshal_systemverilog_dpi_string(
                   "bounded", Mode::Input, 3)
                    .error
                == Error::StringLimit
            && unmarshal_systemverilog_dpi_string(
                   string_value.value, Mode::Input)
                    .error
                == Error::DirectionMismatch,
        "DPI strings reject NUL, malformed UTF-8, resource excess, and input writeback");

    SystemVerilogChandleRegistry registry;
    const auto handle = registry.create({ "dpi-object", "fixture", { } });
    const auto borrowed = marshal_systemverilog_dpi_chandle(
        registry, handle, Mode::Input);
    const auto writable = marshal_systemverilog_dpi_chandle(
        registry, handle, Mode::Inout);
    require(
        borrowed && borrowed.value.handle == handle && borrowed.value.borrowed
            && writable && !writable.value.borrowed
            && unmarshal_systemverilog_dpi_chandle(
                   registry, writable.value, Mode::Inout)
                    .value.handle
                == handle,
        "DPI chandles preserve stable registry identity and borrow state");
    require(registry.release(handle), "DPI chandle fixture release");
    require(
        marshal_systemverilog_dpi_chandle(
            registry, handle, Mode::Input)
                    .error
                == Error::StaleHandle
            && unmarshal_systemverilog_dpi_chandle(
                   registry, writable.value, Mode::Inout)
                    .error
                == Error::StaleHandle
            && unmarshal_systemverilog_dpi_chandle(
                   registry, borrowed.value, Mode::Input)
                    .error
                == Error::DirectionMismatch,
        "DPI chandles reject stale identities and borrowed input writeback");
}

void test_systemverilog_dpi_composite_marshalling()
{
    using namespace fsim::runtime;
    using Kind = SystemVerilogDpiCompositeKind;
    using Domain = SystemVerilogDpiScalarDomain;
    using Error = SystemVerilogDpiMarshallingError;
    using Mode = SystemVerilogDpiTransferMode;

    SystemVerilogDpiTypeDescriptor nibble;
    nibble.kind = Kind::Scalar;
    nibble.scalar_domain = Domain::Logic4;
    nibble.width = 4;
    SystemVerilogDpiTypeDescriptor state;
    state.kind = Kind::Enum;
    state.scalar_domain = Domain::Bit2;
    state.width = 2;
    state.enum_values = {
        PackedLogic4::from_msb_string("00"),
        PackedLogic4::from_msb_string("01"),
        PackedLogic4::from_msb_string("10")
    };
    SystemVerilogDpiTypeDescriptor record;
    record.kind = Kind::Struct;
    record.children = { nibble, state };
    record.member_names = { "payload", "state" };
    SystemVerilogDpiTypeDescriptor records;
    records.kind = Kind::FixedArray;
    records.element_count = 2;
    records.children = { record };

    const auto layout = layout_systemverilog_dpi_composite(records);
    const std::vector<PackedLogic4> leaves {
        PackedLogic4::from_msb_string("10XZ"),
        PackedLogic4::from_msb_string("01"),
        PackedLogic4::from_msb_string("0011"),
        PackedLogic4::from_msb_string("10")
    };
    const auto payload = marshal_systemverilog_dpi_composite(
        records, leaves, Mode::Ref);
    require(
        layout && layout.value.leaf_count == 4
            && layout.value.total_bits == 12
            && payload && payload.value.leaves.size() == 4
            && unmarshal_systemverilog_dpi_composite(
                   payload.value, records, Mode::Ref)
                    .value
                == leaves,
        "DPI fixed-array/struct/enum layout and canonical leaves round-trip");

    SystemVerilogDpiTypeDescriptor wide_enum;
    wide_enum.kind = Kind::Enum;
    wide_enum.scalar_domain = Domain::Logic4;
    wide_enum.width = 129;
    wide_enum.enum_values = {
        PackedLogic4::from_msb_string("1" + std::string(128, '0')),
        PackedLogic4::from_msb_string(
            "X" + std::string(127, '0') + "Z"),
        PackedLogic4::from_msb_string("0" + std::string(128, '1'))
    };
    const std::vector<PackedLogic4> wide_leaf {
        wide_enum.enum_values[1]
    };
    const auto wide_layout = layout_systemverilog_dpi_composite(wide_enum);
    const auto wide_payload = marshal_systemverilog_dpi_composite(
        wide_enum, wide_leaf, Mode::Output);
    require(
        wide_layout && wide_layout.value.total_bits == 129
            && wide_payload
            && unmarshal_systemverilog_dpi_composite(
                   wide_payload.value, wide_enum, Mode::Output)
                    .value
                == wide_leaf,
        "DPI enum descriptors preserve arbitrary-width value and unknown-plane identity");

    auto invalid_enum = leaves;
    invalid_enum.back() = PackedLogic4::from_msb_string("11");
    auto oversized = records;
    oversized.element_count = maximum_dpi_composite_elements + 1U;
    auto mismatched = records;
    mismatched.element_count = 1;
    auto wrong_width_enum = wide_enum;
    wrong_width_enum.enum_values.back()
        = PackedLogic4::from_msb_string("1");
    auto unknown_two_state_enum = wide_enum;
    unknown_two_state_enum.scalar_domain = Domain::Bit2;
    require(
        marshal_systemverilog_dpi_composite(
            records, invalid_enum, Mode::Input)
                    .error
                == Error::EnumValue
            && layout_systemverilog_dpi_composite(oversized).error
                == Error::ElementCount
            && unmarshal_systemverilog_dpi_composite(
                   payload.value, mismatched, Mode::Ref)
                    .error
                == Error::InvalidDescriptor
            && unmarshal_systemverilog_dpi_composite(
                   payload.value, records, Mode::Input)
                    .error
                == Error::DirectionMismatch
            && layout_systemverilog_dpi_composite(wrong_width_enum).error
                == Error::InvalidDescriptor
            && layout_systemverilog_dpi_composite(unknown_two_state_enum).error
                == Error::InvalidDescriptor,
        "DPI composites reject enum, resource, descriptor, and direction errors");
}

void test_systemverilog_dpi_open_arrays()
{
    using namespace fsim::runtime;
    using Kind = SystemVerilogDpiCompositeKind;
    using Domain = SystemVerilogDpiScalarDomain;
    using Error = SystemVerilogDpiMarshallingError;
    using Mode = SystemVerilogDpiTransferMode;

    SystemVerilogDpiTypeDescriptor byte;
    byte.kind = Kind::Scalar;
    byte.scalar_domain = Domain::Bit2;
    byte.width = 8;
    std::vector<PackedLogic4> values;
    for (std::size_t value { }; value < 6; ++value) {
        values.push_back(PackedLogic4::from_aval_bval(8, value, 0));
    }
    SystemVerilogDpiOpenArrayRegistry registry;
    const auto handle = registry.create(
        { { 3, 2 }, { -1, 1 } }, byte, values, true, Mode::Inout);
    require(
        handle && registry.contains(handle.value)
            && registry.range(handle.value, 1)->left == 3
            && registry.range(handle.value, 1)->right == 2
            && registry.range(handle.value, 1)->increment() == -1
            && registry.range(handle.value, 2)->low() == -1
            && registry.range(handle.value, 2)->high() == 1
            && registry.range(handle.value, 2)->size() == 3,
        "DPI open-array ranges retain left/right/low/high/increment/size");
    require(
        registry.element(handle.value, { 3, -1 }).value.front().to_msb_string() == "00000000"
            && registry.element(handle.value, { 2, 1 }).value.front().to_msb_string() == "00000101"
            && registry.contiguous_values(handle.value).value == values,
        "DPI multidimensional declared indices map to canonical row-major elements");
    const auto array_pointer = registry.array_pointer(handle.value);
    const auto last_pointer = registry.element_pointer(handle.value, { 2, 1 });
    auto writable_pointer = registry.mutable_element_pointer(handle.value, { 2, 0 });
    require(
        registry.dimensions(handle.value) == 2 && array_pointer
            && array_pointer.count == 6
            && array_pointer.value[5].to_msb_string() == "00000101"
            && last_pointer && last_pointer.count == 1
            && last_pointer.value->to_msb_string() == "00000101"
            && writable_pointer && writable_pointer.count == 1,
        "DPI open-array accessors expose checked dimensions and transient pointers");
    *writable_pointer.value = PackedLogic4::from_msb_string("10101010");
    require(
        registry.element(handle.value, { 2, 0 }).value.front().to_msb_string()
            == "10101010",
        "DPI writable element pointers update registry-owned storage");
    require(
        registry.store_element(
            handle.value, { 3, 0 },
            { PackedLogic4::from_msb_string("11111111") })
                == Error::None
            && registry.element(handle.value, { 3, 0 }).value.front().to_msb_string() == "11111111"
            && registry.element(handle.value, { 4, 0 }).error
                == Error::IndexRange,
        "DPI open-array element writeback is checked and direction-aware");
    const auto scattered = registry.create(
        { { 0, 0 } }, byte, { PackedLogic4(8, Logic4::zero) }, false, Mode::Input);
    require(
        scattered
            && registry.contiguous_values(scattered.value).error
                == Error::Noncontiguous
            && registry.array_pointer(scattered.value).error
                == Error::Noncontiguous
            && registry.element_pointer(scattered.value, { 0 })
            && registry.mutable_element_pointer(scattered.value, { 0 }).error
                == Error::DirectionMismatch
            && registry.store_element(
                   scattered.value, { 0 },
                   { PackedLogic4(8, Logic4::one) })
                == Error::DirectionMismatch,
        "DPI open arrays reject noncontiguous access and input writeback");
    require(registry.release(handle.value), "DPI open-array release");
    require(
        !registry.contains(handle.value)
            && registry.element(handle.value, { 3, -1 }).error
                == Error::StaleArray,
        "DPI open-array epochs reject stale handles after release");
    require(
        registry.array_pointer(handle.value).error == Error::StaleArray
            && registry.element_pointer(handle.value, { 3, -1 }).error
                == Error::StaleArray,
        "DPI open-array accessors reject pointers after handle release");
}

void test_systemverilog_dpi_scopes()
{
    using namespace fsim::runtime;
    using Error = SystemVerilogDpiScopeError;

    SystemVerilogDpiScopeRegistry registry { 17 };
    const auto top = registry.define("top");
    const auto child = registry.define("top.u_core", top.value);
    const auto leaf = registry.define("top.u_core.g_lane[3]", child.value);
    require(
        top && child && leaf && registry.find("top.u_core") == child.value
            && registry.name(leaf.value) == "top.u_core.g_lane[3]"
            && registry.parent(leaf.value) == child.value
            && registry.simulation_identity() == 17,
        "DPI scopes retain exact simulation-owned hierarchy identities");
    require(
        registry.define("top.u_core", top.value).error == Error::DuplicateName
            && registry.define("top..bad").error == Error::InvalidName
            && registry.define("other.child", top.value).error
                == Error::InvalidParent,
        "DPI scope registration rejects duplicate, malformed, and wrong parents");

    SystemVerilogDpiScopeContext first { registry };
    SystemVerilogDpiScopeContext second { registry };
    const auto initial = first.set(top.value);
    const auto prior = first.set(leaf.value);
    require(
        initial && !initial.previous && prior && prior.previous == top.value
            && first.current() == leaf.value && !second.current()
            && first.set(prior.previous) && first.current() == top.value,
        "DPI current scope is execution-context local and exactly restorable");

    SystemVerilogDpiScopeRegistry other { 18 };
    const auto other_top = other.define("top");
    const auto before = first.current();
    require(
        other_top && first.set(other_top.value).error == Error::StaleScope
            && first.current() == before,
        "DPI scope setting rejects cross-simulation handles transactionally");
}

void test_systemverilog_dpi_callbacks()
{
    using namespace fsim::runtime;
    using Error = SystemVerilogDpiCallbackError;
    using Mode = SystemVerilogDpiTransferMode;

    SystemVerilogDpiScopeRegistry scopes { 23 };
    const auto top = scopes.define("top");
    const auto child = scopes.define("top.u_dpi", top.value);
    SystemVerilogDpiCallbackRegistry callbacks { scopes };
    std::optional<SystemVerilogDpiScopeHandle> observed_scope;
    require(
        callbacks.register_callback(
            "exported_add", child.value, { Mode::Input, Mode::Output },
            [&](SystemVerilogDpiCallbackFrame& frame) {
                observed_scope = frame.current_scope();
                const auto input = frame.read(0);
                auto* output = frame.writable(1);
                if (input && output)
                    *output = *input;
            })
            == Error::None,
        "DPI exported callback registration");
    SystemVerilogDpiCallbackContext context { scopes };
    const auto original = context.current_scope();
    const auto result = callbacks.dispatch(
        "exported_add",
        { { PackedLogic4::from_msb_string("1010") },
            { PackedLogic4::from_msb_string("0000") } },
        context);
    require(
        result && observed_scope == child.value && context.current_scope() == original
            && result.values[1].front().to_msb_string() == "1010",
        "DPI callbacks install exact scope, publish outputs, and restore context");

    require(
        callbacks.register_callback(
            "bad_write", child.value, { Mode::Input },
            [](SystemVerilogDpiCallbackFrame& frame) {
                static_cast<void>(frame.writable(0));
            }) == Error::None
            && callbacks.dispatch(
                            "bad_write", { { PackedLogic4(1, Logic4::zero) } }, context)
                    .error
                == Error::DirectionMismatch,
        "DPI callback frames reject mutable input access transactionally");
    context.request_disable();
    require(
        callbacks.register_callback(
            "ignore_disable", child.value, { },
            [](SystemVerilogDpiCallbackFrame&) { })
                == Error::None
            && callbacks.dispatch("ignore_disable", { }, context).error
                == Error::Disabled
            && context.is_disabled_state(),
        "DPI unacknowledged disabled state rejects callback publication");
    require(
        callbacks.register_callback(
            "ack_disable", child.value, { },
            [](SystemVerilogDpiCallbackFrame& frame) {
                if (frame.is_disabled_state()) {
                    static_cast<void>(frame.acknowledge_disabled_state());
                }
            }) == Error::None
            && callbacks.dispatch("ack_disable", { }, context)
            && !context.is_disabled_state(),
        "DPI disabled-state helpers require and retain explicit acknowledgement");

    require(
        callbacks.register_callback(
            "throws", child.value, { Mode::Output },
            [](SystemVerilogDpiCallbackFrame& frame) {
                auto* output = frame.writable(0);
                if (output)
                    output->front() = PackedLogic4(1, Logic4::one);
                throw std::runtime_error { "callback failed" };
            })
            == Error::None,
        "DPI throwing callback registration");
    const auto failure = callbacks.dispatch(
        "throws", { { PackedLogic4(1, Logic4::zero) } }, context);
    require(
        failure.error == Error::Exception && failure.values.empty()
            && failure.message == "callback failed"
            && context.current_scope() == original,
        "DPI callback exceptions reject output and restore scope");
    require(
        callbacks.dispatch("missing", { }, context).error == Error::UnknownName
            && callbacks.dispatch("exported_add", { }, context).error
                == Error::ArityMismatch
            && callbacks.register_callback(
                   "exported_add", child.value, { },
                   [](SystemVerilogDpiCallbackFrame&) { })
                == Error::DuplicateName,
        "DPI callbacks reject unknown names, arity mismatch, and duplicates");
}

void test_systemverilog_dpi_tasks()
{
    using namespace fsim::runtime;
    using Error = SystemVerilogDpiTaskError;
    using Mode = SystemVerilogDpiTransferMode;
    using Status = SystemVerilogDpiTaskStatus;

    Scheduler scheduler;
    SystemVerilogDpiScopeRegistry scopes { 29 };
    const auto top = scopes.define("top");
    const auto task_scope = scopes.define("top.u_task", top.value);
    const auto callback_scope = scopes.define("top.u_callback", top.value);
    SystemVerilogDpiCallbackContext context { scopes };
    SystemVerilogDpiCallbackRegistry callbacks { scopes };
    std::optional<SystemVerilogDpiScopeHandle> nested_scope;
    require(
        callbacks.register_callback(
            "nested", callback_scope.value, { },
            [&](SystemVerilogDpiCallbackFrame& frame) {
                nested_scope = frame.current_scope();
            })
            == SystemVerilogDpiCallbackError::None,
        "DPI nested callback registration");

    SystemVerilogDpiImportedTaskRegistry tasks { scheduler, scopes };
    bool task_scope_restored { };
    require(
        tasks.register_task(
            "wait_and_call", task_scope.value, { Mode::Input, Mode::Output },
            [&](SystemVerilogDpiCallbackFrame& frame, const std::size_t resume) {
                if (resume == 0) {
                    const auto* input = frame.read(0);
                    auto* output = frame.writable(1);
                    if (input && output)
                        *output = *input;
                    return SystemVerilogDpiTaskAction {
                        SystemVerilogDpiTaskActionKind::Suspend, 5
                    };
                }
                const auto nested = callbacks.dispatch("nested", { }, context);
                task_scope_restored = nested && frame.current_scope() == task_scope.value;
                auto* output = frame.writable(1);
                if (output)
                    output->front() = PackedLogic4::from_msb_string("1111");
                return SystemVerilogDpiTaskAction { };
            })
            == Error::None,
        "DPI suspending imported task registration");
    const auto started = tasks.start(
        "wait_and_call",
        { { PackedLogic4::from_msb_string("0011") },
            { PackedLogic4::from_msb_string("0000") } },
        context);
    require(static_cast<bool>(started), "DPI imported task start");
    require(
        scheduler.run(0).status == RunStatus::time_limit
            && tasks.result(started.value)->status == Status::Suspended
            && tasks.result(started.value)->values.empty(),
        "DPI imported task suspension retains unpublished transactional state");
    require(
        scheduler.run().status == RunStatus::completed
            && tasks.result(started.value)->status == Status::Completed
            && tasks.result(started.value)->values[1].front().to_msb_string()
                == "1111"
            && nested_scope == callback_scope.value && task_scope_restored
            && !context.current_scope(),
        "DPI imported task resumes, re-enters callbacks, and restores scope");

    require(
        tasks.register_task(
            "cancel_me", task_scope.value, { },
            [](SystemVerilogDpiCallbackFrame&, std::size_t) {
                return SystemVerilogDpiTaskAction {
                    SystemVerilogDpiTaskActionKind::Suspend, 10
                };
            })
            == Error::None,
        "DPI cancellable imported task registration");
    const auto cancelled = tasks.start("cancel_me", { }, context);
    static_cast<void>(scheduler.run(scheduler.now()));
    require(
        cancelled && tasks.result(cancelled.value)->status == Status::Suspended
            && tasks.cancel(cancelled.value)
            && tasks.result(cancelled.value)->status == Status::Cancelled
            && scheduler.run().status == RunStatus::completed,
        "DPI imported task cancellation removes the pending scheduler resume");

    require(
        tasks.register_task(
            "throws", task_scope.value, { },
            [](SystemVerilogDpiCallbackFrame&, std::size_t)
                -> SystemVerilogDpiTaskAction {
                throw std::runtime_error { "task failed" };
            }) == Error::None
            && tasks.register_task(
                   "survives", task_scope.value, { },
                   [](SystemVerilogDpiCallbackFrame&, std::size_t) {
                       return SystemVerilogDpiTaskAction { };
                   })
                == Error::None,
        "DPI failure-containment task registration");
    const auto failed = tasks.start("throws", { }, context);
    const auto survives = tasks.start("survives", { }, context);
    require(
        scheduler.run().status == RunStatus::completed && failed && survives
            && tasks.result(failed.value)->status == Status::Failed
            && tasks.result(failed.value)->error == Error::Exception
            && tasks.result(failed.value)->message == "task failed"
            && tasks.result(survives.value)->status == Status::Completed
            && !scheduler.running(),
        "DPI task exceptions are contained without blocking later scheduler work");
    require(
        tasks.start("missing", { }, context).error == Error::UnknownName
            && tasks.start("wait_and_call", { }, context).error
                == Error::ArityMismatch
            && !tasks.result(SystemVerilogDpiTaskHandle { 30, 0, 1 }),
        "DPI imported tasks reject unknown, malformed, and cross-simulation use");
}

void test_systemverilog_dpi_plugin_planning()
{
    using namespace fsim::runtime;
    using Error = SystemVerilogDpiPluginError;
    using Platform = SystemVerilogDpiPluginPlatform;

    SystemVerilogDpiPluginManifest manifest;
    manifest.name = "arith_dpi";
    manifest.sources = { "src/arith.cpp", "src/helpers.c" };
    manifest.include_directories = { "include" };
    manifest.libraries = { "m" };
    manifest.imported_symbols = { "dpi_add", "dpi_c_mix" };
    manifest.exported_symbols = { "sv_report" };
    const auto posix = plan_systemverilog_dpi_plugin(
        manifest, "/project", "/build", { "/plugins", "/cache" },
        "clang++", Platform::Posix);
    const auto repeated = plan_systemverilog_dpi_plugin(
        manifest, "/project", "/build", { "/plugins", "/cache" },
        "clang++", Platform::Posix);
    require(
        posix && repeated
            && posix.value.artifact == "/build/arith_dpi/plugin.so"
            && posix.value.discovery_candidates
                == repeated.value.discovery_candidates
            && posix.value.compile_commands.size() == 2
            && posix.value.compile_commands.front().arguments.front()
                == "clang++"
            && posix.value.compile_commands.front().arguments[5]
                == "/project/include"
            && posix.value.compile_commands.front().arguments[7]
                == "/project/src/arith.cpp"
            && posix.value.compile_commands.front().arguments[9]
                == "/build/arith_dpi/0-arith.cpp.o"
            && posix.value.link_command.arguments[2]
                == "/build/arith_dpi/0-arith.cpp.o"
            && posix.value.link_command.arguments[3]
                == "/build/arith_dpi/1-helpers.c.o"
            && posix.value.link_command.arguments.back()
                == "/build/arith_dpi/plugin.so",
        "DPI POSIX discovery, compile, and link planning is deterministic");
    const auto msvc = plan_systemverilog_dpi_plugin(
        manifest, "C:/project", "C:/build", { "C:/plugins" },
        "cl.exe", Platform::Msvc);
    require(
        msvc && msvc.value.artifact.filename() == "plugin.dll"
            && msvc.value.compile_commands.front().arguments[1] == "/nologo"
            && msvc.value.compile_commands.front().arguments[4] == "/Gd"
            && msvc.value.link_command.arguments[1] == "/LD",
        "DPI MSVC planning uses argv-safe compiler and linker contracts");

    auto invalid = manifest;
    invalid.sources = { "../escape.cpp" };
    require(
        plan_systemverilog_dpi_plugin(
            invalid, "/project", "/build", { }, "c++", Platform::Posix)
                .error
            == Error::InvalidSource,
        "DPI plug-in manifests reject producer-relative source escapes");
    invalid = manifest;
    invalid.exported_symbols = { "dpi_add" };
    require(
        plan_systemverilog_dpi_plugin(
            invalid, "/project", "/build", { }, "c++", Platform::Posix)
                .error
            == Error::DuplicateEntry,
        "DPI plug-in manifests reject cross-profile duplicate symbols");
    invalid = manifest;
    invalid.version = 2;
    require(
        plan_systemverilog_dpi_plugin(
            invalid, "/project", "/build", { }, "c++", Platform::Posix)
                .error
            == Error::ManifestVersion,
        "DPI plug-in manifests reject unsupported versions");

    auto loaded = load_systemverilog_dpi_plugin(
        FSIM_DPI_TEST_PLUGIN_PATH, manifest);
    require(
        loaded && loaded.value->imported_symbol("dpi_add")
            && loaded.value->exported_symbol("sv_report")
            && !loaded.value->imported_symbol("sv_report")
            && loaded.value->path().filename()
                == std::filesystem::path { FSIM_DPI_TEST_PLUGIN_PATH }.filename(),
        "DPI plug-in artifacts resolve complete exact symbol inventories");
    auto add_symbol = loaded.value->imported_symbol("dpi_add");
    auto c_symbol = loaded.value->imported_symbol("dpi_c_mix");
    using AddFunction = int(FSIM_DPI_PLUGIN_CALL*)(int, int);
    const auto add = reinterpret_cast<AddFunction>(add_symbol->address());
    const auto c_mix = reinterpret_cast<AddFunction>(c_symbol->address());
    loaded.value.reset();
    require(
        add_symbol && c_symbol && systemverilog_dpi_plugin_artifact_loaded(FSIM_DPI_TEST_PLUGIN_PATH) && add(7, 5) == 12
            && c_mix(7, 5) == 26,
        "DPI C and C++ symbol leases retain one module and calling convention");
    add_symbol.reset();
    c_symbol.reset();
    require(
        !systemverilog_dpi_plugin_artifact_loaded(FSIM_DPI_TEST_PLUGIN_PATH),
        "DPI modules unload normally after the final symbol lease");
    const fsim_dpi_plugin_descriptor_v1 valid_descriptor {
        FSIM_DPI_PLUGIN_ABI_VERSION,
        sizeof(fsim_dpi_plugin_descriptor_v1),
        sizeof(void*) * 8U,
        0,
        static_cast<std::uint32_t>(manifest.name.size()),
        manifest.name.data()
    };
    require(
        fsim_dpi_abi_c_descriptor_size()
                == sizeof(fsim_dpi_plugin_descriptor_v1)
            && std::string_view { fsim_dpi_abi_c_descriptor_symbol() }
                == FSIM_DPI_PLUGIN_DESCRIPTOR_SYMBOL,
        "DPI C and C++ translation units freeze one descriptor layout and symbol");
    auto invalid_descriptor = valid_descriptor;
    invalid_descriptor.abi_version = FSIM_DPI_PLUGIN_ABI_VERSION + 1U;
    require(
        validate_systemverilog_dpi_plugin_descriptor(
            valid_descriptor, manifest)
                == Error::None
            && validate_systemverilog_dpi_plugin_descriptor(
                   invalid_descriptor, manifest)
                == Error::AbiVersion,
        "DPI plug-in ABI descriptors validate version and complete fixed prefix");
    invalid_descriptor = valid_descriptor;
    invalid_descriptor.pointer_bits /= 2U;
    require(
        validate_systemverilog_dpi_plugin_descriptor(
            invalid_descriptor, manifest)
            == Error::AbiPointerWidth,
        "DPI plug-in ABI descriptors reject pointer-width mismatch");
    invalid_descriptor = valid_descriptor;
    --invalid_descriptor.struct_size;
    require(
        validate_systemverilog_dpi_plugin_descriptor(
            invalid_descriptor, manifest)
            == Error::AbiSize,
        "DPI plug-in ABI descriptors reject a one-byte truncated prefix");
    invalid_descriptor = valid_descriptor;
    invalid_descriptor.struct_size += 64U;
    require(
        validate_systemverilog_dpi_plugin_descriptor(
            invalid_descriptor, manifest)
            == Error::None,
        "DPI plug-in ABI descriptors accept future append-only extents");
    invalid_descriptor = valid_descriptor;
    invalid_descriptor.flags = 1U;
    require(
        validate_systemverilog_dpi_plugin_descriptor(
            invalid_descriptor, manifest)
            == Error::AbiFlags,
        "DPI plug-in ABI descriptors reject reserved flags");
    invalid_descriptor = valid_descriptor;
    --invalid_descriptor.name_size;
    require(
        validate_systemverilog_dpi_plugin_descriptor(
            invalid_descriptor, manifest)
            == Error::AbiName,
        "DPI plug-in ABI descriptors reject a mismatched bounded owner name");
    auto missing = manifest;
    missing.imported_symbols.push_back("missing_symbol");
    const auto rejected = load_systemverilog_dpi_plugin(
        FSIM_DPI_TEST_PLUGIN_PATH, missing);
    require(
        rejected.error == Error::MissingSymbol && !rejected.value
            && rejected.message.find("missing_symbol") != std::string::npos,
        "DPI plug-in symbol resolution is transactional on missing exports");
    const auto incompatible = load_systemverilog_dpi_plugin(
        FSIM_DPI_BAD_ABI_PLUGIN_PATH, manifest);
    require(
        incompatible.error == Error::AbiVersion && !incompatible.value,
        "DPI loader rejects an actual incompatible-ABI shared library");
    const auto provenance = provenance_systemverilog_dpi_plugin(
        manifest, FSIM_DPI_TEST_PLUGIN_PATH, "clang-22", Platform::Posix);
    const auto repeated_provenance = provenance_systemverilog_dpi_plugin(
        manifest, FSIM_DPI_TEST_PLUGIN_PATH, "clang-22", Platform::Posix);
    const auto other_toolchain = provenance_systemverilog_dpi_plugin(
        manifest, FSIM_DPI_TEST_PLUGIN_PATH, "clang-23", Platform::Posix);
    require(
        provenance && repeated_provenance && other_toolchain
            && provenance.value.manifest_digest.size() == 64
            && provenance.value.artifact_digest.size() == 64
            && provenance.value.cache_key
                == repeated_provenance.value.cache_key
            && provenance.value.artifact_digest
                == other_toolchain.value.artifact_digest
            && provenance.value.cache_key != other_toolchain.value.cache_key,
        "DPI plug-in provenance is content-addressed and toolchain-complete");
    auto quarantined = load_systemverilog_dpi_plugin(
        FSIM_DPI_TEST_PLUGIN_PATH, manifest);
    require(
        quarantined && quarantined.value->quarantine(),
        "DPI plug-in failure quarantine accepts a loaded module");
    quarantined.value.reset();
    require(
        systemverilog_dpi_plugin_artifact_loaded(FSIM_DPI_TEST_PLUGIN_PATH),
        "DPI quarantine retains modules with potentially escaped addresses");
}

void test_systemverilog_dpi_engine_differential()
{
    using namespace fsim::runtime;
    using Mode = SystemVerilogDpiTransferMode;
    using Status = SystemVerilogDpiTaskStatus;

    SystemVerilogDpiPluginManifest manifest;
    manifest.name = "arith_dpi";
    manifest.sources = { "arith.cpp" };
    manifest.imported_symbols = { "dpi_add", "dpi_c_mix" };
    manifest.exported_symbols = { "sv_report" };
    auto loaded = load_systemverilog_dpi_plugin(
        FSIM_DPI_TEST_PLUGIN_PATH, manifest);
    require(static_cast<bool>(loaded), "DPI differential fixture load");
    auto symbol = loaded.value->imported_symbol("dpi_add");
    using AddFunction = int(FSIM_DPI_PLUGIN_CALL*)(int, int);
    const auto add = reinterpret_cast<AddFunction>(symbol->address());

    enum class Engine { Interpreter,
        CompiledO0,
        CompiledO2 };
    const auto left = PackedLogic4::from_aval_bval(32, 19, 0);
    const auto right = PackedLogic4::from_aval_bval(32, 23, 0);
    std::vector<PackedLogic4> results;
    for (const auto engine : {
             Engine::Interpreter, Engine::CompiledO0, Engine::CompiledO2 }) {
        auto lhs = left;
        auto rhs = right;
        if (engine == Engine::Interpreter) {
            const auto lhs_payload = marshal_systemverilog_dpi_scalar(
                lhs, SystemVerilogDpiScalarDomain::Bit2);
            const auto rhs_payload = marshal_systemverilog_dpi_scalar(
                rhs, SystemVerilogDpiScalarDomain::Bit2);
            lhs = unmarshal_systemverilog_dpi_scalar(lhs_payload.value, 32).value;
            rhs = unmarshal_systemverilog_dpi_scalar(rhs_payload.value, 32).value;
        }
        const auto value = add(
            static_cast<int>(lhs.low_word().aval),
            static_cast<int>(rhs.low_word().aval));
        results.push_back(PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(value), 0));
    }
    require(
        results[0] == results[1] && results[1] == results[2]
            && results[0].low_word().aval == 42,
        "DPI interpreter and compiled O0/O2 boundaries agree exactly");

    SystemVerilogDpiScopeRegistry first_scopes { 101 };
    SystemVerilogDpiScopeRegistry second_scopes { 102 };
    const auto first_top = first_scopes.define("top_a");
    const auto second_top = second_scopes.define("top_b");
    SystemVerilogDpiCallbackContext first_context { first_scopes };
    SystemVerilogDpiCallbackContext second_context { second_scopes };
    SystemVerilogDpiCallbackRegistry first_callbacks { first_scopes };
    SystemVerilogDpiCallbackRegistry second_callbacks { second_scopes };
    const auto register_root = [&](auto& callbacks, const auto scope) {
        return callbacks.register_callback(
            "invoke_add", scope, { Mode::Input, Mode::Input, Mode::Output },
            [add](SystemVerilogDpiCallbackFrame& frame) {
                const auto* lhs = frame.read(0);
                const auto* rhs = frame.read(1);
                auto* output = frame.writable(2);
                if (lhs && rhs && output) {
                    const auto value = add(
                        static_cast<int>(lhs->front().low_word().aval),
                        static_cast<int>(rhs->front().low_word().aval));
                    output->front() = PackedLogic4::from_aval_bval(
                        32, static_cast<std::uint32_t>(value), 0);
                }
            });
    };
    require(
        register_root(first_callbacks, first_top.value)
                == SystemVerilogDpiCallbackError::None
            && register_root(second_callbacks, second_top.value)
                == SystemVerilogDpiCallbackError::None,
        "DPI multi-root callback registration");
    const std::vector<std::vector<PackedLogic4>> arguments {
        { left }, { right }, { PackedLogic4::from_aval_bval(32, 0, 0) }
    };
    const auto first = first_callbacks.dispatch(
        "invoke_add", arguments, first_context);
    const auto second = second_callbacks.dispatch(
        "invoke_add", arguments, second_context);
    require(
        first && second && first.values == second.values
            && first.values[2].front().low_word().aval == 42
            && first_top.value.simulation != second_top.value.simulation
            && !first_context.current_scope() && !second_context.current_scope(),
        "DPI multi-root and multi-context callbacks remain isolated");

    Scheduler scheduler;
    SystemVerilogDpiImportedTaskRegistry tasks { scheduler, first_scopes };
    require(
        tasks.register_task(
            "suspended_add", first_top.value,
            { Mode::Input, Mode::Input, Mode::Output },
            [add](SystemVerilogDpiCallbackFrame& frame,
                const std::size_t resume) {
                if (resume == 0) {
                    return SystemVerilogDpiTaskAction {
                        SystemVerilogDpiTaskActionKind::Suspend, 3
                    };
                }
                const auto* lhs = frame.read(0);
                const auto* rhs = frame.read(1);
                auto* output = frame.writable(2);
                if (lhs && rhs && output) {
                    const auto value = add(
                        static_cast<int>(lhs->front().low_word().aval),
                        static_cast<int>(rhs->front().low_word().aval));
                    output->front() = PackedLogic4::from_aval_bval(
                        32, static_cast<std::uint32_t>(value), 0);
                }
                return SystemVerilogDpiTaskAction { };
            })
            == SystemVerilogDpiTaskError::None,
        "DPI real-symbol suspended task registration");
    const auto task = tasks.start("suspended_add", arguments, first_context);
    static_cast<void>(scheduler.run(0));
    require(
        task && tasks.result(task.value)->status == Status::Suspended
            && scheduler.run().status == RunStatus::completed
            && tasks.result(task.value)->status == Status::Completed
            && tasks.result(task.value)->values[2].front().low_word().aval == 42
            && !first_context.current_scope(),
        "DPI real-symbol suspended task resumes and restores its root context");
}

} // namespace fsim::tests::runtime
