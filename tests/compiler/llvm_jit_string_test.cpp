// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_strings_at_level(
    const JitOptimizationLevel optimization,
    const std::string_view symbol)
{
    Process process {
        .id = 0,
        .name = "strings",
        .register_count = 4,
        .string_register_count = 4,
        .container_register_count = 0,
        .debug_locals = { },
        .debug_string_locals = { },
        .debug_container_locals = { },
        .container_register_types = { },
        .static_sensitivity = { },
        .static_trigger_regions = { },
        .operations = {
            LoadStringConstant { 0, "A\xcf\x80" },
            LoadStringConstant { 1, "\xf0\x9f\x98\x80" },
            ConcatenateStrings { 2, { 0, 1 } },
            LoadConstant {
                0, PackedLogic4::from_aval_bval(32, 1, 0) },
            LoadConstant {
                1, PackedLogic4::from_aval_bval(32, 0x1f642, 0) },
            StringReplaceByte { 2, 0, 1, true },
            StringLength { 2, 2 },
            StringIndex { 0, 2, 0, true },
            LoadStringConstant {
                3, "AB\x80\xf0\x9f\x98\x80" },
            CompareStrings { 3, 2, 3, false },
            WriteStringObject { 0, 2 },
            ReadStringObject { 0, 0 },
            StringDisplay { 0, "[", "]", true, false },
            Halt { },
        },
        .driver_regions = { },
        .drive_strength = { },
        .switch_source = std::nullopt,
        .switch_target = std::nullopt,
        .switch_control = std::nullopt,
        .switch_active_high = true,
        .switch_bidirectional = false,
        .switch_resistive = false,
        .register_value_kinds = { },
        .initialize = true,
        .program_owner = std::nullopt,
        .final = false,
        .expression_profiles = { }
    };

    LlvmJitOptions options;
    options.optimization = optimization;
    LlvmJit jit(options);
    assert(jit.supports_process(process, { }));
    jit.add_process(symbol, process, { });
    const auto handle = jit.lookup(symbol);
    const auto layout = jit.frame_layout(handle);
    assert(layout.register_count == 4);
    assert(layout.string_register_count == 4);

    TestRuntime state;
    auto runtime = abi(state);
    assert(jit.execute(handle, runtime) == JitExecutionStatus::completed);
    assert(
        state.string_objects[0]
        == "AB\x80\xf0\x9f\x98\x80");
    assert(state.strings[0] == state.string_objects[0]);
    assert(
        state.output
        == std::vector<std::string> {
            "[AB\x80\xf0\x9f\x98\x80]" });

    Process arbitrary_bytes;
    arbitrary_bytes.id = 1;
    arbitrary_bytes.name = "arbitrary_bytes";
    arbitrary_bytes.string_register_count = 1;
    arbitrary_bytes.operations = {
        LoadStringConstant { 0, "\xc0\x80" }, Halt { }
    };
    const auto byte_symbol = std::string { symbol } + "_bytes";
    jit.add_process(byte_symbol, arbitrary_bytes, { });
    TestRuntime byte_state;
    auto byte_runtime = abi(byte_state);
    assert(
        jit.execute(jit.lookup(byte_symbol), byte_runtime)
            == JitExecutionStatus::completed
        && byte_state.strings[0] == "\xc0\x80");

    Process malformed_file;
    malformed_file.id = 2;
    malformed_file.name = "malformed_file_text_target";
    malformed_file.register_count = 2;
    FileReadLine read_line {
        1, 0, 2, 0, FileReadKind::line
    };
    read_line.target_kind = FileTextTargetKind::packed_register;
    read_line.target_width = 137;
    malformed_file.operations = {
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 0, 0) },
        read_line,
        Halt { }
    };
    try {
        jit.add_process(
            std::string { symbol } + "_malformed_file",
            malformed_file, { });
        assert(false && "malformed packed file-text target was accepted");
    } catch (const LlvmJitError& error) {
        assert(std::string_view { error.what() }.find("out of range")
            != std::string_view::npos);
    }
}

} // namespace fsim::tests::compiler
