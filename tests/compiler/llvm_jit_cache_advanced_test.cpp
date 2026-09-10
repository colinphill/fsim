// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void expect_cache_statistics(
    const LlvmJit& jit,
    std::uint64_t hits,
    std::uint64_t misses,
    std::uint64_t stores,
    std::uint64_t rejected_entries = 0);
[[nodiscard]] std::vector<std::filesystem::path> cached_object_paths(
    const std::filesystem::path& root);
void test_process_module_grouping_at_level(
    JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory);
void test_object_cache_at_level(
    JitOptimizationLevel optimization,
    const std::filesystem::path& cache_directory);
void test_optimization_cache_invalidation(
    const std::filesystem::path& cache_directory);
void test_cache_pruning_integration(
    const std::filesystem::path& cache_directory);
void test_inertial_cache_identity(const std::filesystem::path& cache_directory);
void test_projected_cache_identity(const std::filesystem::path& cache_directory);
void test_projected_waveform_cache_identity(
    const std::filesystem::path& cache_directory);
void test_signed_shift_cache_identity(
    const std::filesystem::path& cache_directory);
void test_integer_cache_identity(const std::filesystem::path& cache_directory);
void test_signal_kind_cache_identity(
    const std::filesystem::path& cache_directory);
void test_wide_constant_cache_identity(
    const std::filesystem::path& cache_directory);
void test_coverage_query_cache_identity(
    const std::filesystem::path& cache_directory);
void test_coverage_sample_cache_identity(
    const std::filesystem::path& cache_directory);
void test_random_distribution_cache_identity(
    const std::filesystem::path& cache_directory);
void test_system_command_cache_identity(
    const std::filesystem::path& cache_directory);
void test_inline_constraint_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_pattern_cache_identity(
    const std::filesystem::path& cache_directory);

void test_static_slice_cache_identity(
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const std::int32_t source_left,
            const std::int32_t destination_left,
            const std::uint32_t element_width,
            const bool two_state,
            const bool signed_elements,
            const bool overlap_snapshot,
            const std::uint32_t source_line) {
            ContainerType source;
            source.element_width = element_width;
            source.two_state = two_state;
            source.signed_elements = signed_elements;
            source.fixed = true;
            source.index_left = source_left;
            source.index_right = source_left - 1;
            auto destination = source;
            destination.index_left = destination_left;
            destination.index_right = destination_left - 1;
            Process process;
            process.id = 32;
            process.name = "cached_static_slice";
            process.container_register_count = 3;
            process.container_register_types = {
                source, destination, destination
            };
            process.operations.push_back(
                DebugPoint {
                    DebugPointKind::statement,
                    SourceLocation {
                        "cached_static_slice.sv",
                        source_line,
                        5 } });
            process.operations.push_back(
                LoadConstant {
                    0,
                    PackedLogic4::from_aval_bval(
                        32,
                        static_cast<std::uint32_t>(
                            source_left),
                        0) });
            process.operations.push_back(
                ContainerRead { 1, 0, 0, true });
            process.operations.push_back(
                LoadConstant {
                    2,
                    PackedLogic4::from_aval_bval(
                        32,
                        static_cast<std::uint32_t>(
                            destination_left),
                        0) });
            process.operations.push_back(
                ContainerWrite { 1, 2, 1, true });
            if (overlap_snapshot) {
                process.operations.push_back(
                    CopyContainerRegister { 2, 1 });
                process.operations.push_back(
                    CopyContainerRegister { 1, 2 });
            }
            process.register_count = 3;
            process.operations.push_back(Halt { });
            return process;
        };
    constexpr std::string_view symbol = "cached_static_slice";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(
                jit, hits, misses, misses);
        };
    materialize(
        make_process(4, 2, 8, false, false, true, 17), 0, 1);
    materialize(
        make_process(4, 2, 8, false, false, true, 17), 1, 0);
    materialize(
        make_process(3, 2, 8, false, false, true, 17), 0, 1);
    materialize(
        make_process(4, 1, 8, false, false, true, 17), 0, 1);
    materialize(
        make_process(4, 2, 4, false, false, true, 17), 0, 1);
    materialize(
        make_process(4, 2, 8, true, false, true, 17), 0, 1);
    materialize(
        make_process(4, 2, 8, false, true, true, 17), 0, 1);
    materialize(
        make_process(4, 2, 8, false, false, false, 17), 0, 1);
    materialize(
        make_process(4, 2, 8, false, false, true, 18), 0, 1);
    assert(cached_object_paths(cache_directory).size() == 8);
}

void test_container_predicate_cache_identity(
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const std::uint8_t constant,
            const ContainerPredicateOperator comparison,
            const ContainerPredicateValueKind value_kind = ContainerPredicateValueKind::element,
            const PackedLogic4& exact_constant = PackedLogic4 { }) {
            ContainerType queue;
            queue.element_width = exact_constant.width() == 0
                ? 8
                : static_cast<std::uint32_t>(exact_constant.width());
            queue.queue = true;
            Process process;
            process.id = 31;
            process.name = "cached_container_predicate";
            process.container_register_count = 2;
            process.container_register_types = { queue, queue };
            process.operations = {
                LocateContainer {
                    ContainerLocatorOperator::find,
                    0,
                    1,
                    {
                        { value_kind
                                    == ContainerPredicateValueKind::index
                                ? ContainerPredicateOperator::index
                                : ContainerPredicateOperator::item,
                            0, 0, PackedLogic4 { }, value_kind },
                        { ContainerPredicateOperator::constant, 0, 0,
                            value_kind
                                    == ContainerPredicateValueKind::index
                                ? PackedLogic4::from_aval_bval(
                                      32, constant, 0)
                                : exact_constant.width() == 0
                                ? PackedLogic4::from_aval_bval(
                                      8, constant, 0)
                                : exact_constant,
                            value_kind },
                        { comparison, 0, 1, PackedLogic4 { },
                            ContainerPredicateValueKind::logical },
                    },
                    { } },
                Halt { },
            };
            return process;
        };
    constexpr std::string_view symbol = "cached_container_predicate";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(
                jit, hits, misses, misses);
        };
    materialize(
        make_process(5, ContainerPredicateOperator::greater),
        0, 1);
    materialize(
        make_process(5, ContainerPredicateOperator::greater),
        1, 0);
    materialize(
        make_process(6, ContainerPredicateOperator::greater),
        0, 1);
    materialize(
        make_process(5, ContainerPredicateOperator::less),
        0, 1);
    materialize(
        make_process(
            5, ContainerPredicateOperator::greater,
            ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            5, ContainerPredicateOperator::greater,
            ContainerPredicateValueKind::index),
        1, 0);
    std::string wide_x_bits(137, '0');
    wide_x_bits.front() = 'x';
    wide_x_bits[134] = '1';
    wide_x_bits[136] = '1';
    auto wide_z_bits = wide_x_bits;
    wide_z_bits.front() = 'z';
    const auto wide_x = PackedLogic4::from_msb_string(wide_x_bits);
    const auto wide_z = PackedLogic4::from_msb_string(wide_z_bits);
    materialize(
        make_process(
            5, ContainerPredicateOperator::greater,
            ContainerPredicateValueKind::element, wide_x),
        0, 1);
    materialize(
        make_process(
            5, ContainerPredicateOperator::greater,
            ContainerPredicateValueKind::element, wide_x),
        1, 0);
    materialize(
        make_process(
            5, ContainerPredicateOperator::greater,
            ContainerPredicateValueKind::element, wide_z),
        0, 1);
    assert(cached_object_paths(cache_directory).size() == 6);
}

void test_container_reduction_cache_identity(
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const bool with_transformation,
            const std::uint32_t threshold,
            const ContainerPredicateValueKind comparison_kind,
            const bool swap_branches = false) {
            ContainerType queue;
            queue.element_width = 32;
            queue.queue = true;
            Process process;
            process.id = 32;
            process.name = "cached_container_reduction";
            process.register_count = 1;
            process.container_register_count = 1;
            process.container_register_types = { queue };
            std::vector<ContainerPredicateNode> transformation;
            if (with_transformation) {
                transformation = {
                    { ContainerPredicateOperator::item, 0, 0,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::element },
                    { comparison_kind
                                == ContainerPredicateValueKind::index
                            ? ContainerPredicateOperator::index
                            : ContainerPredicateOperator::item,
                        0, 0, PackedLogic4 { }, comparison_kind },
                    { ContainerPredicateOperator::constant, 0, 0,
                        PackedLogic4::from_aval_bval(
                            32, threshold, 0),
                        comparison_kind },
                    { ContainerPredicateOperator::greater, 1, 2,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::logical },
                    { ContainerPredicateOperator::constant, 0, 0,
                        PackedLogic4::from_aval_bval(32, 0, 0),
                        ContainerPredicateValueKind::element },
                    { ContainerPredicateOperator::conditional, 3,
                        swap_branches ? 4U : 0U,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::element,
                        swap_branches ? 0U : 4U },
                };
            }
            process.operations = {
                ContainerReduction {
                    ContainerReductionOperator::sum,
                    0,
                    0,
                    std::move(transformation) },
                Halt { },
            };
            return process;
        };
    constexpr std::string_view symbol = "cached_container_reduction";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    materialize(
        make_process(
            false, 0, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            false, 0, ContainerPredicateValueKind::index),
        1, 0);
    materialize(
        make_process(
            true, 0, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            true, 0, ContainerPredicateValueKind::index),
        1, 0);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element,
            true),
        0, 1);
    assert(cached_object_paths(cache_directory).size() == 5);
}

void test_container_ordering_cache_identity(
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const bool with_key,
            const std::uint32_t threshold,
            const ContainerPredicateValueKind comparison_kind,
            const bool swap_branches = false,
            const ContainerOrderingOperator ordering = ContainerOrderingOperator::ascending) {
            ContainerType queue;
            queue.element_width = 32;
            queue.queue = true;
            Process process;
            process.id = 33;
            process.name = "cached_container_ordering";
            process.container_register_count = 1;
            process.container_register_types = { queue };
            std::vector<ContainerPredicateNode> key;
            if (with_key) {
                key = {
                    { ContainerPredicateOperator::item, 0, 0,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::element },
                    { comparison_kind
                                == ContainerPredicateValueKind::index
                            ? ContainerPredicateOperator::index
                            : ContainerPredicateOperator::item,
                        0, 0, PackedLogic4 { }, comparison_kind },
                    { ContainerPredicateOperator::constant, 0, 0,
                        PackedLogic4::from_aval_bval(
                            32, threshold, 0),
                        comparison_kind },
                    { ContainerPredicateOperator::greater, 1, 2,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::logical },
                    { ContainerPredicateOperator::constant, 0, 0,
                        PackedLogic4::from_aval_bval(32, 0, 0),
                        ContainerPredicateValueKind::element },
                    { ContainerPredicateOperator::conditional, 3,
                        swap_branches ? 4U : 0U,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::element,
                        swap_branches ? 0U : 4U },
                };
            }
            process.operations = {
                OrderContainer {
                    ordering, 0, std::move(key) },
                Halt { },
            };
            return process;
        };
    constexpr std::string_view symbol = "cached_container_ordering";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    materialize(
        make_process(
            false, 0, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            false, 0, ContainerPredicateValueKind::index),
        1, 0);
    materialize(
        make_process(
            true, 0, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            true, 0, ContainerPredicateValueKind::index),
        1, 0);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element,
            true),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element,
            false, ContainerOrderingOperator::descending),
        0, 1);
    materialize(
        make_process(
            false, 0, ContainerPredicateValueKind::index,
            false, ContainerOrderingOperator::shuffle),
        0, 1);
    assert(cached_object_paths(cache_directory).size() == 7);
}

void test_container_locator_transformation_cache_identity(
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const bool with_transformation,
            const std::uint32_t threshold,
            const ContainerPredicateValueKind comparison_kind,
            const bool swap_branches = false,
            const ContainerLocatorOperator locator = ContainerLocatorOperator::minimum) {
            ContainerType queue;
            queue.element_width = 32;
            queue.queue = true;
            Process process;
            process.id = 34;
            process.name = "cached_locator_transformation";
            process.container_register_count = 2;
            process.container_register_types = { queue, queue };
            std::vector<ContainerPredicateNode> transformation;
            if (with_transformation) {
                transformation = {
                    { ContainerPredicateOperator::item, 0, 0,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::element },
                    { comparison_kind
                                == ContainerPredicateValueKind::index
                            ? ContainerPredicateOperator::index
                            : ContainerPredicateOperator::item,
                        0, 0, PackedLogic4 { }, comparison_kind },
                    { ContainerPredicateOperator::constant, 0, 0,
                        PackedLogic4::from_aval_bval(
                            32, threshold, 0),
                        comparison_kind },
                    { ContainerPredicateOperator::greater, 1, 2,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::logical },
                    { ContainerPredicateOperator::constant, 0, 0,
                        PackedLogic4::from_aval_bval(32, 0, 0),
                        ContainerPredicateValueKind::element },
                    { ContainerPredicateOperator::conditional, 3,
                        swap_branches ? 4U : 0U,
                        PackedLogic4 { },
                        ContainerPredicateValueKind::element,
                        swap_branches ? 0U : 4U },
                };
            }
            process.operations = {
                LocateContainer {
                    locator, 0, 1, { },
                    std::move(transformation) },
                Halt { },
            };
            return process;
        };
    constexpr std::string_view symbol = "cached_locator_transformation";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    materialize(
        make_process(
            false, 0, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            false, 0, ContainerPredicateValueKind::index),
        1, 0);
    materialize(
        make_process(
            true, 0, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            true, 0, ContainerPredicateValueKind::index),
        1, 0);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::index),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element,
            true),
        0, 1);
    materialize(
        make_process(
            true, 1, ContainerPredicateValueKind::element,
            false, ContainerLocatorOperator::maximum),
        0, 1);
    assert(cached_object_paths(cache_directory).size() == 6);
}

void test_callable_frame_cache_identity(
    const std::filesystem::path& cache_directory)
{
    const auto make_process =
        [](const std::uint32_t identity,
            std::vector<RegisterId> packed,
            std::vector<StringRegisterId> strings,
            std::vector<ContainerRegisterId> containers,
            std::vector<RegisterId> preserve_packed,
            std::vector<StringRegisterId> preserve_strings,
            std::vector<ContainerRegisterId> preserve_containers) {
            ContainerType queue;
            queue.element_width = 8;
            queue.queue = true;
            Process process;
            process.id = 32;
            process.name = "cached_callable_frame";
            process.register_count = 2;
            process.string_register_count = 2;
            process.container_register_count = 2;
            process.container_register_types = { queue, queue };
            process.operations = {
                LoadConstant { 0, PackedLogic4 { 137, Logic4::zero } },
                LoadConstant { 1, PackedLogic4 { 137, Logic4::one } },
                LoadStringConstant { 0, "first" },
                LoadStringConstant { 1, "second" },
                CallableFramePush {
                    identity,
                    std::move(packed),
                    std::move(strings),
                    std::move(containers) },
                CallableFramePop {
                    identity,
                    std::move(preserve_packed),
                    std::move(preserve_strings),
                    std::move(preserve_containers) },
                Halt { },
            };
            return process;
        };
    constexpr std::string_view symbol = "cached_callable_frame";
    const std::array<std::uint32_t, 0> no_signals { };
    const auto options = LlvmJitOptions {
        JitOptimizationLevel::o2, cache_directory
    };
    const auto materialize =
        [&](const Process& process,
            const std::uint64_t hits,
            const std::uint64_t misses) {
            LlvmJit jit { options };
            jit.add_process(symbol, process, no_signals);
            assert(jit.lookup(symbol));
            expect_cache_statistics(jit, hits, misses, misses);
        };
    const auto base = [&] {
        return make_process(1, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 });
    };
    materialize(base(), 0, 1);
    materialize(base(), 1, 0);
    materialize(
        make_process(2, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }),
        0, 1);
    materialize(
        make_process(1, { 1 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }),
        0, 1);
    materialize(
        make_process(1, { 0 }, { 1 }, { 0 }, { 0 }, { 0 }, { 0 }),
        0, 1);
    materialize(
        make_process(1, { 0 }, { 0 }, { 1 }, { 0 }, { 0 }, { 0 }),
        0, 1);
    materialize(
        make_process(1, { 0 }, { 0 }, { 0 }, { 1 }, { 0 }, { 0 }),
        0, 1);
    materialize(
        make_process(1, { 0 }, { 0 }, { 0 }, { 0 }, { 1 }, { 0 }),
        0, 1);
    materialize(
        make_process(1, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 1 }),
        0, 1);
    assert(cached_object_paths(cache_directory).size() == 8);
}

void test_persistent_object_cache()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("fsim-llvm-object-cache-" + std::to_string(serial));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    assert(!error);

    test_object_cache_at_level(JitOptimizationLevel::o0, root / "o0");
    test_object_cache_at_level(JitOptimizationLevel::o2, root / "o2");
    test_optimization_cache_invalidation(root / "optimization");
    test_process_module_grouping_at_level(
        JitOptimizationLevel::o0, root / "group-o0");
    test_process_module_grouping_at_level(
        JitOptimizationLevel::o2, root / "group-o2");
    test_cache_pruning_integration(root / "pruning");
    test_inertial_cache_identity(root / "inertial");
    test_projected_cache_identity(root / "projected");
    test_projected_waveform_cache_identity(
        root / "projected-waveform");
    test_signed_shift_cache_identity(root / "signed-shift");
    test_integer_cache_identity(root / "integer");
    test_signal_kind_cache_identity(root / "signal-kind");
    test_wide_constant_cache_identity(root / "wide-constant");
    test_coverage_query_cache_identity(root / "coverage-query");
    test_coverage_sample_cache_identity(root / "coverage-sample");
    test_random_distribution_cache_identity(root / "random-distribution");
    test_system_command_cache_identity(root / "system-command");
    test_inline_constraint_cache_identity(root / "inline-constraint");
    test_static_pattern_cache_identity(
        root / "static-pattern");
    test_static_slice_cache_identity(
        root / "static-slice");
    test_static_slice_consumer_cache_identity(
        root / "static-slice-consumer");
    test_static_slice_call_cache_identity(
        root / "static-slice-call");
    test_static_slice_ordering_cache_identity(
        root / "static-slice-ordering");
    test_static_slice_port_cache_identity(
        root / "static-slice-port");
    test_static_indexed_slice_cache_identity(
        root / "static-indexed-slice");
    test_fixed_array_function_return_cache_identity(
        root / "fixed-array-function-return");
    test_nonstatic_function_return_cache_identity(
        root / "nonstatic-function-return");
    test_expression_selection_cache_identity(
        root / "expression-selection");
    test_procedural_update_cache_identity(root / "procedural-update");
    test_container_construction_cache_identity(root / "container-construction");
    test_container_predicate_cache_identity(
        root / "container-predicate");
    test_container_reduction_cache_identity(
        root / "container-reduction");
    test_container_ordering_cache_identity(
        root / "container-ordering");
    test_container_locator_transformation_cache_identity(
        root / "locator-transformation");
    test_callable_frame_cache_identity(root / "callable-frame");
    test_vhdl_language_profile_cache_identity(
        root / "vhdl-language-profile");
    const auto language_mode_key = [](
        const std::string_view language,
        const std::string_view standard,
        const std::string_view compatibility) {
        fsim::compiler::CacheKeyBuilder builder;
        builder.add("cache-schema", "fsim-hdl-standard-compatibility-v1");
        builder.add("language", language).add("standard", standard);
        return builder.add("compatibility-profile", compatibility).finish();
    };
    const auto verilog_default = language_mode_key("verilog", "2005", "none");
    const auto systemverilog_default = language_mode_key("systemverilog", "2017", "none");
    const auto older_revision = language_mode_key("systemverilog", "2009", "none");
    const auto compatibility_override = language_mode_key(
        "systemverilog", "2009", "implicit-net,sizing");
    assert(verilog_default != systemverilog_default);
    assert(systemverilog_default != older_revision);
    assert(older_revision != compatibility_override);
    assert(compatibility_override == language_mode_key(
        "systemverilog", "2009", "implicit-net,sizing"));

    std::filesystem::remove_all(root, error);
    assert(!error);
}

} // namespace fsim::tests::compiler
