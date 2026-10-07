// SPDX-License-Identifier: Apache-2.0
#include "runtime_direct_artifact_rows_test_support.hpp"

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

namespace {

using namespace fsim::app::direct_artifact_rows_test;

void test_schema_and_direct_row_roundtrip()
{
    assert(fsim::app::kRuntimeStateSchema == 76U);
    const auto source_bytes = encode_source_design();
    assert(source_bytes.has_value());

    auto restored = decode_design(*source_bytes);
    assert_view_rows(*restored);
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
    fsim::diagnostic::Engine repeated_diagnostics;
    const auto third_bytes = fsim::app::serialize_runtime_state(*repeated, repeated_diagnostics);
    assert(third_bytes && *third_bytes == *source_bytes);

    for (const std::uint32_t unsupported_schema : {
             70U, 71U, 72U, 73U, 75U, 77U }) {
        auto invalid_bytes = *source_bytes;
        assert(invalid_bytes.size() > 8U);
        invalid_bytes[8U] = static_cast<char>(unsupported_schema);
        fsim::diagnostic::Engine invalid_diagnostics;
        const auto invalid = fsim::app::deserialize_runtime_state(
            invalid_bytes, "unsupported-runtime-schema", invalid_diagnostics);
        assert(!invalid.has_value());
        assert(invalid_diagnostics.has_error());
    }
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
    test_schema_and_direct_row_roundtrip();
    test_decoder_rejects_noncanonical_process_rows();
    test_lazy_materialization_copy_move_and_state();
    test_restored_interpreter_keeps_public_facades_lazy();
}
