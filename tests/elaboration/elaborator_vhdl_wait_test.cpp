// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/semantic/compiled_design_normalization.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_procedure_waits() {
  const auto parsed = fsim::frontend::parse_text(
      "vhdl-procedure-waits.vhd",
      R"(
entity procedure_waits is end entity;
architecture rtl of procedure_waits is
  signal trigger : boolean;
  signal first_seen : std_logic;
  signal second_seen : std_logic;
  procedure leaf is
  begin
    for index in 0 to 1 loop
      if index = 1 then
        wait on trigger until trigger for 3 ns;
      end if;
    end loop;
  end procedure;
  procedure middle is
  begin
    if true then
      leaf;
    else
      wait;
    end if;
  end procedure;
begin
  driver: process
  begin
    trigger <= false;
    wait for 1 ns;
    trigger <= true;
    wait for 1 ns;
    trigger <= false;
    wait for 2 ns;
    trigger <= true;
    wait;
  end process;
  worker: process
  begin
    first_seen <= '0';
    second_seen <= '0';
    middle;
    first_seen <= '1';
    middle;
    second_seen <= '1';
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(parsed.ok());
  const auto elaborated = compile_and_elaborate(
      parsed.design, "vhdl:work.procedure_waits(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto& worker = elaborated.design->processes().back();
  assert(std::ranges::any_of(
      worker.operations,
      [](const fsim::runtime::simir::Operation& operation) {
        const auto* wait = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::WaitOn>(&operation);
        return wait != nullptr && wait->timeout
            && wait->timeout_result && wait->timeout_origin;
      }));

  auto interpreter = elaborated.design->create_interpreter();
  const auto first = elaborated.design->find_signal("first_seen");
  const auto second = elaborated.design->find_signal("second_seen");
  assert(first && second);
  const auto before_second = interpreter->run(3);
  assert(before_second.status == fsim::runtime::RunStatus::time_limit);
  assert(interpreter->signal_value(*first).to_msb_string() == "1");
  assert(interpreter->signal_value(*second).to_msb_string() == "0");
  const auto after_second = interpreter->run(5);
  assert(after_second.status == fsim::runtime::RunStatus::completed
         || after_second.status == fsim::runtime::RunStatus::time_limit);
  assert(interpreter->signal_value(*first).to_msb_string() == "1");
  assert(interpreter->signal_value(*second).to_msb_string() == "1");

  const auto overload = fsim::frontend::parse_text(
      "vhdl-wait-overload.vhd",
      R"(
entity wait_overload is end entity;
architecture rtl of wait_overload is
  signal trigger : boolean;
  signal observed : boolean;
  procedure pause(value : integer) is
  begin
    wait;
  end procedure;
  procedure pause(value : boolean) is
  begin
    null;
  end procedure;
begin
  observer: process(trigger)
  begin
    pause(false);
    observed <= trigger;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(overload.ok());
  const auto overload_result = compile_and_elaborate(
      overload.design, "vhdl:work.wait_overload(rtl)");
  assert(overload_result.ok());

  const auto restricted = fsim::frontend::parse_text(
      "vhdl-restricted-waits.vhd",
      R"(
entity restricted_waits is end entity;
architecture rtl of restricted_waits is
  signal trigger : boolean;
  procedure leaf is
  begin
    wait on trigger;
  end procedure;
  procedure middle is
  begin
    leaf;
  end procedure;
  impure function illegal(value : boolean) return boolean is
  begin
    middle;
    return true;
  end function;
begin
  sensitized: process(trigger)
  begin
    middle;
  end process;
  caller: process
  begin
    if illegal(false) then
      null;
    end if;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(restricted.ok());
  auto restricted_compiled = compile_test_design(
      std::move(restricted.design));
  assert(fsim::semantic::normalize_compiled_design(
      restricted_compiled));
  const auto restricted_result = fsim::elaboration::elaborate(
      restricted_compiled, "vhdl:work.restricted_waits(rtl)");
  fsim::diagnostic::Engine codec_diagnostics;
  const auto encoded = fsim::app::serialize_compiled_hir_bundle(
      restricted_compiled, codec_diagnostics);
  assert(encoded && !codec_diagnostics.has_error());
  const auto decoded = fsim::app::deserialize_compiled_hir_bundle(
      *encoded, "vhdl-restricted-waits", codec_diagnostics);
  assert(decoded && !codec_diagnostics.has_error());
  const auto decoded_result = fsim::elaboration::elaborate(
      *decoded, "vhdl:work.restricted_waits(rtl)");
  assert(!restricted_result.ok() && !decoded_result.ok());
  const auto diagnostic_signatures = [](const auto& result) {
    std::vector<std::pair<std::string, std::string>> signatures;
    for (const auto& diagnostic : result.diagnostics) {
      signatures.emplace_back(
          diagnostic.code, diagnostic.message);
    }
    return signatures;
  };
  assert(diagnostic_signatures(restricted_result)
         == diagnostic_signatures(decoded_result));
  for (const auto* result :
      { &restricted_result, &decoded_result }) {
    assert(has_diagnostic(*result, "FSIM-VHDL-SEM-012"));
    assert(has_diagnostic(*result, "FSIM-ELAB-VHLEGAL-009"));
  }
}

}  // namespace fsim::tests::elaboration
