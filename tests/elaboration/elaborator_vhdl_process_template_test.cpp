// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/support/environment.hpp"
#include "../../src/app/application_runtime_path_codec.hpp"
#include "../../src/elaboration/elaborated_design_process_access.hpp"
#include "../../src/runtime/simir_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime::simir {

struct OwnedDriverDemotionTestAccess {
    static bool startup_banked(
        const Interpreter& interpreter, const ProcessId process)
    {
        return interpreter.impl_->processes.is_compact_constant(process);
    }

    static bool process_frame_materialized(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const state
            = interpreter.impl_->processes.full_state_if_present(process);
        return state != nullptr && static_cast<bool>(state->frame);
    }
};

} // namespace fsim::runtime::simir

namespace fsim::tests::elaboration {
namespace {

class VhdlTemplateProfile final {
public:
    explicit VhdlTemplateProfile(std::ostringstream& output)
        : previous_ { fsim::support::environment_variable("FSIM_PROFILE_PHASES") }
    {
        if (set("1") != 0) {
            throw std::runtime_error { "could not configure VHDL template census" };
        }
        stream_ = std::cerr.rdbuf(output.rdbuf());
    }

    ~VhdlTemplateProfile()
    {
        std::cerr.rdbuf(stream_);
        const auto result = set(previous_ ? previous_->c_str() : nullptr);
        (void)result;
        assert(result == 0);
    }

private:
    static int set(const char* value)
    {
#if defined(_WIN32)
        const auto result = ::_putenv_s("FSIM_PROFILE_PHASES", value ? value : "");
#else
        const auto result = value ? ::setenv("FSIM_PROFILE_PHASES", value, 1)
                                  : ::unsetenv("FSIM_PROFILE_PHASES");
#endif
        return result;
    }

    std::optional<std::string> previous_;
    std::streambuf* stream_ { };
};

std::uint64_t metric(const std::string_view row, const std::string_view name)
{
    const auto marker = std::string { name } + "=";
    const auto position = row.find(marker);
    assert(position != std::string_view::npos);
    return std::stoull(std::string { row.substr(position + marker.size()) });
}

struct ProjectedConstantTrace {
    struct Event {
        fsim::runtime::simir::SignalId signal { };
        fsim::runtime::SchedulerTraceKind kind { };
        fsim::runtime::SimulationTick time { };
    };

    std::array<Event, 8U> events { };
    std::size_t count { };
    bool overflow { };

    static void receive(
        void* const context,
        const fsim::runtime::SchedulerTraceRecord& record) noexcept
    {
        auto& trace = *static_cast<ProjectedConstantTrace*>(context);
        if (record.kind
                != fsim::runtime::SchedulerTraceKind::signal_transaction
            && record.kind
                != fsim::runtime::SchedulerTraceKind::signal_change) {
            return;
        }
        if (trace.count == trace.events.size()) {
            trace.overflow = true;
            return;
        }
        trace.events[trace.count++] = {
            record.signal, record.kind, record.time
        };
    }

    [[nodiscard]] std::size_t count_for(
        const fsim::runtime::simir::SignalId signal) const noexcept
    {
        std::size_t result { };
        for (std::size_t index = 0U; index < count; ++index) {
            result += events[index].signal == signal ? 1U : 0U;
        }
        return result;
    }

    [[nodiscard]] const Event& event_for(
        const fsim::runtime::simir::SignalId signal,
        const std::size_t ordinal) const
    {
        std::size_t seen { };
        for (std::size_t index = 0U; index < count; ++index) {
            if (events[index].signal != signal) {
                continue;
            }
            if (seen++ == ordinal) {
                return events[index];
            }
        }
        assert(false && "missing projected constant scheduler event");
        return events[0U];
    }
};

} // namespace

void test_vhdl_ordinary_process_template_replay()
{
    using namespace fsim::runtime::simir;
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-process-template.vhd", R"(
entity template_leaf is
  port (source : in std_logic_vector(3 downto 0);
        destination : out std_logic_vector(3 downto 0));
end entity;
architecture rtl of template_leaf is
begin
  transfer_value : process(source)
  begin
    destination <= source;
    destination(0) <= source(0);
  end process;
end architecture;
entity template_top is
end entity;
architecture rtl of template_top is
  signal left_input, right_input : std_logic_vector(3 downto 0);
  signal left_output, right_output : std_logic_vector(3 downto 0);
  signal aliased : std_logic_vector(3 downto 0);
begin
  left_lane : entity work.template_leaf(rtl)
    port map (left_input, left_output);
  right_lane : entity work.template_leaf(rtl)
    port map (right_input, right_output);
  alias_lane : entity work.template_leaf(rtl)
    port map (aliased, aliased);
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    fsim::elaboration::ElaborationResult elaborated;
    std::ostringstream census;
    {
        const VhdlTemplateProfile capture { census };
        elaborated = compile_and_elaborate(
            parsed.design, "vhdl:work.template_top(rtl)");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    const auto output = census.str();
    const auto start = output.find("fsim-profile: vhdl-process-template ");
    assert(start != std::string::npos);
    const auto row = std::string_view { output }.substr(
        start, output.find('\n', start) - start);
    assert(metric(row, "occurrences") == 3U);
    assert(metric(row, "hits") == 1U);
    assert(metric(row, "lowerer_calls") == 2U);

    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;
    const auto process_table = ProcessAccess::process_table(
        *elaborated.design);
    assert(ProcessAccess::row_backed(*elaborated.design));
    assert(process_table != nullptr);
    std::optional<std::size_t> left_row;
    std::optional<std::size_t> right_row;
    for (std::size_t index = 0U;
        index < ProcessAccess::process_count(*elaborated.design); ++index) {
        const auto view = ProcessAccess::process_view(
            *elaborated.design, index);
        if (view.name() == "template_top.left_lane.transfer_value") {
            left_row = index;
        } else if (view.name()
            == "template_top.right_lane.transfer_value") {
            right_row = index;
        }
    }
    assert(left_row && right_row);
    assert(process_table->rows[*left_row].template_id
        == process_table->rows[*right_row].template_id);
    const auto left_view = ProcessAccess::process_view(
        *elaborated.design, *left_row);
    const auto right_view = ProcessAccess::process_view(
        *elaborated.design, *right_row);
    assert(left_view.common_identity() == right_view.common_identity());
    assert(left_view.id() != right_view.id());
    assert(left_view.static_sensitivity().size() == 1U);
    assert(right_view.static_sensitivity().size() == 1U);
    assert(left_view.static_sensitivity().front().signal
        != right_view.static_sensitivity().front().signal);
    assert(left_view.operations().shares_body_with(
        right_view.operations()));

    const auto find_process = [&](const std::string_view name) -> const Process& {
        const auto found = std::ranges::find(
            elaborated.design->processes(), name, &Process::name);
        assert(found != elaborated.design->processes().end());
        return *found;
    };
    const auto& left = find_process("template_top.left_lane.transfer_value");
    const auto& right = find_process("template_top.right_lane.transfer_value");
    const auto& alias = find_process("template_top.alias_lane.transfer_value");
    assert(left.id != right.id && right.id != alias.id);
    assert(left.scheduling_domain == ProcessSchedulingDomain::generic);
    assert(right.scheduling_domain == left.scheduling_domain);
    assert(left.operations.shares_body_with(right.operations));
    assert(left.static_sensitivity.size() == 1U);
    assert(right.static_sensitivity.size() == 1U);
    assert(left.static_sensitivity.front().signal
        != right.static_sensitivity.front().signal);
    const auto assert_output_regions = [](
        const Process& process, const SignalId output_signal) {
        assert(process.driver_regions.size() == 2U);
        const auto whole = std::ranges::find_if(
            process.driver_regions,
            [&](const Process::DriverRegion& region) {
                return region.signal == output_signal && region.whole;
            });
        const auto bit_zero = std::ranges::find_if(
            process.driver_regions,
            [&](const Process::DriverRegion& region) {
                return region.signal == output_signal && !region.whole
                    && region.offset == 0U && region.width == 1U;
            });
        assert(whole != process.driver_regions.end());
        assert(bit_zero != process.driver_regions.end());
        assert(std::ranges::all_of(
            process.driver_regions,
            [&](const Process::DriverRegion& region) {
                return region.signal == output_signal;
            }));
    };
    const auto left_output_signal = elaborated.design->find_signal(
        "template_top.left_output");
    const auto right_output_signal = elaborated.design->find_signal(
        "template_top.right_output");
    assert(left_output_signal && right_output_signal);
    assert_output_regions(left, *left_output_signal);
    assert_output_regions(right, *right_output_signal);
    assert(left.driver_regions.front().signal != right.driver_regions.front().signal);
    assert(alias.static_sensitivity.front().signal == alias.driver_regions.front().signal);
    for (const auto* process : { &left, &right, &alias }) {
        bool whole_projected { };
        bool slice_projected { };
        for (std::size_t index = 0U; index < process->operations.size(); ++index) {
            const auto operation = process->operations.expanded(index);
            whole_projected |= operation_holds<WriteProjected>(operation);
            slice_projected |= operation_holds<WriteProjectedSlice>(operation);
            if (const auto* point = operation_get_if<DebugPoint>(&operation)) {
                assert(point->scope.empty()
                    || point->scope.starts_with(process->name));
            }
        }
        assert(whole_projected && slice_projected);
    }

    const auto signal = [&](const std::string_view name) {
        const auto found = elaborated.design->find_signal(name);
        assert(found);
        return *found;
    };
    const auto left_input = signal("template_top.left_input");
    const auto right_input = signal("template_top.right_input");
    const auto left_output = signal("template_top.left_output");
    const auto right_output = signal("template_top.right_output");
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    const auto first = fsim::runtime::PackedLogic4::from_logic9_msb_string("01XZ");
    const auto second = fsim::runtime::PackedLogic4::from_logic9_msb_string("10Z1");
    interpreter->deposit_signal(left_input, first);
    interpreter->deposit_signal(right_input, second);
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(left_output) == first);
    assert(interpreter->signal_value(right_output) == second);
    const auto changed = fsim::runtime::PackedLogic4::from_logic9_msb_string("X110");
    interpreter->deposit_signal(right_input, changed);
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(left_output) == first);
    assert(interpreter->signal_value(right_output) == changed);

    fsim::diagnostic::Engine diagnostics;
    const auto encoded = fsim::app::runtime_path_codec::serialize_runtime_path_state(
        elaborated.design->state(), nullptr, diagnostics);
    assert(encoded);
    const auto decoded = fsim::app::runtime_path_codec::deserialize_runtime_path_state(
        *encoded, "vhdl-process-template", nullptr, diagnostics);
    assert(decoded);
    const auto decoded_left = std::ranges::find(decoded->processes, left.name, &Process::name);
    const auto decoded_right = std::ranges::find(decoded->processes, right.name, &Process::name);
    assert(decoded_left != decoded->processes.end() && decoded_right != decoded->processes.end());
    assert(decoded_left->operations.shares_body_with(decoded_right->operations));
    assert(decoded_right->static_sensitivity == right.static_sensitivity);
    assert(decoded_right->driver_regions == right.driver_regions);
    assert(decoded_right->scheduling_domain == right.scheduling_domain);

    const auto input_alias_write = fsim::frontend::parse_text(
        "vhdl-input-alias-write.vhd", R"(
entity input_alias_leaf is
  port (source : in std_logic_vector(3 downto 0);
        destination : out std_logic_vector(3 downto 0));
end entity;
architecture rtl of input_alias_leaf is
begin
  invalid_write : process(destination)
  begin
    source <= destination;
  end process;
end architecture;
entity input_alias_top is
end entity;
architecture rtl of input_alias_top is
  signal aliased : std_logic_vector(3 downto 0);
begin
  alias_lane : entity work.input_alias_leaf(rtl)
    port map (aliased, aliased);
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(input_alias_write.ok());
    const auto rejected = compile_and_elaborate(
        input_alias_write.design, "vhdl:work.input_alias_top(rtl)");
    assert(!rejected.ok());
    assert(std::ranges::any_of(
        rejected.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVIFACE-006";
        }));

    const auto input_alias_slice_write = fsim::frontend::parse_text(
        "vhdl-input-alias-slice-write.vhd", R"(
entity input_alias_slice_leaf is
  port (source : in std_logic_vector(3 downto 0);
        destination : out std_logic_vector(3 downto 0));
end entity;
architecture rtl of input_alias_slice_leaf is
begin
  invalid_slice_write : process(destination)
  begin
    source(0) <= destination(0);
  end process;
end architecture;
entity input_alias_slice_top is
end entity;
architecture rtl of input_alias_slice_top is
  signal aliased : std_logic_vector(3 downto 0);
begin
  alias_lane : entity work.input_alias_slice_leaf(rtl)
    port map (aliased, aliased);
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(input_alias_slice_write.ok());
    const auto rejected_slice = compile_and_elaborate(
        input_alias_slice_write.design,
        "vhdl:work.input_alias_slice_top(rtl)");
    assert(!rejected_slice.ok());
    assert(std::ranges::any_of(
        rejected_slice.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVIFACE-006";
        }));
}


void test_vhdl_generate_relative_process_template_replay()
{
    using namespace fsim::runtime::simir;
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-generated-process-template.vhd", R"(
entity generated_leaf is
  port (source : in bit_vector(1 downto 0);
        destination : out bit_vector(1 downto 0));
end entity;
architecture rtl of generated_leaf is
begin
  lane_gen : for i in 0 to 1 generate
    lane : block
      signal dst : bit;
    begin
      transfer : process(source)
      begin
        dst <= source(i);
      end process;
      destination(i) <= dst;
    end block lane;
  end generate lane_gen;
end architecture;
entity generated_top is
end entity;
architecture rtl of generated_top is
  signal left_source, right_source : bit_vector(1 downto 0);
  signal left_destination, right_destination : bit_vector(1 downto 0);
begin
  left_lane : entity work.generated_leaf(rtl)
    port map (source => left_source,
              destination => left_destination);
  right_lane : entity work.generated_leaf(rtl)
    port map (source => right_source,
              destination => right_destination);
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    fsim::elaboration::ElaborationResult elaborated;
    std::ostringstream census;
    {
        const VhdlTemplateProfile capture { census };
        elaborated = compile_and_elaborate(
            parsed.design, "vhdl:work.generated_top(rtl)");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    const auto output = census.str();
    const auto start = output.find("fsim-profile: vhdl-process-template ");
    assert(start != std::string::npos);
    const auto row = std::string_view { output }.substr(
        start, output.find('\n', start) - start);
    assert(metric(row, "occurrences") == 4U);
    assert(metric(row, "lowerer_calls") == 2U);
    assert(metric(row, "hits") == 2U);

    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;
    const auto process_table = ProcessAccess::process_table(
        *elaborated.design);
    assert(ProcessAccess::row_backed(*elaborated.design));
    assert(process_table != nullptr);
    std::vector<ProcessProgramView> transfer_views;
    for (std::size_t index = 0U;
        index < ProcessAccess::process_count(*elaborated.design); ++index) {
        auto view = ProcessAccess::process_view(*elaborated.design, index);
        if (view.name().find(".transfer") != std::string::npos) {
            transfer_views.push_back(view);
        }
    }
    assert(transfer_views.size() == 4U);
    const auto find_transfer_view = [&](const std::string_view instance,
                                        const std::size_t iteration)
        -> const ProcessProgramView& {
        const auto iteration_name = iteration == 0U
            ? std::string_view { "lane_gen[0]" }
            : std::string_view { "lane_gen[1]" };
        const auto found = std::ranges::find_if(
            transfer_views, [&](const ProcessProgramView& view) {
                return view.name().find(instance) != std::string::npos
                    && view.name().find(iteration_name)
                        != std::string::npos;
            });
        assert(found != transfer_views.end());
        return *found;
    };
    for (std::size_t iteration = 0U; iteration < 2U; ++iteration) {
        const auto& left_view = find_transfer_view("left_lane", iteration);
        const auto& right_view = find_transfer_view("right_lane", iteration);
        assert(left_view.id() != right_view.id());
        assert(left_view.common_identity() == right_view.common_identity());
        assert(left_view.operations().shares_body_with(
            right_view.operations()));
    }

    std::vector<const Process*> transfers;
    for (const auto& process : elaborated.design->processes()) {
        if (process.name.find(".transfer") != std::string::npos) {
            transfers.push_back(&process);
        }
    }
    assert(transfers.size() == 4U);
    const auto find_transfer = [&](const std::string_view instance,
                                   const std::size_t iteration)
        -> const Process& {
        const auto iteration_name = iteration == 0U
            ? std::string_view { "lane_gen[0]" }
            : std::string_view { "lane_gen[1]" };
        const auto found = std::ranges::find_if(
            transfers, [&](const Process* const process) {
                return process->name.find(instance)
                        != std::string::npos
                    && process->name.find(iteration_name)
                        != std::string::npos;
            });
        assert(found != transfers.end());
        return **found;
    };
    for (std::size_t iteration = 0U; iteration < 2U; ++iteration) {
        const auto& left = find_transfer("left_lane", iteration);
        const auto& right = find_transfer("right_lane", iteration);
        assert(left.id != right.id);
        assert(left.operations.shares_body_with(right.operations));
        const auto right_overrides
            = right.operations.instance_operation_overrides();
        assert(!right_overrides.empty());
        assert(right_overrides.size() < right.operations.size());
    }
    const auto left_source = elaborated.design->find_signal(
        "generated_top.left_source");
    const auto right_source = elaborated.design->find_signal(
        "generated_top.right_source");
    const auto left_destination = elaborated.design->find_signal(
        "generated_top.left_destination");
    const auto right_destination = elaborated.design->find_signal(
        "generated_top.right_destination");
    assert(left_source && right_source
        && left_destination && right_destination);
    std::vector<SignalId> owners;
    for (const auto* const process : transfers) {
        const bool left_instance
            = process->name.find("left_lane") != std::string::npos;
        const bool right_instance
            = process->name.find("right_lane") != std::string::npos;
        assert(left_instance != right_instance);
        const bool iteration_zero
            = process->name.find("lane_gen[0]") != std::string::npos;
        const bool iteration_one
            = process->name.find("lane_gen[1]") != std::string::npos;
        assert(iteration_zero != iteration_one);
        const auto iteration = iteration_zero ? 0U : 1U;
        const auto expected_source
            = left_instance ? *left_source : *right_source;
        const auto source_reads = std::ranges::count_if(
            process->operations, [&](const Operation& operation) {
                const auto* const read
                    = operation_get_if<ReadSignal>(&operation);
                return read != nullptr
                    && read->signal == expected_source;
            });
        assert(source_reads == 1U);
        std::size_t matching_extracts { };
        for (std::size_t index = 0U;
             index < process->operations.size(); ++index) {
            const auto operation = process->operations.expanded(index);
            if (const auto* const extract
                = operation_get_if<Extract>(&operation)) {
                assert(extract->offset == iteration);
                assert(extract->width == 1U);
                ++matching_extracts;
            }
        }
        assert(matching_extracts == 1U);
        assert(process->driver_regions.size() == 1U);
        const auto& driver = process->driver_regions.front();
        assert(driver.whole);
        assert(std::ranges::find(owners, driver.signal) == owners.end());
        owners.push_back(driver.signal);
        const auto& owner_name
            = elaborated.design->signals().at(driver.signal).name;
        assert(owner_name.find(left_instance
                ? "left_lane.lane_gen[" : "right_lane.lane_gen[")
            != std::string::npos);
        assert(owner_name.ends_with(".dst"));
        const auto projected = std::ranges::find_if(
            process->operations, [&](const Operation& operation) {
                const auto* const write
                    = operation_get_if<WriteProjected>(&operation);
                return write != nullptr && write->signal == driver.signal;
            });
        assert(projected != process->operations.end());
        const auto* const write
            = operation_get_if<WriteProjected>(&*projected);
        assert(write != nullptr && write->delay == 0U
            && write->rejection == 0U
            && write->mode == ProjectedDelayMode::inertial);
    }

    const auto left = fsim::runtime::PackedLogic4::from_msb_string("01");
    const auto right = fsim::runtime::PackedLogic4::from_msb_string("10");
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    interpreter->deposit_signal(*left_source, left);
    interpreter->deposit_signal(*right_source, right);
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*left_destination) == left);
    assert(interpreter->signal_value(*right_destination) == right);
    interpreter->deposit_signal(*left_source,
        fsim::runtime::PackedLogic4::from_msb_string("10"));
    interpreter->deposit_signal(*right_source,
        fsim::runtime::PackedLogic4::from_msb_string("01"));
    assert(interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*left_destination)
        == fsim::runtime::PackedLogic4::from_msb_string("10"));
    assert(interpreter->signal_value(*right_destination)
        == fsim::runtime::PackedLogic4::from_msb_string("01"));

    const auto readonly = fsim::frontend::parse_text(
        "vhdl-generated-readonly-process-template.vhd", R"(
entity generated_alias_leaf is
  port (source : in bit_vector(1 downto 0);
        destination : out bit_vector(1 downto 0));
end entity;
architecture rtl of generated_alias_leaf is
begin
  lane_gen : for i in 0 to 1 generate
    lane : block
      signal dst : bit;
    begin
      transfer : process(source)
      begin
        dst <= source(i);
      end process;
      destination(i) <= dst;
    end block lane;
  end generate lane_gen;
end architecture;
entity generated_alias_top is
end entity;
architecture rtl of generated_alias_top is
  signal left_shared, right_shared : bit_vector(1 downto 0);
begin
  left_lane : entity work.generated_alias_leaf(rtl)
    port map (source => left_shared,
              destination => left_shared);
  right_lane : entity work.generated_alias_leaf(rtl)
    port map (source => right_shared,
              destination => right_shared);
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(readonly.ok());
    fsim::elaboration::ElaborationResult readonly_elaborated;
    std::ostringstream readonly_census;
    {
        const VhdlTemplateProfile capture { readonly_census };
        readonly_elaborated = compile_and_elaborate(
            readonly.design, "vhdl:work.generated_alias_top(rtl)");
    }
    assert(readonly_elaborated.ok() && readonly_elaborated.design);
    const auto readonly_output = readonly_census.str();
    const auto readonly_start = readonly_output.find(
        "fsim-profile: vhdl-process-template ");
    assert(readonly_start != std::string::npos);
    const auto readonly_row = std::string_view { readonly_output }.substr(
        readonly_start,
        readonly_output.find('\n', readonly_start) - readonly_start);
    assert(metric(readonly_row, "occurrences") == 4U);
    assert(metric(readonly_row, "hits") == 0U);
    assert(metric(readonly_row, "lowerer_calls") == 4U);

    const auto selected_branches = fsim::frontend::parse_text(
        "vhdl-generated-branch-process-template.vhd", R"(
entity selected_leaf is
  generic (choose_positive : boolean := true);
  port (source : in bit_vector(1 downto 0);
        destination : out bit_vector(1 downto 0));
end entity;
architecture rtl of selected_leaf is
begin
  lane_gen : for i in 0 to 1 generate
    signal parent_value : bit;
  begin
    parent_value <= source(i);
    selected_branch : if choose_positive generate
      positive_lane : block
        signal dst : bit;
      begin
        transfer : process(parent_value)
        begin
          dst <= parent_value;
        end process;
        destination(i) <= dst;
      end block positive_lane;
    else generate
      negative_lane : block
        signal dst : bit;
      begin
        transfer : process(parent_value)
        begin
          dst <= not parent_value;
        end process;
        destination(i) <= dst;
      end block negative_lane;
    end generate selected_branch;
  end generate lane_gen;
end architecture;
entity selected_top is
end entity;
architecture rtl of selected_top is
  signal positive_left_source, positive_right_source : bit_vector(1 downto 0);
  signal negative_left_source, negative_right_source : bit_vector(1 downto 0);
  signal positive_left_destination, positive_right_destination : bit_vector(1 downto 0);
  signal negative_left_destination, negative_right_destination : bit_vector(1 downto 0);
begin
  positive_left : entity work.selected_leaf(rtl)
    generic map (choose_positive => true)
    port map (source => positive_left_source,
              destination => positive_left_destination);
  positive_right : entity work.selected_leaf(rtl)
    generic map (choose_positive => true)
    port map (source => positive_right_source,
              destination => positive_right_destination);
  negative_left : entity work.selected_leaf(rtl)
    generic map (choose_positive => false)
    port map (source => negative_left_source,
              destination => negative_left_destination);
  negative_right : entity work.selected_leaf(rtl)
    generic map (choose_positive => false)
    port map (source => negative_right_source,
              destination => negative_right_destination);
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(selected_branches.ok());
    fsim::elaboration::ElaborationResult selected_elaborated;
    std::ostringstream selected_census;
    {
        const VhdlTemplateProfile capture { selected_census };
        selected_elaborated = compile_and_elaborate(
            selected_branches.design, "vhdl:work.selected_top(rtl)");
    }
    if (!selected_elaborated.ok()) {
        for (const auto& diagnostic : selected_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message
                      << '\n';
        }
    }
    assert(selected_elaborated.ok() && selected_elaborated.design);
    const auto selected_output = selected_census.str();
    const auto selected_start = selected_output.find(
        "fsim-profile: vhdl-process-template ");
    assert(selected_start != std::string::npos);
    const auto selected_row = std::string_view { selected_output }.substr(
        selected_start,
        selected_output.find('\n', selected_start) - selected_start);
    assert(metric(selected_row, "occurrences") == 8U);
    assert(metric(selected_row, "hits") == 4U);
    assert(metric(selected_row, "lowerer_calls") == 4U);

    const auto selected_signal = [&](const std::string_view name) {
        const auto signal = selected_elaborated.design->find_signal(name);
        assert(signal);
        return *signal;
    };
    const auto positive_left_source
        = selected_signal("selected_top.positive_left_source");
    const auto positive_right_source
        = selected_signal("selected_top.positive_right_source");
    const auto negative_left_source
        = selected_signal("selected_top.negative_left_source");
    const auto negative_right_source
        = selected_signal("selected_top.negative_right_source");
    const auto positive_left_destination
        = selected_signal("selected_top.positive_left_destination");
    const auto positive_right_destination
        = selected_signal("selected_top.positive_right_destination");
    const auto negative_left_destination
        = selected_signal("selected_top.negative_left_destination");
    const auto negative_right_destination
        = selected_signal("selected_top.negative_right_destination");

    std::vector<const Process*> selected_transfers;
    std::vector<SignalId> selected_owners;
    for (const auto& process : selected_elaborated.design->processes()) {
        if (process.name.find(".transfer") == std::string::npos) {
            continue;
        }
        selected_transfers.push_back(&process);
        assert(process.static_sensitivity.size() == 1U);
        std::size_t parent_reads { };
        for (const auto& operation : process.operations) {
            const auto* const read
                = operation_get_if<ReadSignal>(&operation);
            if (read == nullptr) {
                continue;
            }
            const auto& read_name
                = selected_elaborated.design->signals().at(read->signal).name;
            assert(read_name.ends_with(".parent_value"));
            const auto lane_marker = process.name.find(".lane_gen[");
            assert(lane_marker != std::string::npos);
            const auto lane_end = process.name.find(']', lane_marker);
            assert(lane_end != std::string::npos);
            const auto lane_path = std::string_view { process.name }
                                       .substr(0U, lane_end + 1U);
            assert(read_name.starts_with(lane_path));
            assert(process.static_sensitivity.front().signal == read->signal);
            ++parent_reads;
        }
        assert(parent_reads == 1U);
        assert(process.driver_regions.size() == 1U);
        const auto owner = process.driver_regions.front().signal;
        assert(process.driver_regions.front().whole);
        assert(std::ranges::find(selected_owners, owner)
            == selected_owners.end());
        selected_owners.push_back(owner);
        const auto& owner_name
            = selected_elaborated.design->signals().at(owner).name;
        assert(owner_name.ends_with(".dst"));
        const auto projected = std::ranges::find_if(
            process.operations, [&](const Operation& operation) {
                const auto* const write
                    = operation_get_if<WriteProjected>(&operation);
                return write != nullptr && write->signal == owner;
            });
        assert(projected != process.operations.end());
        const auto* const write
            = operation_get_if<WriteProjected>(&*projected);
        assert(write != nullptr && write->delay == 0U
            && write->rejection == 0U
            && write->mode == ProjectedDelayMode::inertial);
    }
    assert(selected_transfers.size() == 8U);

    auto selected_interpreter
        = selected_elaborated.design->create_interpreter();
    selected_interpreter->start();
    assert(selected_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto first_value
        = fsim::runtime::PackedLogic4::from_msb_string("01");
    const auto second_value
        = fsim::runtime::PackedLogic4::from_msb_string("10");
    selected_interpreter->deposit_signal(positive_left_source, first_value);
    selected_interpreter->deposit_signal(positive_right_source, second_value);
    selected_interpreter->deposit_signal(negative_left_source, first_value);
    selected_interpreter->deposit_signal(negative_right_source, second_value);
    assert(selected_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(selected_interpreter->signal_value(positive_left_destination)
        == first_value);
    assert(selected_interpreter->signal_value(positive_right_destination)
        == second_value);
    assert(selected_interpreter->signal_value(negative_left_destination)
        == fsim::runtime::PackedLogic4::from_msb_string("10"));
    assert(selected_interpreter->signal_value(negative_right_destination)
        == fsim::runtime::PackedLogic4::from_msb_string("01"));
}

void test_vhdl_concurrent_statement_template_rows()
{
    using namespace fsim::runtime::simir;
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-concurrent-statement-template.vhd", R"(
entity statement_leaf is
  port (source : in std_logic;
        destination : out std_logic);
end entity;
architecture rtl of statement_leaf is
  function copy_value(value : std_logic) return std_logic is
  begin
    return value;
  end function;
begin
  transfer_value : destination <= copy_value(source);
end architecture;
entity statement_top is
end entity;
architecture rtl of statement_top is
  signal left_input, right_input : std_logic;
  signal left_output, right_output : std_logic;
begin
  left_lane : entity work.statement_leaf(rtl)
    port map (left_input, left_output);
  right_lane : entity work.statement_leaf(rtl)
    port map (right_input, right_output);
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());

    fsim::elaboration::ElaborationResult elaborated;
    std::ostringstream census;
    {
        const VhdlTemplateProfile capture { census };
        elaborated = compile_and_elaborate(
            parsed.design, "vhdl:work.statement_top(rtl)");
    }
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);

    const auto output = census.str();
    const auto start = output.find(
        "fsim-profile: concurrent-process-template ");
    assert(start != std::string::npos);
    const auto row = std::string_view { output }.substr(
        start, output.find('\n', start) - start);
    assert(metric(row, "occurrences") == 2U);
    assert(metric(row, "hits") == 1U);
    assert(metric(row, "lowerer_calls") == 1U);

    using ProcessAccess
        = fsim::elaboration::detail::ElaboratedDesignProcessAccess;
    const auto process_table = ProcessAccess::process_table(
        *elaborated.design);
    assert(ProcessAccess::row_backed(*elaborated.design));
    assert(process_table != nullptr);
    std::optional<std::size_t> left_row;
    std::optional<std::size_t> right_row;
    for (std::size_t index = 0U;
         index < ProcessAccess::process_count(*elaborated.design); ++index) {
        const auto view = ProcessAccess::process_view(
            *elaborated.design, index);
        if (view.name() == "statement_top.left_lane.transfer_value") {
            left_row = index;
        } else if (view.name()
            == "statement_top.right_lane.transfer_value") {
            right_row = index;
        }
    }
    assert(left_row && right_row);
    assert(process_table->rows[*left_row].template_id
        == process_table->rows[*right_row].template_id);
    const auto left_view = ProcessAccess::process_view(
        *elaborated.design, *left_row);
    const auto right_view = ProcessAccess::process_view(
        *elaborated.design, *right_row);
    assert(left_view.id() != right_view.id());
    assert(left_view.common_identity() == right_view.common_identity());
    assert(left_view.operations().shares_body_with(
        right_view.operations()));
    assert(left_view.static_sensitivity().size() == 1U);
    assert(right_view.static_sensitivity().size() == 1U);

    const auto left_input = elaborated.design->find_signal(
        "statement_top.left_input");
    const auto right_input = elaborated.design->find_signal(
        "statement_top.right_input");
    const auto left_output = elaborated.design->find_signal(
        "statement_top.left_output");
    const auto right_output = elaborated.design->find_signal(
        "statement_top.right_output");
    assert(left_input && right_input && left_output && right_output);
    assert(left_view.static_sensitivity().front().signal == *left_input);
    assert(right_view.static_sensitivity().front().signal == *right_input);
    assert(left_view.driver_regions().size() == 1U);
    assert(right_view.driver_regions().size() == 1U);
    assert(left_view.driver_regions().front().signal == *left_output
        && left_view.driver_regions().front().whole);
    assert(right_view.driver_regions().front().signal == *right_output
        && right_view.driver_regions().front().whole);

    fsim::diagnostic::Engine diagnostics;
    const auto encoded = fsim::app::runtime_path_codec::
        serialize_runtime_path_state(
            elaborated.design->state(), nullptr, diagnostics);
    assert(encoded);
    const auto decoded = fsim::app::runtime_path_codec::
        deserialize_runtime_path_state(
            *encoded, "vhdl-concurrent-statement-template", nullptr,
            diagnostics);
    assert(decoded);
    const auto decoded_left = std::ranges::find(
        decoded->processes, left_view.name(), &Process::name);
    const auto decoded_right = std::ranges::find(
        decoded->processes, right_view.name(), &Process::name);
    assert(decoded_left != decoded->processes.end()
        && decoded_right != decoded->processes.end());
    assert(decoded_left->operations.shares_body_with(
        decoded_right->operations));
    assert(decoded_left->static_sensitivity
        == left_view.static_sensitivity());
    assert(decoded_right->static_sensitivity
        == right_view.static_sensitivity());
    assert(decoded_left->driver_regions == left_view.driver_regions());
    assert(decoded_right->driver_regions == right_view.driver_regions());

    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    interpreter->deposit_signal(*left_input,
        fsim::runtime::PackedLogic4::from_logic9_msb_string("0"));
    interpreter->deposit_signal(*right_input,
        fsim::runtime::PackedLogic4::from_logic9_msb_string("1"));
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value_snapshot(*left_output)
        == fsim::runtime::PackedLogic4::from_logic9_msb_string("0"));
    assert(interpreter->signal_value_snapshot(*right_output)
        == fsim::runtime::PackedLogic4::from_logic9_msb_string("1"));
}

void test_vhdl_projected_constant_startup_lowering()
{
    using namespace fsim::runtime::simir;
    using fsim::runtime::SimulationTick;
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-projected-constant-lowering.vhd", R"(
library ieee;
use ieee.std_logic_1164.all;
entity projected_constant_top is
end entity;
architecture rtl of projected_constant_top is
  signal inertial_output : std_logic_vector(8 downto 0);
  signal transport_output : std_logic;
  signal equal_initial_output : std_logic;
begin
  inertial_constant : inertial_output <= reject 2 ps inertial
    "UX01ZWLH-" after 5 ps;
  transport_constant : transport_output <= transport '1'
    after 5 ps;
  equal_initial_constant : equal_initial_output <= 'U';
end architecture;
)", fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "vhdl:work.projected_constant_top(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message
                      << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->processes().size() == 3U);

    const auto signal = [&](const std::string_view name) {
        const auto found = elaborated.design->find_signal(name);
        assert(found);
        return *found;
    };
    const auto inertial_signal = signal(
        "projected_constant_top.inertial_output");
    const auto transport_signal = signal(
        "projected_constant_top.transport_output");
    const auto equal_initial_signal = signal(
        "projected_constant_top.equal_initial_output");
    const auto find_process = [&](const std::string_view suffix)
        -> const Process& {
        const auto qualified_suffix
            = std::string { "." } + std::string { suffix };
        const auto found = std::ranges::find_if(
            elaborated.design->processes(), [&](const Process& process) {
                return process.name == suffix
                    || process.name.ends_with(qualified_suffix);
            });
        assert(found != elaborated.design->processes().end());
        return *found;
    };
    const auto inspect_constant_owner = [](
        const Process& process,
        const SignalId expected_signal,
        const std::string_view expected_value,
        const std::optional<SimulationTick> expected_delay,
        const std::optional<SimulationTick> expected_rejection,
        const ProjectedDelayMode mode) {
        assert(process.scheduling_domain == ProcessSchedulingDomain::generic);
        assert(process.static_sensitivity.empty());
        assert(process.register_count == 1U);
        assert(process.register_value_kinds.size() == 1U
            && process.register_value_kinds.front() == ValueKind::logic9);
        assert(process.driver_regions.size() == 1U);
        assert(process.driver_regions.front().signal == expected_signal
            && process.driver_regions.front().whole);
        assert(process.operations.size() == 4U
            || process.operations.size() == 5U);
        const auto statement_offset
            = process.operations.size() == 5U ? 1U : 0U;

        const auto entry_operation = process.operations.expanded(0U);
        const auto statement_operation
            = process.operations.expanded(statement_offset);
        const auto load_operation
            = process.operations.expanded(1U + statement_offset);
        const auto write_operation
            = process.operations.expanded(2U + statement_offset);
        const auto halt_operation
            = process.operations.expanded(3U + statement_offset);
        const auto* const entry
            = operation_get_if<DebugPoint>(&entry_operation);
        const auto* const statement = statement_offset != 0U
            ? operation_get_if<DebugPoint>(&statement_operation) : nullptr;
        const auto* const load
            = operation_get_if<LoadConstant>(&load_operation);
        const auto* const write
            = operation_get_if<WriteProjected>(&write_operation);
        const auto* const halt = operation_get_if<Halt>(&halt_operation);
        assert(entry != nullptr
            && entry->kind == DebugPointKind::process_entry);
        assert(statement_offset == 0U
            || (statement != nullptr
                && statement->kind == DebugPointKind::statement));
        assert(load != nullptr && load->destination == 0U
            && load->value.width() > 0U
            && load->value.to_msb_string() == std::string { expected_value });
        assert(write != nullptr && write->signal == expected_signal
            && write->source == 0U
            && (!expected_delay || write->delay == *expected_delay)
            && (!expected_rejection
                || write->rejection == *expected_rejection)
            && write->mode == mode);
        assert(halt != nullptr && !halt->program_exit);
        return *write;
    };

    const auto& inertial_owner = find_process("inertial_constant");
    const auto& transport_owner = find_process("transport_constant");
    const auto& equal_initial_owner = find_process("equal_initial_constant");
    assert(inertial_owner.operations.size() == 5U);
    const auto statement_operation = inertial_owner.operations.expanded(1U);
    const auto* const statement
        = operation_get_if<DebugPoint>(&statement_operation);
    assert(statement != nullptr
        && statement->kind == DebugPointKind::statement);
    const auto& statement_point
        = inertial_owner.operations.debug_point(1U, *statement);
    const auto statement_scope
        = inertial_owner.operations.debug_scope(statement_point.scope);
    const auto delayed_value
        = fsim::runtime::PackedLogic4::from_logic9_msb_string("UX01ZWLH-");
    const auto inertial_write = inspect_constant_owner(
        inertial_owner, inertial_signal, "UX01ZWLH-",
        std::nullopt, std::nullopt,
        ProjectedDelayMode::inertial);
    const auto transport_write = inspect_constant_owner(
        transport_owner, transport_signal, "1", std::nullopt,
        std::optional<SimulationTick> { 0U },
        ProjectedDelayMode::transport);
    static_cast<void>(inspect_constant_owner(
        equal_initial_owner, equal_initial_signal, "U",
        std::optional<SimulationTick> { 0U },
        std::optional<SimulationTick> { 0U },
        ProjectedDelayMode::inertial));
    assert(inertial_write.delay > 0U
        && inertial_write.rejection > 0U
        && inertial_write.rejection <= inertial_write.delay);
    assert(inertial_write.delay
                <= std::numeric_limits<SimulationTick>::max() / 2U
        && inertial_write.rejection
                <= std::numeric_limits<SimulationTick>::max() / 5U
        && inertial_write.delay * 2U == inertial_write.rejection * 5U);
    assert(transport_write.delay == inertial_write.delay
        && transport_write.rejection == 0U);

    auto interpreter = elaborated.design->create_interpreter();
    assert(OwnedDriverDemotionTestAccess::startup_banked(
               *interpreter, inertial_owner.id)
        && OwnedDriverDemotionTestAccess::startup_banked(
            *interpreter, transport_owner.id)
        && OwnedDriverDemotionTestAccess::startup_banked(
            *interpreter, equal_initial_owner.id)
        && !OwnedDriverDemotionTestAccess::process_frame_materialized(
            *interpreter, inertial_owner.id));
    ProjectedConstantTrace trace;
    interpreter->scheduler().set_trace_hook(
        &trace, &ProjectedConstantTrace::receive);
    std::size_t statement_points { };
    std::size_t suspension_points { };
    interpreter->set_execution_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const ExecutionPoint& point) {
            if (point.process != inertial_owner.id) {
                return;
            }
            if (point.kind == ExecutionPointKind::statement) {
                ++statement_points;
                assert(point.instruction == 1U
                    && point.source == statement_point.source
                    && point.scope == statement_scope);
                scheduler.request_stop();
            }
            if (point.kind == ExecutionPointKind::process_suspend) {
                ++suspension_points;
                assert(point.instruction == 4U
                    && point.source == statement_point.source
                    && point.scope == statement_scope);
            }
        });
    const auto stopped_at_statement = interpreter->run();
    assert(stopped_at_statement.status == fsim::runtime::RunStatus::stopped
        && statement_points == 1U
        && interpreter->process_instruction(inertial_owner.id) == 2U
        && trace.count_for(inertial_signal) == 0U
        && OwnedDriverDemotionTestAccess::startup_banked(
            *interpreter, inertial_owner.id)
        && !OwnedDriverDemotionTestAccess::process_frame_materialized(
            *interpreter, inertial_owner.id));
    interpreter->scheduler().clear_stop();
    // Do not read a signal through the public getter until after this trace:
    // that observation is itself allowed to materialize the constant owner.
    const auto before_deadline = interpreter->run(inertial_write.delay - 1U);
    assert(before_deadline.status == fsim::runtime::RunStatus::time_limit
        && before_deadline.time == inertial_write.delay - 1U);
    assert(trace.count_for(equal_initial_signal) == 1U
        && trace.event_for(equal_initial_signal, 0U).kind
            == fsim::runtime::SchedulerTraceKind::signal_transaction
        && trace.event_for(equal_initial_signal, 0U).time == 0U);
    assert(trace.count_for(inertial_signal) == 0U
        && trace.count_for(transport_signal) == 0U);

    const auto at_deadline = interpreter->run(inertial_write.delay);
    interpreter->scheduler().set_trace_hook(nullptr, nullptr);
    assert(at_deadline.status == fsim::runtime::RunStatus::completed
        && at_deadline.time == inertial_write.delay
        && !trace.overflow && suspension_points == 1U
        && OwnedDriverDemotionTestAccess::startup_banked(
            *interpreter, inertial_owner.id));
    for (const auto signal_id : { inertial_signal, transport_signal }) {
        assert(trace.count_for(signal_id) == 2U);
        assert(trace.event_for(signal_id, 0U).kind
                == fsim::runtime::SchedulerTraceKind::signal_transaction
            && trace.event_for(signal_id, 0U).time == inertial_write.delay);
        assert(trace.event_for(signal_id, 1U).kind
                == fsim::runtime::SchedulerTraceKind::signal_change
            && trace.event_for(signal_id, 1U).time == inertial_write.delay);
    }
    assert(trace.count_for(equal_initial_signal) == 1U);
    assert(interpreter->signal_value_snapshot(inertial_signal)
        == delayed_value);
    assert(interpreter->signal_value_snapshot(transport_signal)
        == fsim::runtime::PackedLogic4::from_logic9_msb_string("1"));
    assert(interpreter->signal_value_snapshot(equal_initial_signal)
        == fsim::runtime::PackedLogic4::from_logic9_msb_string("U"));
}

} // namespace fsim::tests::elaboration
