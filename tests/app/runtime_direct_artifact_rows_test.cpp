// SPDX-License-Identifier: Apache-2.0
#include "runtime_direct_artifact_rows_test_support.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace {

using namespace fsim::app::direct_artifact_rows_test;

void test_string_method_archive_enum_range()
{
    using Operator = fsim::runtime::simir::StringMethodOperator;

    fsim::app::codec_detail::Writer valid_writer;
    valid_writer.write(static_cast<std::uint8_t>(
        Operator::format_string_dynamic_field));
    const auto valid_bytes = std::move(valid_writer).finish();
    fsim::app::codec_detail::Reader valid_reader { valid_bytes };
    Operator decoded { };
    assert(valid_reader.read(decoded));
    assert(decoded == Operator::format_string_dynamic_field);

    fsim::app::codec_detail::Writer invalid_writer;
    invalid_writer.write(std::uint8_t { 0xffU });
    const auto invalid_bytes = std::move(invalid_writer).finish();
    fsim::app::codec_detail::Reader invalid_reader { invalid_bytes };
    assert(!invalid_reader.read(decoded));
    assert(invalid_reader.failure().find("invalid scalar enumeration")
        != std::string::npos);
}

void test_process_startup_phase_archive_enum_range()
{
    using Phase = fsim::runtime::simir::ProcessStartupPhase;

    fsim::app::codec_detail::Writer valid_writer;
    valid_writer.write(static_cast<std::uint8_t>(
        Phase::package_static_initializer));
    const auto valid_bytes = std::move(valid_writer).finish();
    fsim::app::codec_detail::Reader valid_reader { valid_bytes };
    Phase decoded { };
    assert(valid_reader.read(decoded));
    assert(decoded == Phase::package_static_initializer);

    fsim::app::codec_detail::Writer invalid_writer;
    invalid_writer.write(std::uint8_t { 0xffU });
    const auto invalid_bytes = std::move(invalid_writer).finish();
    fsim::app::codec_detail::Reader invalid_reader { invalid_bytes };
    assert(!invalid_reader.read(decoded));
    assert(invalid_reader.failure().find("invalid scalar enumeration")
        != std::string::npos);
}

void test_tagged_fork_runtime_roundtrip()
{
    using namespace fsim::runtime::simir;

    auto state = make_startup_metadata_state();
    Process process;
    process.id = 3U;
    process.name = "top.tagged_fork";
    process.language_standard = "1800-2017";
    process.compatibility_profile = "none";
    process.operations.emplace_back(
        Fork { { 2U }, ForkJoinKind::none, true });
    process.operations.emplace_back(Halt { });
    process.operations.emplace_back(ForkEnd { });
    state.processes.push_back(std::move(process));

    auto design = ElaboratedDesign::from_state(std::move(state));
    assert(design);
    fsim::diagnostic::Engine encode_diagnostics;
    const auto bytes = fsim::app::serialize_runtime_state(
        *design, encode_diagnostics);
    assert(bytes && !encode_diagnostics.has_error());

    fsim::diagnostic::Engine decode_diagnostics;
    auto restored = fsim::app::deserialize_runtime_state(
        *bytes, "tagged-fork-runtime-roundtrip", decode_diagnostics);
    assert(restored && !decode_diagnostics.has_error());
    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;
    const auto view = ProcessAccess::process_view(*restored, 3U);
    const auto operation = view.operations().expanded(0U);
    const auto* fork = operation_get_if<Fork>(&operation);
    assert(fork && fork->isolate_register_frame
        && fork->join == ForkJoinKind::none
        && fork->branches == std::vector<InstructionIndex> { 2U });
}

void test_schema_and_direct_row_roundtrip()
{
    assert(fsim::app::kRuntimeStateSchema == 88U);
    const auto source_bytes = encode_startup_metadata_design();
    assert(source_bytes.has_value());
    const auto wire_payload = read_runtime_wire_payload(*source_bytes);
    assert(wire_payload.has_value());
    assert(wire_payload->process_rows->rows[0U]
        .instance.startup_package_owner_id == 8U);
    assert(wire_payload->state.string_objects[0U].package_owner_id == 8U);
    assert(wire_payload->state.container_objects[0U].package_owner_id == 8U);

    auto restored = decode_design(*source_bytes);
    assert_view_rows(*restored);
    assert_startup_metadata_view_rows(*restored);
    assert_package_startup_sidecar_rows(*restored);
    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;
    const auto independent_view = ProcessAccess::process_view(*restored, 2U);
    const auto archived_scan_operation
        = independent_view.operations().expanded(5U);
    const auto* archived_scan
        = fsim::runtime::simir::operation_get_if<fsim::runtime::simir::FileScan>(
            &archived_scan_operation);
    assert(archived_scan != nullptr
        && archived_scan->time_unit_femtoseconds == 1'000'000U
        && archived_scan->conversions.size() == 1U
        && archived_scan->conversions.front().format
            == fsim::runtime::simir::InputScanFormat::time);
    const auto original_table = fsim::elaboration::detail::ElaboratedDesignProcessAccess::process_table(
        *restored);

    fsim::diagnostic::Engine diagnostics;
    const auto repeated_bytes = fsim::app::serialize_runtime_state(*restored, diagnostics);
    assert(repeated_bytes.has_value() && !diagnostics.has_error());
    assert(*repeated_bytes == *source_bytes);
    assert(fsim::elaboration::detail::ElaboratedDesignProcessAccess::row_backed(
        *restored));
    assert(
        fsim::elaboration::detail::ElaboratedDesignProcessAccess::process_table(
            *restored)
        == original_table);

    auto repeated = decode_design(*repeated_bytes);
    assert_view_rows(*repeated);
    assert_startup_metadata_view_rows(*repeated);
    assert_package_startup_sidecar_rows(*repeated);
    fsim::diagnostic::Engine repeated_diagnostics;
    const auto third_bytes = fsim::app::serialize_runtime_state(*repeated, repeated_diagnostics);
    assert(third_bytes && *third_bytes == *source_bytes);

    auto facade_copy = *repeated;
    assert_startup_metadata_facade_rows(facade_copy.processes());

    for (const std::uint32_t unsupported_schema : {
             70U, 71U, 72U, 73U, 75U, 76U, 78U, 81U, 82U, 83U,
             fsim::app::kRuntimeStateSchema - 1U }) {
        auto invalid_bytes = *source_bytes;
        assert(invalid_bytes.size() > 8U);
        invalid_bytes[8U] = static_cast<char>(unsupported_schema);
        fsim::diagnostic::Engine invalid_diagnostics;
        const auto invalid = fsim::app::deserialize_runtime_state(
            invalid_bytes, "unsupported-runtime-schema", invalid_diagnostics);
        assert(!invalid.has_value());
        assert(invalid_diagnostics.has_error());
    }

    const auto reject_startup_metadata = [&](const std::string_view name,
                                             const std::string_view expected,
                                             const auto& mutate) {
        auto payload = read_runtime_wire_payload(*source_bytes);
        assert(payload.has_value());
        mutate(*payload);
        const auto malformed = write_runtime_wire_payload(*payload);
        assert(malformed.has_value());
        fsim::diagnostic::Engine malformed_diagnostics;
        const auto rejected = fsim::app::deserialize_runtime_state(
            *malformed, std::string { name }, malformed_diagnostics);
        assert(!rejected.has_value());
        assert(malformed_diagnostics.has_error());
        assert(std::ranges::any_of(
            malformed_diagnostics.diagnostics(), [&](const auto& item) {
                return item.message.find(expected) != std::string::npos;
            }));
    };
    reject_startup_metadata("duplicate-tagged-process-order",
        "runtime package startup order is duplicated", [](auto& payload) {
            auto& second = payload.process_rows->rows[1U].instance;
            second.startup_phase = ProcessStartupPhase::package_static_initializer;
            second.startup_order
                = payload.process_rows->rows.front().instance.startup_order;
        });
    reject_startup_metadata("duplicate-process-callback-order",
        "runtime package class-handle initializer metadata is invalid",
        [](auto& payload) {
            payload.state.systemverilog_package_class_initializers.front()
                .startup_order
                = payload.process_rows->rows.front().instance.startup_order;
        });
    reject_startup_metadata("duplicate-package-callback-order",
        "runtime package static-property initializer metadata is invalid",
        [](auto& payload) {
            payload.state.systemverilog_package_class_static_initializers
                .front().startup_order = payload.state
                    .systemverilog_package_class_initializers.front()
                    .startup_order;
        });
    reject_startup_metadata("package-callback-omits-target-store",
        "runtime package class-handle initializer metadata is invalid",
        [](auto& payload) {
            payload.state.systemverilog_package_class_initializers.front()
                .writable_signal_ids = { 0U, 4U };
        });
    reject_startup_metadata("package-callback-unsorted-store-set",
        "runtime package class-handle initializer metadata is invalid",
        [](auto& payload) {
            payload.state.systemverilog_package_class_initializers.front()
                .writable_signal_ids = { 1U, 0U, 4U };
        });
    reject_startup_metadata("package-process-duplicate-string-permission",
        "runtime package process object permissions are invalid",
        [](auto& payload) {
            payload.process_rows->rows[0U].instance
                .startup_writable_string_object_ids = { 0U, 0U };
        });
    reject_startup_metadata("package-process-out-of-range-container-permission",
        "runtime package process object permissions are invalid",
        [](auto& payload) {
            payload.process_rows->rows[0U].instance
                .startup_writable_container_object_ids = { 1U };
        });
    reject_startup_metadata("ordinary-process-carries-package-object-permission",
        "ordinary runtime process carries package object permissions",
        [](auto& payload) {
            payload.process_rows->rows[1U].instance
                .startup_writable_string_object_ids = { 0U };
        });
    reject_startup_metadata("ordinary-process-carries-package-owner",
        "ordinary runtime process carries package object permissions",
        [](auto& payload) {
            payload.process_rows->rows[1U].instance
                .startup_package_owner_id = 8U;
        });
    reject_startup_metadata("package-process-missing-owner",
        "runtime package process object permissions are invalid",
        [](auto& payload) {
            payload.process_rows->rows[0U].instance
                .startup_package_owner_id = 0U;
        });
    reject_startup_metadata("package-process-string-write-mismatch",
        "runtime package process object permissions do not match its writes",
        [](auto& payload) {
            payload.process_rows->rows[0U].instance
                .startup_writable_string_object_ids.clear();
        });
    reject_startup_metadata("package-process-foreign-string-owner",
        "runtime package process string permission has a foreign owner",
        [](auto& payload) {
            payload.state.string_objects[0U].package_owner_id = 9U;
        });
    reject_startup_metadata("package-process-foreign-container-owner",
        "runtime package process container permission is not direct owned storage",
        [](auto& payload) {
            payload.state.container_objects[0U].package_owner_id = 9U;
        });
    reject_startup_metadata("package-process-aliased-container-object",
        "runtime package process container permission is not direct owned storage",
        [](auto& payload) {
            const fsim::runtime::simir::ContainerSliceAlias alias {
                0U, 0, 0 };
            payload.state.container_object_info[0U].slice_alias = alias;
            payload.state.container_objects[0U].slice_alias = alias;
        });
}

void test_from_state_rejects_sensitive_package_startup_process()
{
    auto state = make_startup_metadata_state();
    state.processes[0U].static_sensitivity.push_back(
        { 0U, EdgeKind::any, 0U, 1U });
    const auto design = ElaboratedDesign::from_state(std::move(state));
    assert(!design.has_value());
}

void test_from_state_rejects_generic_domain_package_startup_process()
{
    auto state = make_startup_metadata_state();
    state.processes[0U].scheduling_domain
        = ProcessSchedulingDomain::generic;
    const auto design = ElaboratedDesign::from_state(std::move(state));
    assert(!design.has_value());
}

void assert_invalid_runtime_row_wire(
    const std::string_view bytes, const std::string_view source_name,
    const std::string_view expected_message)
{
    const auto parsed = read_runtime_wire_payload(bytes);
    assert(parsed.has_value());
    assert(parsed->schema == fsim::app::kRuntimeStateSchema);
    assert(parsed->process_rows != nullptr);
    assert(parsed->state.processes.empty());
    assert(parsed->path_mode == 0U);

    assert(decode_malformed_process_rows(
        bytes, source_name, expected_message));
    fsim::diagnostic::Engine diagnostics;
    const auto design = fsim::app::deserialize_runtime_state(
        bytes, std::string { source_name }, diagnostics);
    assert(!design.has_value());
    assert(diagnostics.has_error());
    assert(std::ranges::any_of(diagnostics.diagnostics(),
        [&](const auto& diagnostic) {
            return diagnostic.message.find(expected_message)
                != std::string::npos;
        }));
}

void test_decoder_rejects_noncanonical_process_rows()
{
    const auto valid_bytes = encode_source_design();
    assert(valid_bytes.has_value());
    const auto valid_payload = read_runtime_wire_payload(*valid_bytes);
    assert(valid_payload.has_value());
    assert(valid_payload->schema == fsim::app::kRuntimeStateSchema);
    assert(valid_payload->process_rows != nullptr);
    assert(valid_payload->state.processes.empty());
    assert(valid_payload->process_rows->rows.size() == 3U);
    assert(valid_payload->process_rows->rows[0U].template_id == 0U);
    assert(valid_payload->process_rows->rows[1U].template_id == 0U);
    assert(valid_payload->process_rows->rows[2U].template_id == 1U);
    const auto writer_roundtrip = write_runtime_wire_payload(*valid_payload);
    assert(writer_roundtrip && *writer_roundtrip == *valid_bytes);

    const auto check_mutation = [&](const std::string_view name,
                                    const std::string_view expected_message,
                                    const auto& mutate) {
        const auto malformed
            = encode_malformed_process_rows(*valid_bytes, mutate);
        assert(malformed.has_value());
        const auto malformed_payload = read_runtime_wire_payload(*malformed);
        assert(malformed_payload.has_value());
        assert(malformed_payload->path_mode == valid_payload->path_mode);
        assert(malformed_payload->path_binding
            == valid_payload->path_binding);
        assert(malformed_payload->path_ids == valid_payload->path_ids);
        assert_invalid_runtime_row_wire(
            *malformed, name, expected_message);
    };

    check_mutation("runtime-row-out-of-range-template",
        "runtime process row table is not canonical",
        [](auto& table) {
            table.rows[2U].template_id
                = static_cast<std::uint32_t>(table.templates.size() + 1U);
        });
    check_mutation("runtime-row-null-template",
        "runtime process row table is not canonical",
        [](auto& table) { table.templates[0U].reset(); });
    check_mutation("runtime-row-nondense-process-id",
        "runtime process row table is not canonical",
        [](auto& table) { table.rows[1U].instance.id = 7U; });
    check_mutation("runtime-row-unused-template-definition",
        "runtime process template table contains an unused entry",
        [](auto& table) { table.rows[2U].template_id = 0U; });
    check_mutation("runtime-row-out-of-order-template-definition",
        "runtime process row table is not canonical",
        [](auto& table) {
            table.rows[0U].template_id = 1U;
            table.rows[1U].template_id = 0U;
            table.rows[2U].template_id = 1U;
        });
}

void test_lazy_materialization_copy_move_and_state()
{
    const auto source_bytes = encode_source_design();
    assert(source_bytes.has_value());
    auto restored = decode_design(*source_bytes);
    assert_view_rows(*restored);

    auto copied = *restored;
    assert_view_rows(copied);
    const auto& copied_processes = copied.processes();
    assert_facade_rows(copied_processes);
    assert(!fsim::elaboration::detail::ElaboratedDesignProcessAccess::row_backed(
        copied));
    assert_view_rows(*restored);

    auto state_copy = *restored;
    const auto materialized_state = std::as_const(state_copy).state();
    assert_facade_rows(materialized_state.processes);
    assert(fsim::elaboration::detail::ElaboratedDesignProcessAccess::row_backed(
        state_copy));
    assert_view_rows(state_copy);
    assert_view_rows(*restored);

    auto moved = std::move(*restored);
    assert_view_rows(moved);
    fsim::diagnostic::Engine move_diagnostics;
    const auto moved_bytes = fsim::app::serialize_runtime_state(moved, move_diagnostics);
    assert(moved_bytes && *moved_bytes == *source_bytes);
    assert_view_rows(moved);

    auto moved_state = std::move(moved).state();
    assert_facade_rows(moved_state.processes);
}

void test_restored_interpreter_keeps_public_facades_lazy()
{
    const auto source_bytes = encode_source_design();
    assert(source_bytes.has_value());
    auto restored = decode_design(*source_bytes);
    assert_view_rows(*restored);

    auto interpreter = std::move(*restored).create_interpreter();
    assert(interpreter != nullptr);
    for (std::uint32_t process = 0U; process < 3U; ++process) {
        assert(!facade_materialized(*interpreter, process));
    }

    interpreter->start();
    const auto result = interpreter->run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    const fsim::runtime::PackedLogic4 expected_one { 1U,
        fsim::runtime::Logic4::one };
    const fsim::runtime::PackedLogic4 expected_zero { 1U,
        fsim::runtime::Logic4::zero };
    assert(interpreter->signal_value_snapshot(2U) == expected_one);
    assert(interpreter->signal_value_snapshot(3U) == expected_zero);
    assert(interpreter->signal_value_snapshot(4U) == expected_one);
    assert(interpreter->read_debug_local(0U, 0U) == expected_one);
    assert(interpreter->read_debug_local(1U, 0U) == expected_zero);
    assert(interpreter->read_debug_local(2U, 0U) == expected_one);
    for (std::uint32_t process = 0U; process < 3U; ++process) {
        assert(!facade_materialized(*interpreter, process));
    }
}

} // namespace

int main()
{
    test_string_method_archive_enum_range();
    test_process_startup_phase_archive_enum_range();
    test_tagged_fork_runtime_roundtrip();
    test_schema_and_direct_row_roundtrip();
    test_from_state_rejects_sensitive_package_startup_process();
    test_from_state_rejects_generic_domain_package_startup_process();
    test_decoder_rejects_noncanonical_process_rows();
    test_lazy_materialization_copy_move_and_state();
    test_restored_interpreter_keeps_public_facades_lazy();
}
