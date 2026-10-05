// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_region_frontier_logic9_words_test.hpp"

#include "llvm/logic9_word_lowering.hpp"

#include <fsim/runtime/packed_value.hpp>
#include <fsim/runtime/simir.hpp>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::compiler::test {
namespace {

using llvm::IRBuilder;
using llvm_detail::Logic9WordValue;
using llvm_detail::emit_logic9_binary;
using llvm_detail::emit_logic9_canonical_guard;
using llvm_detail::emit_logic9_copy;
using llvm_detail::emit_logic9_not;
using runtime::PackedLogic4;
using runtime::RunStatus;
using runtime::simir::BinaryOperator;
using runtime::simir::Interpreter;
using runtime::simir::Process;
using runtime::simir::ResolutionKind;
using runtime::simir::ValueKind;

using NativeLogic9Operation = std::uint32_t (*) (
    const std::uint64_t*, const std::uint64_t*, std::uint64_t*);

constexpr std::array<char, 9U> logic9_states {
    'U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-',
};
constexpr std::array<std::uint32_t, 5U> test_widths {
    1U, 65U, 129U, 256U, 1024U,
};
constexpr std::array<BinaryOperator, 3U> binary_operators {
    BinaryOperator::bit_and,
    BinaryOperator::bit_or,
    BinaryOperator::bit_xor,
};

enum class TestOperation : std::uint8_t {
    copy,
    unary_not,
    binary_and,
    binary_or,
    binary_xor,
};

enum class TestOptimization : std::uint8_t {
    o0,
    o2,
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] std::uint32_t word_count_for(const std::uint32_t width) noexcept
{
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(width) + 63U) / 64U);
}

[[nodiscard]] std::uint64_t word_mask(
    const std::uint32_t width, const std::uint32_t word) noexcept
{
    if (word + 1U < word_count_for(width) || width % 64U == 0U) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (UINT64_C(1) << (width % 64U)) - 1U;
}

[[nodiscard]] std::string symbol_name(
    const TestOperation operation, const std::uint32_t width)
{
    std::string result { "frontier_logic9_" };
    switch (operation) {
    case TestOperation::copy:
        result.append("copy");
        break;
    case TestOperation::unary_not:
        result.append("not");
        break;
    case TestOperation::binary_and:
        result.append("and");
        break;
    case TestOperation::binary_or:
        result.append("or");
        break;
    case TestOperation::binary_xor:
        result.append("xor");
        break;
    }
    result.push_back('_');
    result.append(std::to_string(width));
    return result;
}

[[nodiscard]] bool is_binary(const TestOperation operation) noexcept
{
    return operation == TestOperation::binary_and
        || operation == TestOperation::binary_or
        || operation == TestOperation::binary_xor;
}

[[nodiscard]] BinaryOperator binary_operator(
    const TestOperation operation)
{
    switch (operation) {
    case TestOperation::binary_and:
        return BinaryOperator::bit_and;
    case TestOperation::binary_or:
        return BinaryOperator::bit_or;
    case TestOperation::binary_xor:
        return BinaryOperator::bit_xor;
    case TestOperation::copy:
    case TestOperation::unary_not:
        break;
    }
    throw std::logic_error { "non-binary Logic9 test operation" };
}

[[nodiscard]] Logic9WordValue load_words(
    IRBuilder<>& builder, llvm::Value* const base,
    const std::uint32_t width)
{
    Logic9WordValue value;
    value.width = width;
    auto* const i64 = llvm::Type::getInt64Ty(builder.getContext());
    const auto word_count = word_count_for(width);
    for (auto& plane : value.planes) {
        plane.reserve(word_count);
    }
    for (std::size_t plane = 0U; plane < value.planes.size(); ++plane) {
        for (std::uint32_t word = 0U; word < word_count; ++word) {
            const auto offset = static_cast<std::uint64_t>(plane)
                * word_count + word;
            auto* const address = builder.CreateGEP(i64, base,
                builder.getInt64(offset));
            value.planes[plane].push_back(
                builder.CreateLoad(i64, address));
        }
    }
    return value;
}

void store_words(IRBuilder<>& builder, llvm::Value* const base,
    const Logic9WordValue& value)
{
    auto* const i64 = llvm::Type::getInt64Ty(builder.getContext());
    const auto word_count = word_count_for(value.width);
    for (std::size_t plane = 0U; plane < value.planes.size(); ++plane) {
        for (std::uint32_t word = 0U; word < word_count; ++word) {
            const auto offset = static_cast<std::uint64_t>(plane)
                * word_count + word;
            auto* const address = builder.CreateGEP(i64, base,
                builder.getInt64(offset));
            const auto mask = builder.getInt64(word_mask(value.width, word));
            builder.CreateStore(builder.CreateAnd(
                value.planes[plane][word], mask), address);
        }
    }
}

[[nodiscard]] llvm::Function* add_generated_operation(
    llvm::Module& module, const TestOperation operation,
    const std::uint32_t width)
{
    auto& context = module.getContext();
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const function_type = llvm::FunctionType::get(i32,
        { pointer, pointer, pointer }, false);
    auto* const function = llvm::Function::Create(function_type,
        llvm::Function::ExternalLinkage,
        symbol_name(operation, width), module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const accepted = llvm::BasicBlock::Create(
        context, "accepted", function);
    auto* const declined = llvm::BasicBlock::Create(
        context, "declined", function);
    IRBuilder<> builder(entry);

    auto argument = function->arg_begin();
    llvm::Value* const left_pointer = &*argument++;
    llvm::Value* const right_pointer = &*argument++;
    llvm::Value* const output_pointer = &*argument;
    const auto left = load_words(builder, left_pointer, width);
    auto* valid = emit_logic9_canonical_guard(builder, left);
    require(valid != nullptr,
        "the canonical preflight predicate emits for a valid shape");

    Logic9WordValue right;
    if (is_binary(operation)) {
        right = load_words(builder, right_pointer, width);
        auto* const right_valid = emit_logic9_canonical_guard(builder, right);
        require(right_valid != nullptr,
            "the second canonical predicate emits for a valid shape");
        valid = builder.CreateAnd(valid, right_valid);
    }
    builder.CreateCondBr(valid, accepted, declined);

    builder.SetInsertPoint(declined);
    builder.CreateRet(builder.getInt32(0U));

    builder.SetInsertPoint(accepted);
    std::optional<Logic9WordValue> result;
    switch (operation) {
    case TestOperation::copy:
        result = emit_logic9_copy(builder, left);
        break;
    case TestOperation::unary_not:
        result = emit_logic9_not(builder, left);
        break;
    case TestOperation::binary_and:
    case TestOperation::binary_or:
    case TestOperation::binary_xor:
        result = emit_logic9_binary(builder, left, right,
            binary_operator(operation));
        break;
    }
    require(result.has_value(), "the selected Logic9 helper emits");
    store_words(builder, output_pointer, *result);
    builder.CreateRet(builder.getInt32(1U));
    return function;
}

[[nodiscard]] PackedLogic4 logic9_pattern(
    const std::uint32_t width, const std::uint32_t first_state)
{
    std::string value(width, 'U');
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        value[width - bit - 1U]
            = logic9_states[(first_state + bit) % logic9_states.size()];
    }
    return PackedLogic4::from_logic9_msb_string(value);
}

[[nodiscard]] std::pair<PackedLogic4, PackedLogic4> logic9_pair_pattern(
    const std::uint32_t width, const std::uint32_t first_pair)
{
    std::string left(width, 'U');
    std::string right(width, 'U');
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        const auto pair = (first_pair + bit) % 81U;
        left[width - bit - 1U] = logic9_states[pair / 9U];
        right[width - bit - 1U] = logic9_states[pair % 9U];
    }
    return {
        PackedLogic4::from_logic9_msb_string(left),
        PackedLogic4::from_logic9_msb_string(right),
    };
}

[[nodiscard]] PackedLogic4 run_interpreter_operation(
    const TestOperation operation, const PackedLogic4& left,
    const PackedLogic4* const right)
{
    Interpreter interpreter;
    const auto left_signal = interpreter.add_signal({
        "frontier.logic9.left", left, ResolutionKind::none,
        ValueKind::logic9,
    });
    std::optional<runtime::simir::SignalId> right_signal;
    if (is_binary(operation)) {
        require(right != nullptr,
            "binary Logic9 oracle receives a right input");
        right_signal = interpreter.add_signal({
            "frontier.logic9.right", *right, ResolutionKind::none,
            ValueKind::logic9,
        });
    }
    const auto output_signal = interpreter.add_signal({
        "frontier.logic9.output",
        PackedLogic4::from_logic9_msb_string(
            std::string(left.width(), 'U')),
        ResolutionKind::none, ValueKind::logic9,
    });

    Process process;
    process.id = 0U;
    process.name = "frontier_logic9_truth_table_oracle";
    process.register_count = is_binary(operation) ? 3U : 2U;
    process.register_value_kinds.assign(
        process.register_count, ValueKind::logic9);
    process.operations.push_back(runtime::simir::ReadSignal {
        0U, left_signal,
    });
    if (operation == TestOperation::copy) {
        process.operations.push_back(runtime::simir::CopyRegister {
            1U, 0U,
        });
        process.operations.push_back(runtime::simir::WriteBlocking {
            output_signal, 1U,
        });
    } else if (operation == TestOperation::unary_not) {
        process.operations.push_back(runtime::simir::UnaryNot {
            1U, 0U,
        });
        process.operations.push_back(runtime::simir::WriteBlocking {
            output_signal, 1U,
        });
    } else {
        process.operations.push_back(runtime::simir::ReadSignal {
            1U, *right_signal,
        });
        process.operations.push_back(runtime::simir::Binary {
            binary_operator(operation), 2U, 0U, 1U,
        });
        process.operations.push_back(runtime::simir::WriteBlocking {
            output_signal, 2U,
        });
    }
    process.operations.push_back(runtime::simir::Halt { });
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(result.status == RunStatus::completed,
        "the Logic9 interpreter oracle completes");
    return interpreter.signal_value(output_signal);
}

[[nodiscard]] std::vector<std::uint64_t> flatten_logic9(
    const PackedLogic4& value)
{
    require(value.is_logic9() && value.width() != 0U,
        "the Logic9 test value has an exact nine-state kind");
    const auto width = static_cast<std::uint32_t>(value.width());
    const auto words = word_count_for(width);
    std::vector<std::uint64_t> result(
        std::size_t { words } * 4U, 0U);
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        const auto source = value.logic9_plane_words(plane);
        require(source.size() == words,
            "each Logic9 plane has the expected word count");
        std::copy(source.begin(), source.end(),
            result.begin() + static_cast<std::ptrdiff_t>(plane * words));
    }
    return result;
}

[[nodiscard]] PackedLogic4 expand_logic9(
    const std::uint32_t width, const std::vector<std::uint64_t>& words)
{
    const auto count = word_count_for(width);
    require(words.size() == std::size_t { count } * 4U,
        "the generated Logic9 output has four complete planes");
    return PackedLogic4::from_logic9_word_planes(width,
        std::span<const std::uint64_t> { words.data(), count },
        std::span<const std::uint64_t> { words.data() + count, count },
        std::span<const std::uint64_t> { words.data() + count * 2U, count },
        std::span<const std::uint64_t> { words.data() + count * 3U, count });
}

void require_equal_logic9(const PackedLogic4& actual,
    const PackedLogic4& expected)
{
    require(actual.is_logic9() && expected.is_logic9()
            && actual.width() == expected.width(),
        "generated and interpreted values retain Logic9 kind and width");
    for (std::size_t bit = 0U; bit < actual.width(); ++bit) {
        require(actual.get_logic9(bit) == expected.get_logic9(bit),
            "generated result matches the interpreter at every bit");
    }
}

void require_canonical_output_words(
    const std::uint32_t width,
    const std::vector<std::uint64_t>& words)
{
    const auto count = word_count_for(width);
    const auto mask = word_mask(width, count - 1U);
    for (std::uint32_t word = 0U; word < count; ++word) {
        const auto p0 = words[word];
        const auto p1 = words[count + word];
        const auto p2 = words[count * 2U + word];
        const auto p3 = words[count * 3U + word];
        require((p3 & (p0 | p1 | p2)) == 0U,
            "generated output never contains a reserved Logic9 code");
        if (word + 1U == count) {
            require(((p0 | p1 | p2 | p3) & ~mask) == 0U,
                "generated output has zero unused tail bits");
        }
    }
}

void run_validity_cases(NativeLogic9Operation copy_one,
    NativeLogic9Operation copy_wide)
{
    constexpr auto sentinel = UINT64_C(0xa55aa55aa55aa55a);
    std::array<std::uint64_t, 4U> output { sentinel, sentinel,
        sentinel, sentinel };
    std::array<std::uint64_t, 4U> input { };
    for (std::uint32_t code = 9U; code < 16U; ++code) {
        for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
            input[plane] = ((code >> plane) & 1U) != 0U ? 1U : 0U;
        }
        output.fill(sentinel);
        const auto status = copy_one(input.data(), nullptr, output.data());
        require(status == 0U && std::ranges::all_of(output,
                    [](const auto word) { return word == sentinel; }),
            "every reserved code declines before output mutation");
    }

    input.fill(0U);
    input[3U] = 1U;
    output.fill(sentinel);
    require(copy_one(input.data(), nullptr, output.data()) == 1U,
        "ordinal code eight is accepted");
    require(output[3U] == 1U && output[0U] == 0U
            && output[1U] == 0U && output[2U] == 0U,
        "copy preserves the legal dont-care code exactly");

    constexpr std::uint32_t wide_width = 65U;
    constexpr std::size_t wide_words = 2U;
    std::array<std::uint64_t, 8U> wide_input { };
    std::array<std::uint64_t, 8U> wide_output { };
    wide_input[1U] = UINT64_C(1) << 1U;
    wide_output.fill(sentinel);
    require(copy_wide(wide_input.data(), nullptr, wide_output.data()) == 0U
            && std::ranges::all_of(wide_output,
                [](const auto word) { return word == sentinel; }),
        "nonzero high tail bits decline before output mutation");
    require(wide_width == wide_words * 64U - 63U,
        "wide tail fixture uses one active bit in its last word");
}

[[nodiscard]] std::unique_ptr<llvm::orc::LLJIT> make_jit(
    const TestOptimization optimization)
{
    auto target = llvm::cantFail(
        llvm::orc::JITTargetMachineBuilder::detectHost());
    target.setCodeGenOptLevel(optimization == TestOptimization::o0
            ? llvm::CodeGenOptLevel::None
            : llvm::CodeGenOptLevel::Default);
    llvm::orc::LLJITBuilder builder;
    builder.setNumCompileThreads(0U);
    builder.setJITTargetMachineBuilder(std::move(target));
    return llvm::cantFail(builder.create());
}

void optimize_module(llvm::Module& module,
    const TestOptimization optimization)
{
    llvm::LoopAnalysisManager loop_analyses;
    llvm::FunctionAnalysisManager function_analyses;
    llvm::CGSCCAnalysisManager cgscc_analyses;
    llvm::ModuleAnalysisManager module_analyses;
    llvm::PassBuilder pass_builder;
    pass_builder.registerModuleAnalyses(module_analyses);
    pass_builder.registerCGSCCAnalyses(cgscc_analyses);
    pass_builder.registerFunctionAnalyses(function_analyses);
    pass_builder.registerLoopAnalyses(loop_analyses);
    pass_builder.crossRegisterProxies(loop_analyses, function_analyses,
        cgscc_analyses, module_analyses);
    auto pipeline = pass_builder.buildPerModuleDefaultPipeline(
        optimization == TestOptimization::o0
            ? llvm::OptimizationLevel::O0
            : llvm::OptimizationLevel::O2);
    pipeline.run(module, module_analyses);
}

void run_generated_tests(const TestOptimization optimization)
{
    auto jit = make_jit(optimization);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>(
        optimization == TestOptimization::o0
            ? "frontier-logic9-word-lowering-o0"
            : "frontier-logic9-word-lowering-o2",
        *context);
    module->setDataLayout(jit->getDataLayout());
    module->setTargetTriple(jit->getTargetTriple());

    for (const auto width : test_widths) {
        for (const auto operation : {
                 TestOperation::copy,
                 TestOperation::unary_not,
                 TestOperation::binary_and,
                 TestOperation::binary_or,
                 TestOperation::binary_xor,
             }) {
            (void)add_generated_operation(*module, operation, width);
        }
    }
    std::string verification_error;
    llvm::raw_string_ostream verification_stream { verification_error };
    require(!llvm::verifyModule(*module, &verification_stream),
        "the standalone Logic9 helper module verifies");
    optimize_module(*module, optimization);
    verification_error.clear();
    require(!llvm::verifyModule(*module, &verification_stream),
        "the optimized Logic9 helper module verifies");
    llvm::cantFail(jit->addIRModule(llvm::orc::ThreadSafeModule {
        std::move(module), std::move(context),
    }));

    for (const auto width : test_widths) {
        for (const auto operation : {
                 TestOperation::copy,
                 TestOperation::unary_not,
             }) {
            const auto input = logic9_pattern(width, 0U);
            const auto expected = run_interpreter_operation(
                operation, input, nullptr);
            auto left = flatten_logic9(input);
            std::vector<std::uint64_t> output(left.size(),
                UINT64_C(0xcccccccccccccccc));
            const auto entry = llvm::cantFail(jit->lookup(
                symbol_name(operation, width))).toPtr<NativeLogic9Operation>();
            require(entry(left.data(), nullptr, output.data()) == 1U,
                "canonical Logic9 unary input is admitted");
            require_canonical_output_words(width, output);
            require_equal_logic9(expand_logic9(width, output), expected);
        }

        for (const auto operation : binary_operators) {
            const auto test_operation = operation == BinaryOperator::bit_and
                ? TestOperation::binary_and
                : operation == BinaryOperator::bit_or
                ? TestOperation::binary_or
                : TestOperation::binary_xor;
            const auto entry = llvm::cantFail(jit->lookup(
                symbol_name(test_operation, width))).toPtr<NativeLogic9Operation>();
            for (std::uint32_t first_pair = 0U;
                 first_pair < 81U; first_pair += width) {
                const auto [left_value, right_value]
                    = logic9_pair_pattern(width, first_pair);
                const auto expected = run_interpreter_operation(
                    test_operation, left_value, &right_value);
                auto left = flatten_logic9(left_value);
                auto right = flatten_logic9(right_value);
                std::vector<std::uint64_t> output(left.size(),
                    UINT64_C(0xcccccccccccccccc));
                require(entry(left.data(), right.data(), output.data()) == 1U,
                    "canonical Logic9 binary inputs are admitted");
                require_canonical_output_words(width, output);
                require_equal_logic9(expand_logic9(width, output), expected);
            }
        }
    }

    const auto copy_one = llvm::cantFail(jit->lookup(
        symbol_name(TestOperation::copy, 1U))).toPtr<NativeLogic9Operation>();
    const auto copy_wide = llvm::cantFail(jit->lookup(
        symbol_name(TestOperation::copy, 65U))).toPtr<NativeLogic9Operation>();
    run_validity_cases(copy_one, copy_wide);
}

} // namespace

void run_region_frontier_logic9_word_lowering_tests()
{
    for (const auto optimization : {
             TestOptimization::o0,
             TestOptimization::o2,
         }) {
        run_generated_tests(optimization);
    }
}

} // namespace fsim::compiler::test
