// SPDX-License-Identifier: Apache-2.0
void test_simir_mutable_strings()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    Interpreter interpreter;
    const auto object = interpreter.add_string_object(
        StringObject { "top.title", { } });

    Process process;
    process.id = 0;
    process.name = "mutable_strings";
    process.register_count = 5;
    process.string_register_count = 4;
    process.debug_locals = {
        DebugLocal {
            "equal", "logic", 0, 1, { }, { }, { },
            ValueKind::logic4, { } },
        DebugLocal {
            "length", "int", 1, 32, { }, { }, { },
            ValueKind::logic4, { } },
        DebugLocal {
            "first", "int", 3, 32, { }, { }, { },
            ValueKind::logic4, { } },
    };
    process.debug_string_locals = {
        DebugStringLocal { "copy", 3, { } },
    };
    process.operations = {
        LoadStringConstant { 0, "f\xcf\x80" },
        LoadStringConstant { 1, "\xf0\x9f\x98\x80" },
        ConcatenateStrings { 2, { 0, 1 } },
        WriteStringObject { object, 2 },
        ReadStringObject { 3, object },
        CompareStrings { 0, 2, 3, false },
        StringLength { 1, 3 },
        LoadConstant {
            2, PackedLogic4::from_aval_bval(32, 1, 0) },
        StringIndex { 3, 3, 2, true },
        LoadConstant {
            4, PackedLogic4::from_aval_bval(32, 0x1f642, 0) },
        StringReplaceByte { 3, 2, 4, true },
        WriteStringObject { object, 3 },
        Halt { },
    };
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && interpreter.string_object_value(object)
                == "fB\x80\xf0\x9f\x98\x80",
        "mutable string object read, value-copy, concatenation, index, and "
        "replacement");
    require(
        interpreter.read_debug_local(0, 0).to_msb_string() == "1"
            && interpreter.read_debug_local(0, 1).low_word().aval == 7
            && interpreter.read_debug_local(0, 2).low_word().aval == 0xcf
            && interpreter.read_debug_string_local(0, 0)
                == "fB\x80\xf0\x9f\x98\x80",
        "mutable string comparison, length, indexing, and debugger values");

    try {
        Interpreter invalid;
        (void)invalid.add_string_object(
            StringObject {
                "oversize",
                std::string(maximum_string_bytes + 1, 'x') });
        throw std::runtime_error { "oversize string object was accepted" };
    } catch (const std::length_error& error) {
        require(
            std::string_view { error.what() }.find("byte limit")
                != std::string_view::npos,
            "oversize string object diagnostic");
    }

    Interpreter invalid_index;
    Process bad;
    bad.id = 0;
    bad.name = "invalid_string_index";
    bad.register_count = 2;
    bad.string_register_count = 1;
    bad.operations = {
        LoadStringConstant { 0, "x" },
        LoadConstant {
            0, PackedLogic4::from_aval_bval(32, 1, 0) },
        StringIndex { 1, 0, 0, true },
        Halt { },
    };
    (void)invalid_index.add_process(std::move(bad));
    try {
        (void)invalid_index.run();
        throw std::runtime_error { "out-of-range string index was accepted" };
    } catch (const InterpreterError& error) {
        require(
            std::string_view { error.what() }.find("outside the byte range")
                != std::string_view::npos,
            "out-of-range string index diagnostic");
    }
}

void test_simir_fork_process_lifecycle()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    const auto run_fork = [](
                              std::vector<Operation> operations,
                              const std::string_view expected,
                              const SimulationTick expected_time,
                              const std::uint64_t expected_register) {
        Interpreter interpreter;
        const auto output = interpreter.add_signal(
            Signal { "fork.output", PackedLogic4::from_msb_string("00") });
        Process process;
        process.id = 0;
        process.name = "fork_lifecycle";
        process.register_count = 4;
        process.debug_locals = {
            DebugLocal {
                "shared", "logic [1:0]", 0, 2, { }, { }, { },
                ValueKind::logic4, { } },
        };
        for (auto& operation : operations) {
            if (auto* write = fsim::runtime::simir::operation_get_if<WriteBlocking>(&operation)) {
                write->signal = output;
            }
        }
        process.operations = std::move(operations);
        (void)interpreter.add_process(std::move(process));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && result.time == expected_time
                && interpreter.signal_value(output).to_msb_string()
                    == expected
                && interpreter.read_debug_local(0, 0).low_word().aval
                    == expected_register,
            "fork lifecycle, shared frame, and deterministic completion");
    };

    run_fork(
        {
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 0, 0) },
            Fork { { 4, 7 }, ForkJoinKind::all },
            WriteBlocking { 0, 0 },
            Halt { },
            WaitFor { 2 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 1, 0) },
            ForkEnd { },
            WaitFor { 1 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 2, 0) },
            ForkEnd { },
        },
        "01", 2, 1);

    run_fork(
        {
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 0, 0) },
            Fork { { 5, 8 }, ForkJoinKind::any },
            WriteBlocking { 0, 0 },
            WaitFork { },
            Halt { },
            WaitFor { 1 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 1, 0) },
            ForkEnd { },
            WaitFor { 2 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 2, 0) },
            ForkEnd { },
        },
        "01", 2, 2);

    run_fork(
        {
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 0, 0) },
            Fork { { 6, 9 }, ForkJoinKind::none },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 3, 0) },
            WaitFork { },
            WriteBlocking { 0, 0 },
            Halt { },
            WaitFor { 2 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 1, 0) },
            ForkEnd { },
            WaitFor { 1 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 2, 0) },
            ForkEnd { },
        },
        "01", 2, 1);

    run_fork(
        {
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 0, 0) },
            Fork { { 5 }, ForkJoinKind::none },
            DisableFork { },
            WaitFor { 2 },
            Halt { },
            WaitFor { 1 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 3, 0) },
            WriteBlocking { 0, 0 },
            ForkEnd { },
        },
        "00", 2, 0);

    run_fork(
        {
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 0, 0) },
            Fork { { 8 }, ForkJoinKind::none },
            Fork { { 12 }, ForkJoinKind::none },
            DisableFork { 1 },
            WaitFork { },
            WriteBlocking { 0, 0 },
            Halt { },
            Halt { },
            WaitFor { 1 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 1, 0) },
            ForkEnd { },
            Halt { },
            WaitFor { 2 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 2, 0) },
            ForkEnd { },
        },
        "10", 2, 2);

    run_fork(
        {
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 0, 0) },
            Fork { { 4 }, ForkJoinKind::any },
            DisableFork { },
            Halt { },
            Fork { { 7 }, ForkJoinKind::none },
            ForkEnd { },
            Halt { },
            WaitFor { 1 },
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 3, 0) },
            WriteBlocking { 0, 0 },
            ForkEnd { },
        },
        "00", 0, 0);

    run_fork(
        {
            LoadConstant { 0, PackedLogic4::from_aval_bval(2, 0, 0) },
            Fork { { 9 }, ForkJoinKind::none },
            LoadConstant { 1, PackedLogic4::from_aval_bval(2, 1, 0) },
            Binary { BinaryOperator::add_unsigned, 0, 0, 1 },
            LoadConstant { 2, PackedLogic4::from_aval_bval(2, 2, 0) },
            Binary { BinaryOperator::less_unsigned, 3, 0, 2 },
            Branch { 3, 1, 7, UnknownBranchPolicy::when_false },
            WaitFork { },
            Halt { },
            WaitFor { 2 },
            ForkEnd { },
        },
        "00", 2, 2);

    {
        Interpreter interpreter;
        Process process;
        process.id = 0;
        process.name = "process_handle_lifecycle";
        process.register_count = 7;
        process.debug_locals = {
            DebugLocal {
                "handle", "process", 0, 64, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "waiting_status", "process::state", 1, 32, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "waiting_completed", "bit", 2, 1, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "finished_status", "process::state", 3, 32, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "finished_completed", "bit", 4, 1, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "killed_status", "process::state", 5, 32, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "killed_completed", "bit", 6, 1, { }, { }, { },
                ValueKind::logic4, { } },
        };
        process.operations = {
            Fork { { 14 }, ForkJoinKind::none },
            Yield { },
            ProcessStatusQuery { 1, 0 },
            ProcessCompleted { 2, 0 },
            ProcessAwait { 0 },
            ProcessStatusQuery { 3, 0 },
            ProcessCompleted { 4, 0 },
            Fork { { 17 }, ForkJoinKind::none },
            Yield { },
            ProcessKill { 0 },
            ProcessStatusQuery { 5, 0 },
            ProcessCompleted { 6, 0 },
            Halt { },
            Halt { },
            ProcessSelf { 0 },
            WaitFor { 2 },
            ForkEnd { },
            ProcessSelf { 0 },
            WaitForever { },
            ForkEnd { },
        };
        (void)interpreter.add_process(std::move(process));
        const auto result = interpreter.run();
        const auto value = [&](const RegisterId id) {
            return interpreter.read_debug_local(0, id).low_word().aval;
        };
        require(
            result.status == RunStatus::completed
                && result.time == 2
                && value(1)
                    == static_cast<std::uint32_t>(ProcessStatus::waiting)
                && value(2) == 0
                && value(3)
                    == static_cast<std::uint32_t>(ProcessStatus::finished)
                && value(4) == 1
                && value(5)
                    == static_cast<std::uint32_t>(ProcessStatus::killed)
                && value(6) == 1,
            "generation-safe process handles await, kill, and report lifecycle");
    }

    {
        Interpreter interpreter;
        Process process;
        process.id = 0;
        process.name = "process_suspend_resume";
        process.register_count = 4;
        process.debug_locals = {
            DebugLocal {
                "handle", "process", 0, 64, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "initial_status", "process::state", 1, 32, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "woken_status", "process::state", 2, 32, { }, { }, { },
                ValueKind::logic4, { } },
            DebugLocal {
                "finished_status", "process::state", 3, 32, { }, { }, { },
                ValueKind::logic4, { } },
        };
        process.operations = {
            Fork { { 12 }, ForkJoinKind::none },
            Yield { },
            ProcessSuspend { 0 },
            ProcessStatusQuery { 1, 0 },
            WaitFor { 6 },
            ProcessStatusQuery { 2, 0 },
            ProcessResume { 0 },
            Yield { },
            ProcessAwait { 0 },
            ProcessStatusQuery { 3, 0 },
            Halt { },
            Halt { },
            ProcessSelf { 0 },
            WaitFor { 2 },
            ForkEnd { },
        };
        (void)interpreter.add_process(std::move(process));
        const auto result = interpreter.run();
        const auto value = [&](const RegisterId id) {
            return interpreter.read_debug_local(0, id).low_word().aval;
        };
        require(
            result.status == RunStatus::completed
                && result.time == 6
                && value(1)
                    == static_cast<std::uint32_t>(ProcessStatus::suspended)
                && value(2)
                    == static_cast<std::uint32_t>(ProcessStatus::suspended)
                && value(3)
                    == static_cast<std::uint32_t>(ProcessStatus::finished),
            "a suspended process retains a timed wake until resume");
    }

    {
        Interpreter interpreter;
        Process process;
        process.id = 0;
        process.name = "process_random_state";
        process.register_count = 2;
        process.string_register_count = 3;
        process.debug_string_locals = {
            DebugStringLocal { "original", 0, { } },
            DebugStringLocal { "seeded", 1, { } },
            DebugStringLocal { "restored", 2, { } },
        };
        process.operations = {
            Fork { { 12 }, ForkJoinKind::none },
            Yield { },
            ProcessGetRandState { 0, 0 },
            LoadConstant {
                1, PackedLogic4::from_aval_bval(32, 0x1234, 0) },
            ProcessSrandom { 0, 1 },
            ProcessGetRandState { 1, 0 },
            ProcessSetRandState { 0, 0 },
            ProcessGetRandState { 2, 0 },
            ProcessKill { 0 },
            Halt { },
            Halt { },
            Halt { },
            ProcessSelf { 0 },
            WaitForever { },
            ForkEnd { },
        };
        (void)interpreter.add_process(std::move(process));
        const auto result = interpreter.run();
        const auto original = interpreter.read_debug_string_local(0, 0);
        const auto seeded = interpreter.read_debug_string_local(0, 1);
        const auto restored = interpreter.read_debug_string_local(0, 2);
        require(
            result.status == RunStatus::completed
                && original.starts_with("fsim-randstate-v1:")
                && original == restored && original != seeded,
            "process random-state tokens survive seed and exact restoration");
    }

    {
        Interpreter interpreter;
        const auto trigger = interpreter.add_signal(
            Signal { "fork.trigger", PackedLogic4::from_msb_string("0") });
        const auto observed = interpreter.add_signal(
            Signal { "fork.observed", PackedLogic4::from_msb_string("0") });
        Process process;
        process.id = 0;
        process.name = "fork_static_sensitivity";
        process.register_count = 1;
        process.static_sensitivity = { { trigger, EdgeKind::any } };
        process.operations = {
            Fork { { 3, 7 }, ForkJoinKind::all },
            Halt { },
            Halt { },
            WaitSensitivity { },
            LoadConstant { 0, PackedLogic4::from_msb_string("1") },
            WriteBlocking { observed, 0 },
            ForkEnd { },
            WaitFor { 1 },
            LoadConstant { 0, PackedLogic4::from_msb_string("1") },
            WriteBlocking { trigger, 0 },
            ForkEnd { },
        };
        (void)interpreter.add_process(std::move(process));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && result.time == 1
                && interpreter.signal_value(observed).to_msb_string() == "1",
            "dynamic fork children register inherited static sensitivity");
    }

    {
        Interpreter interpreter;
        const auto observed = interpreter.add_signal(
            Signal { "fork.filtered", PackedLogic4::from_msb_string("00") });
        Process process;
        process.id = 0;
        process.name = "filtered_fork";
        process.register_count = 1;
        process.operations = {
            Fork { { 5 }, ForkJoinKind::none },
            LoadConstant { 0, PackedLogic4::from_msb_string("11") },
            WriteBlocking { observed, 0 },
            Halt { },
            Halt { },
            LoadConstant { 0, PackedLogic4::from_msb_string("01") },
            WriteBlocking { observed, 0 },
            ForkEnd { },
        };
        (void)interpreter.add_process(std::move(process));
        std::vector<ProcessId> filtered;
        interpreter.set_fork_spawn_filter([&](const ProcessId owner) {
            filtered.push_back(owner);
            return false;
        });
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && filtered == std::vector<ProcessId> { 0 }
                && interpreter.signal_value(observed).to_msb_string() == "11",
            "a rejected dynamic fork continues its parent without spawning");
    }

    {
        Interpreter interpreter;
        Process process;
        process.id = 0;
        process.name = "nested_attempt_identity";
        process.operations = {
            Fork { { 4 }, ForkJoinKind::none },
            WaitFor { 2 },
            Halt { },
            Halt { },
            Fork { { 7 }, ForkJoinKind::none },
            ForkEnd { },
            Halt { },
            WaitFor { 1 },
            ForkEnd { },
        };
        (void)interpreter.add_process(std::move(process));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed
                && interpreter.design_process(2) == 0
                && interpreter.dynamic_process_root(1) == 1
                && interpreter.dynamic_process_root(2) == 1,
            "nested dynamic descendants retain one stable attempt root");
    }

    const auto expect_malformed = [](
                                      std::vector<Operation> operations,
                                      const std::string_view message) {
        Interpreter interpreter;
        Process process;
        process.id = 0;
        process.name = "malformed_fork";
        process.register_count = 2;
        process.operations = std::move(operations);
        (void)interpreter.add_process(std::move(process));
        bool rejected = false;
        try {
            (void)interpreter.run();
        } catch (const InterpreterError& error) {
            rejected = std::string_view { error.what() }.find(message)
                != std::string_view::npos;
        }
        require(rejected, "malformed fork SimIR must be rejected");
    };
    expect_malformed(
        { ForkEnd { }, Halt { } },
        "ForkEnd requires a dynamically spawned fork child");
    expect_malformed(
        { Fork { { 2, 2 }, ForkJoinKind::all }, Halt { }, ForkEnd { } },
        "fork branch entry is duplicated");
    expect_malformed(
        { Fork { { 1 }, ForkJoinKind::all }, ForkEnd { } },
        "fork branch must follow its parent continuation");
    expect_malformed(
        { Fork { { }, ForkJoinKind::all } },
        "fork parent continuation is outside the operation stream");
    expect_malformed(
        {
            Fork { { 2 }, static_cast<ForkJoinKind>(99) },
            Halt { },
            ForkEnd { },
        },
        "fork has an invalid join kind");
    expect_malformed(
        {
            LoadConstant {
                0,
                PackedLogic4::from_aval_bval(
                    64, (std::uint64_t { 99 } << 32U) | 1U, 0) },
            ProcessStatusQuery { 1, 0 },
            Halt { },
        },
        "process handle generation is stale");
}
