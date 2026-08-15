// SPDX-License-Identifier: Apache-2.0
#include "application_trace_hierarchy.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/systemc/kernel_backend_binding_inventory.hpp"
#include "fsim/systemc/kernel_backend_inventory.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using fsim::semantic::design::InstanceOccurrenceId;
using fsim::semantic::design::ObjectId;
using fsim::semantic::design::SpecializationId;

struct Fixture {
    fsim::semantic::design::DesignIr design;
    std::array<ObjectId, 5> objects;
};

[[nodiscard]] Fixture make_fixture()
{
    using namespace fsim;
    using namespace semantic::design;
    struct Root {
        std::string alias;
        std::string library;
        std::string unit;
        semantic::Language language;
    };
    const std::array roots {
        Root { "verilog_root", "vlib", "producer",
            semantic::Language::verilog },
        Root { "systemverilog_root", "svlib", "monitor",
            semantic::Language::system_verilog },
        Root { "vhdl_root", "work", "consumer(rtl)",
            semantic::Language::vhdl },
        Root { "systemc_root", "systemc", "bridge",
            semantic::Language::systemc },
    };

    Fixture result;
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const auto specialization
            = SpecializationId::from_index(static_cast<std::uint32_t>(index));
        const auto instance
            = InstanceOccurrenceId::from_index(static_cast<std::uint32_t>(index));
        Specialization specialization_record;
        specialization_record.id = specialization;
        specialization_record.instance = instance;
        specialization_record.language = roots[index].language;
        specialization_record.library = roots[index].library;
        specialization_record.name = roots[index].unit;
        InstanceOccurrence occurrence;
        occurrence.id = instance;
        occurrence.specialization = specialization;
        occurrence.name = roots[index].alias;
        occurrence.path = roots[index].alias;
        occurrence.target = roots[index].unit;
        result.design.mutable_roots().push_back(roots[index].alias);
        result.design.mutable_specializations().push_back(
            std::move(specialization_record));
        result.design.mutable_instances().push_back(std::move(occurrence));

        Object object;
        object.id = ObjectId::from_index(static_cast<std::uint32_t>(index));
        object.specialization = specialization;
        object.kind = ObjectKind::signal;
        object.name = "value";
        object.path = "value";
        object.width = 1U;
        object.runtime_index = index;
        result.objects[index] = object.id;
        result.design.mutable_objects().push_back(std::move(object));
    }

    Object alias;
    alias.id = ObjectId::from_index(4U);
    alias.specialization = SpecializationId::from_index(2U);
    alias.kind = ObjectKind::signal;
    alias.name = "verilog_value";
    alias.path = "vhdl_root.verilog_value";
    alias.width = 1U;
    alias.runtime_index = 0U;
    result.objects[4] = alias.id;
    result.design.mutable_objects().push_back(std::move(alias));
    return result;
}

template <typename Function>
void expect_invalid(Function&& function)
{
    bool rejected = false;
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_canonical_mixed_root_hierarchy()
{
    using namespace fsim;
    using app::application_detail::canonical_fst_trace_object;
    const auto fixture = make_fixture();
    const auto& objects = fixture.design.objects();
    const std::array expected_paths {
        std::string { "verilog_root.value" },
        std::string { "systemverilog_root.value" },
        std::string { "vhdl_root.value" },
        std::string { "systemc_root.value" },
        std::string { "vhdl_root.verilog_value" },
    };
    const std::array expected_languages {
        runtime::TraceLanguage::Verilog,
        runtime::TraceLanguage::SystemVerilog,
        runtime::TraceLanguage::Vhdl,
        runtime::TraceLanguage::SystemC,
        runtime::TraceLanguage::Vhdl,
    };
    std::array<app::application_detail::FstTraceObject, 5> canonical;
    for (std::size_t index = 0; index < canonical.size(); ++index) {
        canonical[index] = canonical_fst_trace_object(
            fixture.design, objects[fixture.objects[index].value()]);
        assert(canonical[index].path == expected_paths[index]);
        assert(canonical[index].source.language == expected_languages[index]);
        assert(canonical[index].source.root_identity
            == fixture.design.roots()[index == 4U ? 2U : index]);
    }
    assert(canonical[0].source.library == "vlib");
    assert(canonical[1].source.library == "svlib");
    assert(canonical[2].source.library == "work");
    assert(canonical[3].source.library == "systemc");
    assert(canonical[3].source.kind == runtime::TraceSourceKind::SystemC);
    assert(canonical[4].source.owner_identity
        == "work:consumer(rtl)@vhdl_root");

    runtime::TraceDeclarationBuilder builder;
    const auto primary = builder.add_typed_variable(
        canonical[0].path, runtime::TraceTypeKind::Packed, 1U,
        runtime::SystemVerilogScalarKind::None, { }, canonical[0].source);
    static_cast<void>(builder.add_typed_variable(
        canonical[1].path, runtime::TraceTypeKind::Packed, 1U,
        runtime::SystemVerilogScalarKind::None, { }, canonical[1].source));
    static_cast<void>(builder.add_typed_variable(
        canonical[2].path, runtime::TraceTypeKind::Packed, 1U,
        runtime::SystemVerilogScalarKind::None, { }, canonical[2].source));
    static_cast<void>(builder.add_typed_variable(
        canonical[3].path, runtime::TraceTypeKind::Packed, 1U,
        runtime::SystemVerilogScalarKind::None, { }, canonical[3].source));
    const auto alias = builder.add_alias(
        canonical[4].path, primary, canonical[4].source);
    const auto model = std::move(builder).freeze();
    assert(model.scopes().size() == 4U);
    assert(model.alias(alias).target == primary);
    assert(model.source(model.alias(alias).source).language
        == runtime::TraceLanguage::Vhdl);

    std::ostringstream output;
    runtime::FstWriter writer { output };
    writer.declare(model);
    writer.begin();
    writer.close(0U);
    assert(!output.str().empty());
}

void test_invalid_hierarchy_provenance()
{
    using namespace fsim;
    using app::application_detail::canonical_fst_trace_object;
    expect_invalid([] {
        auto fixture = make_fixture();
        fixture.design.mutable_roots().erase(
            fixture.design.mutable_roots().begin());
        static_cast<void>(canonical_fst_trace_object(
            fixture.design, fixture.design.objects().front()));
    });
    expect_invalid([] {
        auto fixture = make_fixture();
        fixture.design.mutable_specializations().front().library.clear();
        static_cast<void>(canonical_fst_trace_object(
            fixture.design, fixture.design.objects().front()));
    });
    expect_invalid([] {
        auto fixture = make_fixture();
        fixture.design.mutable_instances().front().parent
            = InstanceOccurrenceId::from_index(0U);
        static_cast<void>(canonical_fst_trace_object(
            fixture.design, fixture.design.objects().front()));
    });
    expect_invalid([] {
        auto fixture = make_fixture();
        fixture.design.mutable_objects().front().path.clear();
        static_cast<void>(canonical_fst_trace_object(
            fixture.design, fixture.design.objects().front()));
    });
}

void test_systemc_channel_inventory_path()
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const systemc::SystemCKernelProtocolLimits limits;
    const auto island = systemc::make_systemc_island_id(
        "trace-hierarchy-island", limits, diagnostics);
    const auto hierarchy = systemc::make_systemc_hierarchy_id(
        *island, "systemc_root", limits, diagnostics);
    assert(island && hierarchy);
    systemc::SystemCKernelChannelInventory inventory { *island, *hierarchy };
    assert(inventory.register_channel(
        { "systemc_root.value", "sc_signal:bool:1",
            systemc::SystemCKernelChannelKind::signal,
            systemc::SystemCKernelChannelValueProfile {
                systemc::SystemCKernelValueKind::bit2, 1U, false },
            systemc::SystemCKernelWriterPolicy::one,
            systemc::SystemCKernelUpdateOwner::signal_kernel,
            systemc::SystemCKernelObservationMode::value_changed, true },
        diagnostics));
    assert(inventory.freeze(diagnostics));
    const auto fixture = make_fixture();
    const auto canonical = app::application_detail::canonical_fst_trace_object(
        fixture.design,
        fixture.design.objects()[fixture.objects[3].value()]);
    assert(inventory.snapshot().channels.front().descriptor.canonical_path
        == canonical.path);
    const auto foreign_object = systemc::make_systemc_object_id(
        *hierarchy, "verilog_root.value", limits, diagnostics);
    const auto foreign_endpoint = systemc::make_systemc_endpoint_id(
        *foreign_object, "signal", limits, diagnostics);
    assert(foreign_object && foreign_endpoint);
    systemc::SystemCKernelBindingTarget target;
    target.chain = { "vhdl_root.verilog_value", "systemc_root.value" };
    target.final_channel_path = "systemc_root.value";
    target.foreign_language = systemc::SystemCKernelHostLanguage::verilog;
    target.foreign_endpoint = *foreign_endpoint;
    systemc::SystemCKernelBindingInventory bindings { inventory.snapshot() };
    assert(bindings.register_binding(
        { "vhdl_root.verilog_value", "mixed_signal_alias",
            systemc::SystemCKernelBindingKind::port,
            systemc::SystemCKernelBindingDirection::input,
            { std::move(target) } },
        diagnostics));
    assert(bindings.freeze(diagnostics));
    assert(bindings.snapshot().bindings.front().descriptor.declared_path
        == fixture.design.objects()[fixture.objects[4].value()].path);
    assert(bindings.snapshot().bindings.front()
            .descriptor.targets.front().final_channel
        == inventory.snapshot().channels.front().channel);
    assert(!diagnostics.has_error());
}

} // namespace

int main()
{
    test_canonical_mixed_root_hierarchy();
    test_invalid_hierarchy_provenance();
    test_systemc_channel_inventory_path();
}
