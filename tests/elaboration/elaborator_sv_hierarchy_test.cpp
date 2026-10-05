// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/support/environment.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"
#include "../../src/elaboration/elaborated_design_process_access.hpp"
#include "../../src/elaboration/systemverilog_template_bindings.hpp"

#include <cstdlib>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::elaboration {
namespace {

    void append_units(
        fsim::frontend::ParsedDesign& destination,
        fsim::frontend::ParseResult parsed,
        const std::string_view library)
    {
        assert(parsed.ok());
        for (auto& unit : parsed.design.units) {
            unit.library = library;
            destination.units.push_back(std::move(unit));
        }
    }

    const fsim::elaboration::SpecializationInfo* find_specialization(
        const fsim::elaboration::ElaboratedDesign& design,
        const std::string_view instance)
    {
        const auto found = std::ranges::find_if(
            design.specializations(),
            [&](const auto& specialization) {
                return specialization.instance == instance;
            });
        return found == design.specializations().end() ? nullptr : &*found;
    }

    int set_profile_environment(const char* key, const char* value)
    {
#if defined(_WIN32)
        return ::_putenv_s(key, value == nullptr ? "" : value);
#else
        return value == nullptr ? ::unsetenv(key) : ::setenv(key, value, 1);
#endif
    }

    class ProfileCapture final {
    public:
        explicit ProfileCapture(std::ostringstream& output)
        : previous_environment_ {
              fsim::support::environment_variable("FSIM_PROFILE_PHASES")
          }
        {
            if (set_profile_environment("FSIM_PROFILE_PHASES", "1") != 0) {
                throw std::runtime_error {
                    "failed to enable phase profiling for template test"
                };
            }
            previous_stream_ = std::cerr.rdbuf(output.rdbuf());
        }

        ProfileCapture(const ProfileCapture&) = delete;
        ProfileCapture& operator=(const ProfileCapture&) = delete;

        ~ProfileCapture()
        {
            std::cerr.rdbuf(previous_stream_);
            const auto restored = set_profile_environment(
                "FSIM_PROFILE_PHASES",
                previous_environment_
                    ? previous_environment_->c_str()
                    : nullptr);
            (void)restored;
            assert(restored == 0);
        }

    private:
        std::optional<std::string> previous_environment_;
        std::streambuf* previous_stream_ { };
    };

    std::uint64_t profile_metric(
        const std::string_view row,
        const std::string_view name)
    {
        const auto marker = std::string { name } + "=";
        const auto position = row.find(marker);
        assert(position != std::string_view::npos);
        return std::stoull(std::string { row.substr(
        position + marker.size()) });
    }

    void test_systemverilog_template_exact_element_bindings()
    {
        using fsim::elaboration::ContainerObjectInfo;
        using fsim::elaboration::SignalInfo;
        using fsim::elaboration::SystemVerilogTemplateSignalRole;
        using fsim::elaboration::make_systemverilog_template_signal_remap;
        using fsim::elaboration::remap_systemverilog_template_process;
        using fsim::elaboration::resolve_systemverilog_template_element_signals;
        using fsim::elaboration::elaboration_detail::
            ContainerDeclarationBinding;
        using fsim::elaboration::elaboration_detail::
            ContainerDeclarationBindings;
        using fsim::semantic::SpecializedHirOverlay;
        using fsim::runtime::Logic4;
        using fsim::runtime::PackedLogic4;
        using namespace fsim::runtime::simir;

        const auto declaration_a
            = fsim::semantic::DeclarationId::from_index(31U);
        const auto declaration_b
            = fsim::semantic::DeclarationId::from_index(32U);
        SpecializedHirOverlay source_overlay;
        source_overlay.unit = fsim::semantic::UnitId::from_index(3U);
        source_overlay.scope = fsim::semantic::ScopeId::from_index(1U);
        source_overlay.language = fsim::semantic::Language::system_verilog;
        SpecializedHirOverlay target_overlay = source_overlay;
        SpecializedHirOverlay foreign_overlay = source_overlay;
        foreign_overlay.scope = fsim::semantic::ScopeId::from_index(2U);
        const auto container_type = [] {
            ContainerType type;
            type.element_kind = ContainerElementKind::Packed;
            type.element_width = 1U;
            type.fixed = true;
            type.index_left = 1;
            type.index_right = 0;
            type.dimensions.emplace_back(1, 0);
            return type;
        };

        std::vector<ContainerObjectInfo> object_info;
        std::vector<ContainerObject> objects;
        std::vector<ContainerElementSignalAlias> element_aliases;
        std::vector<ContainerAggregateSignalAlias> aggregate_aliases;
        std::vector<SignalInfo> signal_info;
        std::vector<Signal> signals;
        const auto add_signal = [&](const SignalId id,
                                    const std::string& name,
                                    const std::size_t width) {
            SignalInfo info;
            info.id = id;
            info.name = name;
            info.width = width;
            info.type_name = "logic";
            info.source_domain = fsim::frontend::ValueDomain::Logic4;
            info.systemverilog_net_type = "wire";
            info.resolution = ResolutionKind::sv_wire;
            Signal signal {
                name,
                PackedLogic4(width, Logic4::zero),
                ResolutionKind::sv_wire,
                ValueKind::logic4,
            };
            signal_info.push_back(std::move(info));
            signals.push_back(std::move(signal));
        };
        const auto add_object = [&](const ContainerObjectId id,
                                    const std::string& full_name) {
            auto type = container_type();
            ContainerObjectInfo info;
            info.id = id;
            info.name = full_name;
            info.type = type;
            object_info.push_back(std::move(info));
            objects.push_back(ContainerObject {
                full_name, default_container_value(type), std::nullopt
            });
            const auto first_signal = static_cast<SignalId>(id * 3U);
            for (std::uint32_t ordinal = 0U; ordinal < 2U; ++ordinal) {
                const auto source_index = ordinal == 0U ? 1 : 0;
                const auto name = full_name + "["
                    + std::to_string(source_index) + "]";
                const auto signal = first_signal + ordinal;
                add_signal(signal, name, 1U);
                element_aliases.push_back({
                    id, ordinal, signal, true, true
                });
            }
            const auto aggregate_signal = first_signal + 2U;
            add_signal(aggregate_signal, full_name, 2U);
            aggregate_aliases.push_back({
                id, aggregate_signal, true, false
            });
        };

        // The two HIR declarations have the same simple spelling in nested
        // scopes. Their declaration IDs and materialized objects stay distinct.
        add_object(0U, "top.source.outer.arr");
        add_object(1U, "top.source.inner.arr");
        add_object(2U, "top.target.outer.arr");
        add_object(3U, "top.target.inner.arr");

        ContainerDeclarationBindings source_bindings;
        source_bindings.emplace(
            declaration_a.value(), ContainerDeclarationBinding {
                0U, false, &source_overlay });
        source_bindings.emplace(
            declaration_b.value(), ContainerDeclarationBinding {
                1U, false, &source_overlay });
        ContainerDeclarationBindings target_parent;
        target_parent.emplace(
            declaration_a.value(), ContainerDeclarationBinding {
                2U, false, &target_overlay });
        ContainerDeclarationBindings target_bindings { &target_parent };
        target_bindings.insert_or_assign(
            declaration_b.value(), ContainerDeclarationBinding {
                3U, false, &target_overlay });
        ContainerDeclarationBindings foreign_bindings;
        foreign_bindings.emplace(
            declaration_b.value(), ContainerDeclarationBinding {
                1U, false, &foreign_overlay });

        const auto source_a = resolve_systemverilog_template_element_signals(
            declaration_a, source_overlay, source_bindings, object_info,
            objects,
            element_aliases, aggregate_aliases, signal_info, signals);
        const auto source_b = resolve_systemverilog_template_element_signals(
            declaration_b, source_overlay, source_bindings, object_info,
            objects,
            element_aliases, aggregate_aliases, signal_info, signals);
        const auto target_a = resolve_systemverilog_template_element_signals(
            declaration_a, target_overlay, target_bindings, object_info,
            objects,
            element_aliases, aggregate_aliases, signal_info, signals);
        const auto target_b = resolve_systemverilog_template_element_signals(
            declaration_b, target_overlay, target_bindings, object_info,
            objects,
            element_aliases, aggregate_aliases, signal_info, signals);
        assert(source_a && source_b && target_a && target_b);
        assert(source_a->front().signal != source_b->front().signal);
        assert(target_a->front().signal != target_b->front().signal);
        assert(target_b->front().signal == 9U);
        assert(!resolve_systemverilog_template_element_signals(
            declaration_b, target_overlay, source_bindings, object_info,
            objects, element_aliases, aggregate_aliases, signal_info,
            signals));
        assert(!resolve_systemverilog_template_element_signals(
            declaration_b, source_overlay, foreign_bindings, object_info,
            objects, element_aliases, aggregate_aliases, signal_info,
            signals));

        std::vector<SystemVerilogTemplateSignalRole> source_roles;
        std::vector<SystemVerilogTemplateSignalRole> target_roles;
        std::vector<SystemVerilogTemplateSignalRole> wrong_scope_roles;
        for (std::size_t index = 0U; index < source_b->size(); ++index) {
            const auto& source = (*source_b)[index];
            const auto& target = (*target_b)[index];
            const auto& wrong_scope = (*target_a)[index];
            source_roles.push_back({
                declaration_b, source.signal, source.ordinal,
                source.container_type, source.read_only,
                source.readable, source.writable,
            });
            target_roles.push_back({
                declaration_b, target.signal, target.ordinal,
                target.container_type, target.read_only,
                target.readable, target.writable,
            });
            wrong_scope_roles.push_back({
                declaration_a, wrong_scope.signal, wrong_scope.ordinal,
                wrong_scope.container_type, wrong_scope.read_only,
                wrong_scope.readable, wrong_scope.writable,
            });
        }
        const auto remap = make_systemverilog_template_signal_remap(
            source_roles, target_roles, signal_info, signals);
        assert(remap && remap->at(source_b->at(0U).signal)
            == target_b->at(0U).signal);
        assert(!make_systemverilog_template_signal_remap(
            source_roles, wrong_scope_roles, signal_info, signals));

        auto mismatched_layout = target_roles;
        mismatched_layout.front().container_type->index_left = 0;
        mismatched_layout.front().container_type->index_right = 1;
        mismatched_layout.front().container_type->dimensions.front()
            = { 0, 1 };
        assert(!make_systemverilog_template_signal_remap(
            source_roles, mismatched_layout, signal_info, signals));
        auto mismatched_access = target_roles;
        mismatched_access.front().read_only = true;
        assert(!make_systemverilog_template_signal_remap(
            source_roles, mismatched_access, signal_info, signals));

        Process replay;
        replay.operations.emplace_back(
            ReadSignal { 0U, source_b->at(0U).signal });
        replay.operations.emplace_back(WriteUpdate {
            source_b->at(1U).signal,
            0U,
            SignalUpdateDomain::systemverilog_active,
        });
        replay.static_sensitivity.push_back({
            source_b->at(0U).signal, EdgeKind::any, 1U, 1U
        });
        replay.driver_regions.push_back({
            source_b->at(1U).signal, 0U, 1U, false
        });
        assert(remap_systemverilog_template_process(
            replay, *remap, "top.source.inner", "top.target.inner",
            signal_info));
        const auto read = replay.operations.expanded(0U);
        const auto write = replay.operations.expanded(1U);
        assert(operation_get_if<ReadSignal>(&read)->signal
            == target_b->at(0U).signal);
        assert(operation_get_if<WriteUpdate>(&write)->signal
            == target_b->at(1U).signal);
        assert(replay.static_sensitivity.size() == 1U);
        assert(replay.static_sensitivity.front().signal
            == target_b->at(0U).signal);
        assert(replay.static_sensitivity.front().offset == 1U);
        assert(replay.static_sensitivity.front().width == 1U);
        assert(replay.driver_regions.size() == 1U);
        assert(replay.driver_regions.front().signal
            == target_b->at(1U).signal);
        assert(replay.driver_regions.front().offset == 0U);
        assert(replay.driver_regions.front().width == 1U);
    }

} // namespace

void test_verilog_concurrent_template_rows()
{
    using namespace fsim::runtime::simir;
    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;

    const auto parsed = fsim::frontend::parse_text(
        "verilog-concurrent-template-rows.v",
        R"(
module concurrent_leaf(input source_value, output result_value);
  assign result_value = source_value;
endmodule

module verilog_template_top;
  reg left_source;
  reg right_source;
  wire left_result;
  wire right_result;
  concurrent_leaf left(.source_value(left_source), .result_value(left_result));
  concurrent_leaf right(.source_value(right_source), .result_value(right_result));
  initial begin
    left_source = 1'b0;
    right_source = 1'b1;
    #1 left_source = 1'b1;
    right_source = 1'b0;
  end
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    if (!parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(parsed.ok());

    std::ostringstream profile_output;
    fsim::elaboration::ElaborationResult elaborated;
    {
        const ProfileCapture profile { profile_output };
        elaborated = compile_and_elaborate(
            parsed.design, "verilog_template_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto profile = profile_output.str();
    const auto marker = profile.find(
        "fsim-profile: systemverilog-concurrent-process-template ");
    assert(marker != std::string::npos);
    const auto line_end = profile.find('\n', marker);
    const auto row = std::string_view { profile }.substr(
        marker,
        line_end == std::string::npos
            ? profile.size() - marker
            : line_end - marker);
    assert(profile_metric(row, "lowered") == 1U);
    assert(profile_metric(row, "replayed") == 1U);
    assert(profile_metric(row, "hits") == 1U);

    const auto left_source = elaborated.design->find_signal(
        "verilog_template_top.left_source");
    const auto right_source = elaborated.design->find_signal(
        "verilog_template_top.right_source");
    const auto left_result = elaborated.design->find_signal(
        "verilog_template_top.left_result");
    const auto right_result = elaborated.design->find_signal(
        "verilog_template_top.right_result");
    assert(left_source && right_source && left_result && right_result);

    const auto table = ProcessAccess::process_table(*elaborated.design);
    assert(ProcessAccess::row_backed(*elaborated.design));
    assert(table && table->rows.size()
        == ProcessAccess::process_count(*elaborated.design));
    const auto find_assignment = [&](const SignalId read_signal,
                                     const SignalId write_signal)
        -> std::optional<std::size_t> {
        for (std::size_t index = 0U;
            index < ProcessAccess::process_count(*elaborated.design);
            ++index) {
            const auto process = ProcessAccess::process_view(
                *elaborated.design, index);
            bool reads { };
            bool writes { };
            for (std::size_t operation_index = 0U;
                operation_index < process.operations().size();
                ++operation_index) {
                const auto operation
                    = process.operations().expanded(operation_index);
                if (const auto* read = operation_get_if<ReadSignal>(
                        &operation)) {
                    reads = reads || read->signal == read_signal;
                }
                if (const auto* write = operation_get_if<WriteUpdate>(
                        &operation)) {
                    writes = writes || write->signal == write_signal;
                }
            }
            if (reads && writes) {
                return index;
            }
        }
        return std::nullopt;
    };
    const auto left_index = find_assignment(*left_source, *left_result);
    const auto right_index = find_assignment(*right_source, *right_result);
    assert(left_index && right_index && *left_index != *right_index);
    assert(table->rows[*left_index].template_id
        == table->rows[*right_index].template_id);

    const auto left_process = ProcessAccess::process_view(
        *elaborated.design, *left_index);
    const auto right_process = ProcessAccess::process_view(
        *elaborated.design, *right_index);
    assert(left_process.id() != right_process.id());
    assert(left_process.common_identity()
        == right_process.common_identity());
    assert(left_process.scheduling_domain()
        == ProcessSchedulingDomain::systemverilog);
    assert(right_process.scheduling_domain()
        == left_process.scheduling_domain());
    assert(left_process.operations().shares_body_with(
        right_process.operations()));

    const auto find_update_domain = [](const auto& process) {
        for (std::size_t index = 0U;
            index < process.operations().size();
            ++index) {
            const auto operation = process.operations().expanded(index);
            if (const auto* write = operation_get_if<WriteUpdate>(
                    &operation)) {
                return write->domain;
            }
        }
        assert(false);
        return SignalUpdateDomain::generic;
    };
    assert(find_update_domain(left_process)
        == SignalUpdateDomain::systemverilog_active);
    assert(find_update_domain(right_process)
        == SignalUpdateDomain::systemverilog_active);

    const auto read_signal = [](const auto& process) {
        for (std::size_t index = 0U;
            index < process.operations().size();
            ++index) {
            const auto operation = process.operations().expanded(index);
            if (const auto* read = operation_get_if<ReadSignal>(
                    &operation)) {
                return read->signal;
            }
        }
        assert(false);
        return SignalId { };
    };
    const auto written_signal = [](const auto& process) {
        for (std::size_t index = 0U;
            index < process.operations().size();
            ++index) {
            const auto operation = process.operations().expanded(index);
            if (const auto* write = operation_get_if<WriteUpdate>(
                    &operation)) {
                return write->signal;
            }
        }
        assert(false);
        return SignalId { };
    };
    assert(read_signal(left_process) == *left_source);
    assert(read_signal(right_process) == *right_source);
    assert(written_signal(left_process) == *left_result);
    assert(written_signal(right_process) == *right_result);

    fsim::diagnostic::Engine artifact_diagnostics;
    const auto artifact_bytes
        = fsim::app::runtime_path_codec::serialize_runtime_design_state(
            *elaborated.design, nullptr, artifact_diagnostics);
    assert(artifact_bytes && !artifact_diagnostics.has_error());
    auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_program_state(
            *artifact_bytes, "verilog-concurrent-template-rows", nullptr,
            artifact_diagnostics);
    assert(decoded && !artifact_diagnostics.has_error());
    assert(decoded->process_rows);
    assert(decoded->process_rows->rows[*left_index].template_id
        == decoded->process_rows->rows[*right_index].template_id);
    auto restored = ProcessAccess::from_state(
        std::move(decoded->state), std::move(decoded->process_rows));
    assert(restored && ProcessAccess::row_backed(*restored));
    fsim::diagnostic::Engine restored_diagnostics;
    const auto restored_bytes
        = fsim::app::runtime_path_codec::serialize_runtime_design_state(
            *restored, nullptr, restored_diagnostics);
    assert(restored_bytes && !restored_diagnostics.has_error());
    assert(*restored_bytes == *artifact_bytes);

    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*left_result).to_msb_string() == "1");
    assert(interpreter->signal_value(*right_result).to_msb_string() == "0");
}

void test_systemverilog_concurrent_template_replay()
{
    test_systemverilog_template_exact_element_bindings();
    const auto parsed = fsim::frontend::parse_text(
        "sv-concurrent-template-replay.sv",
        R"(
module template_leaf #(
  parameter logic [1:0] MASK = 2'b11
) (
  input logic [1:0] value,
  output logic [1:0] transformed
);
  for (genvar index = 0; index < 2; ++index) begin : lanes
    logic lane_value;
    assign lane_value = value[index] ^ MASK[index];
    assign transformed[index] = lane_value;
  end
endmodule

module typed_template_leaf #(parameter type T = logic [1:0]) (
  input T value,
  output T transformed
);
  assign transformed = value;
endmodule

module template_top;
  logic [1:0] left_value;
  logic [1:0] right_value;
  logic [1:0] other_left_value;
  logic [1:0] other_right_value;
  logic [1:0] left_result;
  logic [1:0] right_result;
  logic [1:0] other_left_result;
  logic [1:0] other_right_result;
  logic [1:0] narrow_value;
  logic [1:0] narrow_result;
  logic [1:0] narrow_peer_value;
  logic [1:0] narrow_peer_result;
  logic [3:0] wide_value;
  logic [3:0] wide_result;
  template_leaf #(.MASK(2'b11)) left (
    .value(left_value), .transformed(left_result));
  template_leaf #(.MASK(2'b11)) right (
    .value(right_value), .transformed(right_result));
  template_leaf #(.MASK(2'b10)) other_left (
    .value(other_left_value), .transformed(other_left_result));
  template_leaf #(.MASK(2'b10)) other_right (
    .value(other_right_value), .transformed(other_right_result));
  typed_template_leaf #(.T(logic [1:0])) narrow (
    .value(narrow_value), .transformed(narrow_result));
  typed_template_leaf #(.T(logic [1:0])) narrow_peer (
    .value(narrow_peer_value), .transformed(narrow_peer_result));
  typed_template_leaf #(.T(logic [3:0])) wide (
    .value(wide_value), .transformed(wide_result));
  initial begin
    left_value = 2'b10;
    right_value = 2'b01;
    other_left_value = 2'b10;
    other_right_value = 2'b01;
    narrow_value = 2'b10;
    narrow_peer_value = 2'b01;
    wide_value = 4'b1010;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    if (!parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(parsed.ok());

    std::ostringstream profile_output;
    fsim::elaboration::ElaborationResult elaborated;
    {
        const ProfileCapture profile { profile_output };
        elaborated = compile_and_elaborate(
            parsed.design, "sv:work.template_top");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto output = profile_output.str();
    const auto row_start = output.find(
        "fsim-profile: systemverilog-concurrent-process-template ");
    assert(row_start != std::string::npos);
    const auto row_end = output.find('\n', row_start);
    const auto row = std::string_view { output }.substr(
        row_start,
        row_end == std::string::npos
            ? output.size() - row_start
            : row_end - row_start);
    const auto lowered_templates = profile_metric(row, "lowered");
    const auto replayed_templates = profile_metric(row, "replayed");
    if (lowered_templates != 10U || replayed_templates != 9U) {
        std::cerr << "unexpected SystemVerilog template profile counts: "
                  << row << '\n';
    }
    assert(lowered_templates == 10U);
    assert(replayed_templates == 9U);
    assert(profile_metric(row, "hits") == 9U);

    const auto left_value = elaborated.design->find_signal(
        "template_top.left_value");
    const auto right_value = elaborated.design->find_signal(
        "template_top.right_value");
    const auto other_left_value = elaborated.design->find_signal(
        "template_top.other_left_value");
    const auto other_right_value = elaborated.design->find_signal(
        "template_top.other_right_value");
    const auto left_result = elaborated.design->find_signal(
        "template_top.left_result");
    const auto right_result = elaborated.design->find_signal(
        "template_top.right_result");
    const auto other_left_result = elaborated.design->find_signal(
        "template_top.other_left_result");
    const auto other_right_result = elaborated.design->find_signal(
        "template_top.other_right_result");
    const auto narrow_result = elaborated.design->find_signal(
        "template_top.narrow_result");
    const auto narrow_peer_result = elaborated.design->find_signal(
        "template_top.narrow_peer_result");
    const auto wide_result = elaborated.design->find_signal(
        "template_top.wide_result");
    const auto left_local = elaborated.design->find_signal(
        "template_top.left.lanes[0].lane_value");
    const auto left_local_next = elaborated.design->find_signal(
        "template_top.left.lanes[1].lane_value");
    const auto right_local = elaborated.design->find_signal(
        "template_top.right.lanes[0].lane_value");
    const auto right_local_next = elaborated.design->find_signal(
        "template_top.right.lanes[1].lane_value");
    const auto other_left_local = elaborated.design->find_signal(
        "template_top.other_left.lanes[0].lane_value");
    const auto other_left_local_next = elaborated.design->find_signal(
        "template_top.other_left.lanes[1].lane_value");
    const auto other_right_local = elaborated.design->find_signal(
        "template_top.other_right.lanes[0].lane_value");
    const auto other_right_local_next = elaborated.design->find_signal(
        "template_top.other_right.lanes[1].lane_value");
    assert(left_value && right_value && other_left_value
        && other_right_value && left_result && right_result
        && other_left_result && other_right_result && left_local
        && left_local_next && right_local && right_local_next
        && other_left_local && other_left_local_next && other_right_local
        && other_right_local_next && narrow_result && wide_result);
    assert(narrow_peer_result);
    assert(*left_value != *right_value && *left_result != *right_result);
    assert(*left_local != *right_local
        && *other_left_local != *other_right_local);

    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;
    const auto process_table = ProcessAccess::process_table(
        *elaborated.design);
    assert(ProcessAccess::row_backed(*elaborated.design));
    assert(process_table != nullptr);
    assert(process_table->rows.size()
        == elaborated.design->process_count());
    const auto find_assignment_row = [&](const auto read_signal,
                                         const auto write_signal)
        -> std::optional<std::size_t> {
        for (std::size_t index = 0U;
            index < ProcessAccess::process_count(*elaborated.design);
            ++index) {
            const auto process = ProcessAccess::process_view(
                *elaborated.design, index);
            bool reads { };
            bool writes { };
            for (std::size_t operation_index = 0U;
                operation_index < process.operations().size();
                ++operation_index) {
                const auto operation
                    = process.operations().expanded(operation_index);
                if (const auto* read
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::ReadSignal>(&operation)) {
                    reads = reads || read->signal == read_signal;
                }
                if (const auto* write
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::WriteUpdate>(&operation)) {
                    writes = writes || write->signal == write_signal;
                }
                if (const auto* write
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::WriteUpdateSlice>(
                            &operation)) {
                    writes = writes || write->signal == write_signal;
                }
            }
            if (reads && writes) {
                return index;
            }
        }
        return std::nullopt;
    };
    const auto assert_shared_assignment_rows = [&](const auto left_input,
                                                   const auto right_input,
                                                   const auto left_local,
                                                   const auto right_local) {
        const auto left_index = find_assignment_row(
            left_input, left_local);
        const auto right_index = find_assignment_row(
            right_input, right_local);
        assert(left_index && right_index);
        assert(process_table->rows[*left_index].template_id
            == process_table->rows[*right_index].template_id);
        const auto left_view = ProcessAccess::process_view(
            *elaborated.design, *left_index);
        const auto right_view = ProcessAccess::process_view(
            *elaborated.design, *right_index);
        assert(left_view.id() != right_view.id());
        assert(left_view.common_identity() == right_view.common_identity());
        assert(left_view.scheduling_domain()
            == fsim::runtime::simir::ProcessSchedulingDomain::systemverilog);
        assert(right_view.scheduling_domain()
            == left_view.scheduling_domain());
        assert(left_view.operations().shares_body_with(
            right_view.operations()));
        assert(std::ranges::any_of(
            left_view.static_sensitivity(),
            [&](const auto& sensitivity) {
                return sensitivity.signal == left_input;
            }));
        assert(std::ranges::any_of(
            right_view.static_sensitivity(),
            [&](const auto& sensitivity) {
                return sensitivity.signal == right_input;
            }));
        assert(left_view.static_sensitivity().front().signal
            != right_view.static_sensitivity().front().signal);
        assert(std::ranges::any_of(
            left_view.driver_regions(),
            [&](const auto& driver) {
                return driver.signal == left_local;
            }));
        assert(std::ranges::any_of(
            right_view.driver_regions(),
            [&](const auto& driver) {
                return driver.signal == right_local;
            }));
        assert(left_view.driver_regions().front().signal
            != right_view.driver_regions().front().signal);
    };
    assert_shared_assignment_rows(
        *left_value, *right_value, *left_local, *right_local);
    assert_shared_assignment_rows(
        *other_left_value, *other_right_value,
        *other_left_local, *other_right_local);
    const auto narrow_value = elaborated.design->find_signal(
        "template_top.narrow_value");
    const auto narrow_peer_value = elaborated.design->find_signal(
        "template_top.narrow_peer_value");
    assert(narrow_value && narrow_peer_value);
    assert_shared_assignment_rows(
        *narrow_value, *narrow_peer_value,
        *narrow_result, *narrow_peer_result);

    fsim::diagnostic::Engine artifact_diagnostics;
    const auto artifact_bytes
        = fsim::app::runtime_path_codec::serialize_runtime_design_state(
            *elaborated.design, nullptr, artifact_diagnostics);
    assert(artifact_bytes && !artifact_diagnostics.has_error());
    assert(ProcessAccess::row_backed(*elaborated.design));
    assert(ProcessAccess::process_table(*elaborated.design) == process_table);
    auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_program_state(
            *artifact_bytes, "sv-concurrent-template-rows", nullptr,
            artifact_diagnostics);
    assert(decoded && !artifact_diagnostics.has_error());
    assert(decoded->process_rows != nullptr);
    assert(decoded->process_rows->rows.size()
        == ProcessAccess::process_count(*elaborated.design));
    const auto artifact_left_row = find_assignment_row(
        *left_value, *left_local);
    const auto artifact_right_row = find_assignment_row(
        *right_value, *right_local);
    assert(artifact_left_row && artifact_right_row);
    assert(decoded->process_rows->rows[*artifact_left_row].template_id
        == decoded->process_rows->rows[*artifact_right_row].template_id);
    auto restored = ProcessAccess::from_state(
        std::move(decoded->state), std::move(decoded->process_rows));
    assert(restored && ProcessAccess::row_backed(*restored));
    fsim::diagnostic::Engine restored_diagnostics;
    const auto restored_bytes
        = fsim::app::runtime_path_codec::serialize_runtime_design_state(
            *restored, nullptr, restored_diagnostics);
    assert(restored_bytes && !restored_diagnostics.has_error());
    assert(*restored_bytes == *artifact_bytes);

    const auto find_assignment_binding = [&](const auto read_signal,
                                             const auto write_signal) {
        for (const auto& process : elaborated.design->processes()) {
            bool reads { };
            bool writes { };
            for (std::size_t index = 0;
                 index < process.operations.size(); ++index) {
                const auto operation = process.operations.expanded(index);
                if (const auto* read
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::ReadSignal>(&operation)) {
                    reads = reads || read->signal == read_signal;
                }
                if (const auto* write
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::WriteUpdate>(&operation)) {
                    writes = writes || write->signal == write_signal;
                }
                if (const auto* write
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::WriteUpdateSlice>(
                        &operation)) {
                    writes = writes || write->signal == write_signal;
                }
            }
            if (reads && writes) {
                return &process;
            }
        }
        return static_cast<const fsim::runtime::simir::Process*>(nullptr);
    };
    constexpr auto systemverilog_domain
        = fsim::runtime::simir::ProcessSchedulingDomain::systemverilog;
    constexpr auto first_genvar_offset = std::uint32_t { 0U };
    constexpr auto next_genvar_offset = std::uint32_t { 1U };
    const auto assert_instance_bindings = [&](const auto input,
                                              const auto local_zero,
                                              const auto local_one,
                                              const auto output) {
        const std::array locals { local_zero, local_one };
        for (std::size_t index = 0U; index < locals.size(); ++index) {
            const auto expected_offset = index == 0U
                ? first_genvar_offset : next_genvar_offset;
            const auto input_process
                = find_assignment_binding(input, locals[index]);
            assert(input_process != nullptr);
            assert(input_process->scheduling_domain == systemverilog_domain);
            assert(std::ranges::any_of(
                input_process->static_sensitivity,
                [&](const auto& sensitivity) {
                    return sensitivity.signal == input
                        && sensitivity.offset == expected_offset
                        && sensitivity.width == 1U;
                }));

            const auto output_process
                = find_assignment_binding(locals[index], output);
            assert(output_process != nullptr);
            assert(output_process->scheduling_domain == systemverilog_domain);
            assert(std::ranges::any_of(
                output_process->driver_regions,
                [&](const auto& driver) {
                    return driver.signal == output && !driver.whole
                        && driver.offset == expected_offset
                        && driver.width == 1U;
                }));
        }
    };
    assert_instance_bindings(
        *left_value, *left_local, *left_local_next, *left_result);
    assert_instance_bindings(
        *right_value, *right_local, *right_local_next, *right_result);
    assert_instance_bindings(
        *other_left_value, *other_left_local, *other_left_local_next,
        *other_left_result);
    assert_instance_bindings(
        *other_right_value, *other_right_local, *other_right_local_next,
        *other_right_result);

    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*left_result).to_msb_string() == "01");
    assert(interpreter->signal_value(*right_result).to_msb_string() == "10");
    assert(interpreter->signal_value(*other_left_result).to_msb_string()
        == "00");
    assert(interpreter->signal_value(*other_right_result).to_msb_string()
        == "11");
    assert(interpreter->signal_value(*narrow_result).to_msb_string()
        == "10");
    assert(interpreter->signal_value(*narrow_peer_result).to_msb_string()
        == "01");
    assert(interpreter->signal_value(*wide_result).to_msb_string()
        == "1010");
    test_verilog_concurrent_template_rows();
}

void test_systemverilog_hierarchy_configuration()
{
    fsim::frontend::ParsedDesign design;
    append_units(
        design,
        fsim::frontend::parse_text(
            "hierarchy-work.sv",
            R"(
extern module monitor #(
  parameter logic [136:0] MAGIC = 137'h1
) (
  input logic [136:0] value
);

module monitor #(
  parameter logic [136:0] MAGIC = 137'h1
) (
  input logic [136:0] value
);
  logic [136:0] observed;
  assign observed = value ^ MAGIC;
endmodule

module configured_top;
  logic [136:0] left_value;
  logic [136:0] right_value;
  leaf left(.value(left_value));
  leaf right(.value(right_value));
  defparam right.P = 137'h1_0000_0000_0000_0000_0000_0000_0000_0001;

  for (genvar index = 0; index < 2; ++index) begin : lanes
    leaf generated();
  end

  bind leaf monitor #(
    .MAGIC(137'h1_0000_0000_0000_0000_0000_0000_0000_0002)
  ) all_leaf_monitor(.value(value));
  bind configured_top.lanes[1].generated monitor #(
    .MAGIC(137'h1_0000_0000_0000_0000_0000_0000_0000_0003)
  ) selected_monitor(.value(value));
endmodule

config configured;
  design work.configured_top;
  instance configured_top.left use fast.leaf;
  instance configured_top.lanes[1].generated use fast.leaf;
  cell leaf liblist slow;
endconfig : configured
)",
            fsim::frontend::Language::SystemVerilog2017),
        "work");
    append_units(
        design,
        fsim::frontend::parse_text(
            "hierarchy-fast.sv",
            R"(
module leaf #(
  parameter logic [136:0] P = 137'h11
) (
  output logic [136:0] value
);
  assign value = P;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017),
        "fast");
    append_units(
        design,
        fsim::frontend::parse_text(
            "hierarchy-slow.sv",
            R"(
module leaf #(
  parameter logic [136:0] P = 137'h22
) (
  output logic [136:0] value
);
  assign value = P;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017),
        "slow");

    const auto elaborated = compile_and_elaborate(
        design, "sv:work.configured");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design);

    const auto* top = find_specialization(*elaborated.design, "configured");
    const auto* left = find_specialization(
        *elaborated.design, "configured.left");
    const auto* right = find_specialization(
        *elaborated.design, "configured.right");
    const auto* lane0 = find_specialization(
        *elaborated.design, "configured.lanes[0].generated");
    const auto* lane1 = find_specialization(
        *elaborated.design, "configured.lanes[1].generated");
    assert(top && left && right && lane0 && lane1);
    assert(std::ranges::any_of(
        top->parameter_identity_values,
        [](const auto& value) {
            return value.first == "__configuration"
                && value.second.starts_with("sv-config-v1;");
        }));
    assert(left->library == "fast");
    assert(right->library == "slow");
    assert(lane0->library == "slow");
    assert(lane1->library == "fast");
    assert(!left->parameter_identity_values.empty());
    assert(!right->parameter_identity_values.empty());
    const auto parameter_identity = [](const auto& specialization,
                                        const std::string_view name) {
        const auto found = std::ranges::find_if(
            specialization.parameter_identity_values,
            [&](const auto& value) { return value.first == name; });
        assert(found != specialization.parameter_identity_values.end());
        return found->second;
    };
    assert(
        parameter_identity(*left, "P")
        != parameter_identity(*right, "P"));
    assert(std::ranges::any_of(
        left->parameter_identity_values,
        [](const auto& value) {
            return value.first == "__configuration"
                && value.second.starts_with("sv-config-v1;");
        }));
    assert(std::ranges::any_of(
        right->parameter_values,
        [](const auto& value) {
            return value.first == "P"
                && value.second.find('1') != std::string::npos;
        }));

    for (const auto path : {
             "configured.left.all_leaf_monitor",
             "configured.right.all_leaf_monitor",
             "configured.lanes[0].generated.all_leaf_monitor",
             "configured.lanes[1].generated.all_leaf_monitor",
             "configured.lanes[1].generated.selected_monitor" }) {
        const auto* bound = find_specialization(*elaborated.design, path);
        assert(bound);
        assert(bound->library == "work");
        assert(!bound->parameter_values.empty());
    }
    assert(
        find_specialization(
            *elaborated.design,
            "configured.lanes[0].generated.selected_monitor")
        == nullptr);

    const auto mismatch = fsim::frontend::parse_text(
        "extern-mismatch.sv",
        R"(
extern module mismatch(input logic [136:0] value);
module mismatch(input logic [135:0] value); endmodule
module mismatch_top; mismatch child(); endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatch.ok());
    const auto mismatch_result = compile_and_elaborate(
        mismatch.design, "sv:work.mismatch_top");
    assert(!mismatch_result.ok());
    assert(has_diagnostic(mismatch_result, "FSIM-ELAB-SVEXTERN-002"));

    const auto missing = fsim::frontend::parse_text(
        "extern-missing.sv",
        R"(
extern module missing(input logic [136:0] value);
module missing_top; endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(missing.ok());
    const auto missing_result = compile_and_elaborate(
        missing.design, "sv:work.missing_top");
    assert(!missing_result.ok());
    assert(has_diagnostic(missing_result, "FSIM-ELAB-SVEXTERN-001"));

    const auto implicit_ports = fsim::frontend::parse_text(
        "implicit-ports.sv",
        R"(
module implicit_leaf(
  input logic clk,
  input logic value,
  output logic result);
  assign result = value;
endmodule
module implicit_top;
  logic clk;
  logic value;
  logic result;
  implicit_leaf child(.clk, .*);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(implicit_ports.ok());
    const auto implicit_result = compile_and_elaborate(
        implicit_ports.design, "sv:work.implicit_top");
    for (const auto& diagnostic : implicit_result.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
    }
    assert(implicit_result.ok());
    for (const auto name : { "clk", "value", "result" }) {
        const auto parent = implicit_result.design->find_signal(name);
        const auto child = implicit_result.design->find_signal(
            "implicit_top.child." + std::string { name });
        assert(parent && child && *parent == *child);
    }
}

} // namespace fsim::tests::elaboration
