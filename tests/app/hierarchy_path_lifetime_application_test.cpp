// SPDX-License-Identifier: Apache-2.0
#include "application_trace_observation.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/semantic/design_ir.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace {

using fsim::semantic::HierarchyPathId;
using fsim::semantic::HierarchyPathTable;
using fsim::semantic::design::DesignIr;

[[nodiscard]] DesignIr make_design()
{
    HierarchyPathTable::Builder paths;
    const auto top = paths.intern("top");
    static_cast<void>(paths.intern("top.signal"));

    DesignIr design;
    design.mutable_top() = top;
    design.mutable_roots().push_back(top);
    const bool rebound = design.rebind_path_table(std::move(paths).freeze());
    assert(rebound);
    static_cast<void>(rebound);
    assert(design.valid());
    return design;
}

void test_design_ir_owner_after_artifact_decode_copy_and_move()
{
    using fsim::app::deserialize_design_ir_state;
    using fsim::app::serialize_design_ir_state;

    std::optional<DesignIr> moved_design;
    HierarchyPathId retained_path;
    {
        auto source = make_design();
        fsim::diagnostic::Engine diagnostics;
        auto encoded = serialize_design_ir_state(source, diagnostics);
        assert(encoded && !diagnostics.has_error());

        auto decoded = deserialize_design_ir_state(
            *encoded, "hierarchy-path-lifetime", diagnostics);
        assert(decoded && !diagnostics.has_error());
        const auto path = decoded->hierarchy_paths().find("top.signal");
        assert(path);
        retained_path = *path;

        auto copied = *decoded;
        moved_design.emplace(std::move(copied));
        decoded.reset();
    }

    assert(moved_design);
    assert(moved_design->path(retained_path) == "top.signal");
    auto retained_owner = moved_design->hierarchy_paths();
    moved_design.reset();
    assert(retained_owner.view(retained_path) == "top.signal");
}

struct CapturedDeclaration {
    fsim::runtime::TraceVariableDeclaration variable;
};

void test_trace_observer_removal_releases_owned_path_copy()
{
    using namespace fsim;
    using app::application_detail::TraceObservationKind;
    using app::application_detail::TraceObservationRecorder;
    using app::application_detail::TraceObservationValue;
    using runtime::TraceDeclarationBuilder;
    using runtime::TraceDeclarationModel;
    using runtime::TraceSignalId;

    std::optional<TraceDeclarationModel> retained_model;
    runtime::TraceVariableDeclaration detached;
    TraceSignalId signal;
    {
        HierarchyPathTable::Builder paths;
        static_cast<void>(paths.intern("top"));
        static_cast<void>(paths.intern("top.signal"));
        TraceDeclarationBuilder builder {
            std::move(paths).freeze() };
        signal = builder.add_variable("top.signal", 1U);
        auto model = std::move(builder).freeze();
        auto copied_model = model;
        retained_model.emplace(std::move(copied_model));
        detached = model.variable(signal);
    }

    assert(detached.hierarchical_name.view() == "top.signal");
    assert(retained_model);
    assert(retained_model->variable(signal).hierarchical_name.view()
        == "top.signal");

    TraceObservationRecorder recorder { *retained_model };
    auto captured = std::make_shared<CapturedDeclaration>(
        CapturedDeclaration { retained_model->variable(signal) });
    std::weak_ptr<CapturedDeclaration> captured_lifetime = captured;
    std::size_t callback_count = 0U;
    TraceObservationRecorder::Observer callback =
        [declaration = captured, &callback_count](const auto& record) {
            assert(declaration->variable.hierarchical_name.view()
                == "top.signal");
            assert(record.values.size() == 1U);
            ++callback_count;
        };
    const auto observer = recorder.add_observer(std::move(callback));
    callback = { };
    captured.reset();
    assert(!captured_lifetime.expired());

    const std::array values { TraceObservationValue { signal,
        runtime::PackedLogic4::from_msb_string("1"), std::nullopt } };
    const auto first_sequence = recorder.accept(TraceObservationKind::Signal,
        1U, 0U, runtime::TraceRegion::Active, "signal:top.signal", values);
    assert(first_sequence == 1U);
    static_cast<void>(first_sequence);
    assert(callback_count == 1U);

    recorder.remove_observer(observer);
    assert(captured_lifetime.expired());
    const auto second_sequence = recorder.accept(TraceObservationKind::Signal,
        2U, 0U, runtime::TraceRegion::Active, "signal:top.signal", values);
    assert(second_sequence == 2U);
    static_cast<void>(second_sequence);
    assert(callback_count == 1U);
    assert(detached.hierarchical_name.view() == "top.signal");
}

} // namespace

int main()
{
    test_design_ir_owner_after_artifact_decode_copy_and_move();
    test_trace_observer_removal_releases_owned_path_copy();
}
