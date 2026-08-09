// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_register_model.hpp"

#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

template <typename Function>
void require_error(const std::string_view code, Function &&function,
                   const std::string_view message) {
  try {
    function();
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

struct BackdoorFixture {
  struct HdlState {
    PackedLogic4 deposited;
    PackedLogic4 current;
    bool forced{};
  };

  SystemVerilogClassHeap heap;
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRegisterModelService model;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmRootHandle peer_root{};
  SystemVerilogUvmRegisterBlockHandle block;
  SystemVerilogUvmRegisterHandle reg;
  SystemVerilogUvmRegisterFieldHandle field;
  SystemVerilogUvmRegisterMemoryHandle memory;
  std::map<std::pair<SystemVerilogUvmRegisterHdlKind, std::string>, HdlState>
      hdl;
  std::optional<std::string> fail_write_path;

  explicit BackdoorFixture(SystemVerilogUvmRegisterModelLimits limits = {})
      : objects(
            heap, [](std::string_view, std::string_view, std::string_view) {
              return SystemVerilogClassHandle{};
            }),
        components(heap, objects), model(components, limits) {
    root = components.create_root("backdoor");
    peer_root = components.create_root("peer");
    block = model.create_block({root, std::nullopt, "registers"});
    reg = model.create_register({block, "status", 16, 0});
    field = model.create_field(
        {reg, "low", 8, 0, SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
    memory = model.create_memory(
        {block, "samples", 16, 0, {4},
         SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
    model.lock_model(block);
    install_transport();
  }

  void add(const SystemVerilogUvmRegisterHdlKind kind, std::string path,
           PackedLogic4 value) {
    hdl.emplace(std::pair{kind, std::move(path)},
                HdlState{value, std::move(value), false});
  }

  void install_transport() {
    model.set_backdoor_transport({
        [this](const auto kind, const std::string_view path)
            -> std::optional<std::size_t> {
          const auto found = hdl.find({kind, std::string{path}});
          return found == hdl.end()
                     ? std::nullopt
                     : std::optional<std::size_t>{
                           found->second.current.width()};
        },
        [this](const auto kind, const std::string_view path) {
          const auto found = hdl.find({kind, std::string{path}});
          if (found == hdl.end())
            throw std::runtime_error{"missing HDL object"};
          return found->second.current;
        },
        [this](const auto kind, const std::string_view path, const auto operation,
               const PackedLogic4 &value) {
          if (fail_write_path && path == *fail_write_path)
            throw std::runtime_error{"injected HDL write failure"};
          auto found = hdl.find({kind, std::string{path}});
          if (found == hdl.end())
            throw std::runtime_error{"missing HDL object"};
          switch (operation) {
          case SystemVerilogUvmRegisterBackdoorKind::Deposit:
            found->second.deposited = value;
            if (!found->second.forced)
              found->second.current = value;
            return;
          case SystemVerilogUvmRegisterBackdoorKind::Force:
            found->second.current = value;
            found->second.forced = true;
            return;
          case SystemVerilogUvmRegisterBackdoorKind::Release:
            found->second.forced = false;
            found->second.current = found->second.deposited;
            return;
          case SystemVerilogUvmRegisterBackdoorKind::Read:
            throw std::runtime_error{"read passed to HDL writer"};
          }
        }});
  }

  [[nodiscard]] SystemVerilogUvmRegisterHdlPathHandle mixed_path() {
    return model.register_hdl_path(
        {root,
         "RTL",
         {reg, std::nullopt, std::nullopt, 0},
         {{SystemVerilogUvmRegisterHdlKind::Vpi, "sv.top.status_lane", 4, 0,
           8},
          {SystemVerilogUvmRegisterHdlKind::Vhpi, "vhdl.top.status_high", 0,
           8, 8}}});
  }
};

} // namespace

void test_systemverilog_uvm_register_user_frontdoors() {
  BackdoorFixture fixture;
  auto hardware = PackedLogic4::from_aval_bval(16, 0x1234, 0);
  bool saw_phase{};
  bool saw_process{};
  const auto frontdoor = fixture.model.register_user_frontdoor(
      {fixture.root,
       "custom",
       {fixture.reg, std::nullopt, std::nullopt, 0},
       [&](const SystemVerilogUvmRegisterFrontdoorOptions &options) {
         saw_phase = static_cast<bool>(options.phase);
         saw_process = static_cast<bool>(options.process);
         return SystemVerilogUvmRegisterOperationResult{
             SystemVerilogUvmRegisterOperationStatus::IsOk, hardware, false,
             {}};
       },
       [&](const PackedLogic4 &value,
           const SystemVerilogUvmRegisterFrontdoorOptions &) {
         hardware = value;
         return SystemVerilogUvmRegisterOperationResult{
             SystemVerilogUvmRegisterOperationStatus::IsOk, PackedLogic4{0},
             true, {}};
       }});
  const auto read = fixture.model.user_frontdoor_read(frontdoor);
  require(read.success() && read.value.low_word().aval == 0x1234 &&
              fixture.model.get_mirrored(fixture.reg).low_word().aval == 0x1234,
          "user frontdoor reads must predict exact callback values");
  const auto write = fixture.model.user_frontdoor_write(
      frontdoor, PackedLogic4::from_aval_bval(16, 0xabcd, 0));
  const auto snapshot = fixture.model.user_frontdoor_snapshot(frontdoor);
  require(write.success() && hardware.low_word().aval == 0xabcd &&
              snapshot.reads == 1 && snapshot.writes == 1 &&
              snapshot.failures == 0 && !saw_phase && !saw_process,
          "user frontdoor writes and inventories must retain exact state");

  const auto throwing = fixture.model.register_user_frontdoor(
      {fixture.root,
       "throwing",
       {std::nullopt, fixture.field, std::nullopt, 0},
       [](const auto &) -> SystemVerilogUvmRegisterOperationResult {
         throw std::runtime_error{"injected user-frontdoor failure"};
       },
       {}});
  const auto failure = fixture.model.user_frontdoor_read(throwing);
  require(!failure.success() &&
              failure.message.find("injected user-frontdoor failure") !=
                  std::string::npos &&
              fixture.model.user_frontdoor_snapshot(throwing).failures == 1,
          "user-frontdoor callback exceptions must be contained");
  require_error(
      "FSIM-UVM-REG-009",
      [&] {
        (void)fixture.model.register_user_frontdoor(
            {fixture.peer_root, "foreign",
             {fixture.reg, std::nullopt, std::nullopt, 0}, {}, {}});
      },
      "user frontdoors must reject foreign roots and missing callbacks");
}

void test_systemverilog_uvm_register_backdoors() {
  BackdoorFixture fixture;
  fixture.add(SystemVerilogUvmRegisterHdlKind::Vpi, "sv.top.status_lane",
              PackedLogic4::from_aval_bval(16, 0xa340, 0));
  fixture.add(SystemVerilogUvmRegisterHdlKind::Vhpi,
              "vhdl.top.status_high",
              PackedLogic4::from_aval_bval(8, 0x12, 0));
  fixture.add(SystemVerilogUvmRegisterHdlKind::Vhpi, "vhdl.top.samples_2",
              PackedLogic4::from_aval_bval(16, 0, 0));
  const auto path = fixture.mixed_path();
  const auto path_snapshot = fixture.model.hdl_path_snapshot(path);
  require(path_snapshot.slices.size() == 2 &&
              path_snapshot.slices.front().kind ==
                  SystemVerilogUvmRegisterHdlKind::Vpi &&
              path_snapshot.slices.back().kind ==
                  SystemVerilogUvmRegisterHdlKind::Vhpi,
          "HDL path inventory must retain mixed-language concatenation");
  const auto read = fixture.model.backdoor_read(path);
  require(read.success() && read.value.low_word().aval == 0x1234,
          "mixed VPI/VHPI slices must assemble in logical value order");
  const auto deposited = fixture.model.backdoor_write(
      path, PackedLogic4::from_aval_bval(16, 0xabcd, 0));
  require(deposited.success() &&
              fixture.hdl.at({SystemVerilogUvmRegisterHdlKind::Vpi,
                              "sv.top.status_lane"})
                      .current.low_word()
                      .aval == 0xacd0 &&
              fixture.hdl.at({SystemVerilogUvmRegisterHdlKind::Vhpi,
                              "vhdl.top.status_high"})
                      .current.low_word()
                      .aval == 0xab,
          "backdoor deposit must preserve bits outside each selected slice");
  const auto field_path = fixture.model.register_hdl_path(
      {fixture.root,
       "GATES",
       {std::nullopt, fixture.field, std::nullopt, 0},
       {{SystemVerilogUvmRegisterHdlKind::Vpi, "sv.top.status_lane", 4, 0,
         8}}});
  require(fixture.model.backdoor_read(field_path).value.low_word().aval == 0xcd,
          "field backdoors must select their exact HDL slice");
  const auto memory_path = fixture.model.register_hdl_path(
      {fixture.root,
       "RTL",
       {std::nullopt, std::nullopt, fixture.memory, 2},
       {{SystemVerilogUvmRegisterHdlKind::Vhpi, "vhdl.top.samples_2", 0, 0,
         16}}});
  require(fixture.model
              .backdoor_write(
                  memory_path,
                  PackedLogic4::from_aval_bval(16, 0xbeef, 0))
              .success(),
          "memory-word backdoors must deposit and predict exact values");
  const std::array memory_index{std::size_t{2}};
  require(fixture.model.read(fixture.memory, memory_index).value.low_word().aval ==
              0xbeef,
          "memory-word backdoor prediction must reach sparse memory state");
  const auto forced = fixture.model.backdoor_write(
      path, PackedLogic4::from_aval_bval(16, 0x5678, 0),
      SystemVerilogUvmRegisterBackdoorKind::Force);
  require(forced.success() &&
              fixture.model.get_mirrored(fixture.reg).low_word().aval == 0x5678,
          "backdoor force must predict the forced value");
  const auto released = fixture.model.backdoor_write(
      path, PackedLogic4{0}, SystemVerilogUvmRegisterBackdoorKind::Release);
  require(released.success() && released.value.low_word().aval == 0xabcd &&
              fixture.model.hdl_path_snapshot(path).accesses == 4,
          "backdoor release must re-read and predict the deposited value");
  fixture.fail_write_path = "vhdl.top.status_high";
  require_error(
      "FSIM-UVM-REG-009",
      [&] {
        (void)fixture.model.backdoor_write(
            path, PackedLogic4::from_aval_bval(16, 0x1111, 0));
      },
      "multi-slice backdoor transport failures must be cataloged");
  fixture.fail_write_path.reset();
  require(fixture.hdl.at({SystemVerilogUvmRegisterHdlKind::Vpi,
                          "sv.top.status_lane"})
                  .current.low_word()
                  .aval == 0xacd0 &&
              fixture.hdl.at({SystemVerilogUvmRegisterHdlKind::Vhpi,
                              "vhdl.top.status_high"})
                      .current.low_word()
                      .aval == 0xab,
          "failed concatenated deposits must restore every applied slice");

  fixture.hdl.erase(
      {SystemVerilogUvmRegisterHdlKind::Vhpi, "vhdl.top.status_high"});
  require_error(
      "FSIM-UVM-REG-009", [&] { (void)fixture.model.backdoor_read(path); },
      "canonical paths must be re-resolved after relocation");
  fixture.add(SystemVerilogUvmRegisterHdlKind::Vhpi,
              "vhdl.top.status_high",
              PackedLogic4::from_aval_bval(8, 0x9a, 0));
  require(fixture.model.backdoor_read(path).value.low_word().aval == 0x9acd,
          "recreated canonical HDL paths must resolve without stale handles");

  require_error(
      "FSIM-UVM-REG-009",
      [&] {
        (void)fixture.model.register_hdl_path(
            {fixture.root,
             "overlap",
             {fixture.reg, std::nullopt, std::nullopt, 0},
             {{SystemVerilogUvmRegisterHdlKind::Vpi, "sv.top.status_lane", 0,
               0, 16},
              {SystemVerilogUvmRegisterHdlKind::Vpi, "sv.top.status_lane", 0,
               0, 16}}});
      },
      "overlapping HDL concatenation slices must reject transactionally");
}

void test_systemverilog_uvm_register_backdoor_limits() {
  SystemVerilogUvmRegisterModelLimits limits;
  limits.maximum_user_frontdoors = 1;
  limits.maximum_hdl_paths = 1;
  limits.maximum_hdl_slices = 2;
  limits.maximum_hdl_path_bytes = 64;
  limits.maximum_backdoor_operations = 1;
  BackdoorFixture fixture{limits};
  fixture.add(SystemVerilogUvmRegisterHdlKind::Vpi, "sv.top.status_lane",
              PackedLogic4::from_aval_bval(16, 0xa340, 0));
  fixture.add(SystemVerilogUvmRegisterHdlKind::Vhpi,
              "vhdl.top.status_high",
              PackedLogic4::from_aval_bval(8, 0x12, 0));
  const auto path = fixture.mixed_path();
  require_error(
      "FSIM-UVM-REG-010",
      [&] {
        (void)fixture.model.register_hdl_path(
            {fixture.root,
             "second",
             {std::nullopt, fixture.field, std::nullopt, 0},
             {{SystemVerilogUvmRegisterHdlKind::Vpi, "sv.top.status_lane", 4,
               0, 8}}});
      },
      "HDL path and slice ceilings must reject before publication");
  require(fixture.model.backdoor_read(path).success(),
          "first bounded backdoor operation must succeed");
  require_error(
      "FSIM-UVM-REG-010", [&] { (void)fixture.model.backdoor_read(path); },
      "backdoor operation ceilings must reject exactly");

  const auto callback = [](const auto &) {
    return SystemVerilogUvmRegisterOperationResult{
        SystemVerilogUvmRegisterOperationStatus::IsOk,
        PackedLogic4::from_aval_bval(16, 0, 0), false, {}};
  };
  (void)fixture.model.register_user_frontdoor(
      {fixture.root, "one", {fixture.reg, std::nullopt, std::nullopt, 0},
       callback, {}});
  require_error(
      "FSIM-UVM-REG-010",
      [&] {
        (void)fixture.model.register_user_frontdoor(
            {fixture.root, "two",
             {fixture.reg, std::nullopt, std::nullopt, 0}, callback, {}});
      },
      "user-frontdoor ceilings must reject before publication");

  SystemVerilogUvmRegisterModelLimits slice_limits;
  slice_limits.maximum_hdl_slices = 1;
  BackdoorFixture slice_bounded{slice_limits};
  slice_bounded.add(SystemVerilogUvmRegisterHdlKind::Vpi,
                    "sv.top.status_lane",
                    PackedLogic4::from_aval_bval(16, 0, 0));
  slice_bounded.add(SystemVerilogUvmRegisterHdlKind::Vhpi,
                    "vhdl.top.status_high",
                    PackedLogic4::from_aval_bval(8, 0, 0));
  require_error("FSIM-UVM-REG-010",
                [&] { (void)slice_bounded.mixed_path(); },
                "aggregate HDL slice ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits byte_limits;
  byte_limits.maximum_hdl_path_bytes = 8;
  BackdoorFixture byte_bounded{byte_limits};
  byte_bounded.add(SystemVerilogUvmRegisterHdlKind::Vpi,
                   "sv.top.status_lane",
                   PackedLogic4::from_aval_bval(16, 0, 0));
  byte_bounded.add(SystemVerilogUvmRegisterHdlKind::Vhpi,
                   "vhdl.top.status_high",
                   PackedLogic4::from_aval_bval(8, 0, 0));
  require_error("FSIM-UVM-REG-010",
                [&] { (void)byte_bounded.mixed_path(); },
                "aggregate HDL path-byte ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits value_limits;
  value_limits.maximum_backdoor_value_bits = 8;
  BackdoorFixture value_bounded{value_limits};
  value_bounded.add(SystemVerilogUvmRegisterHdlKind::Vpi,
                    "sv.top.status_lane",
                    PackedLogic4::from_aval_bval(16, 0, 0));
  value_bounded.add(SystemVerilogUvmRegisterHdlKind::Vhpi,
                    "vhdl.top.status_high",
                    PackedLogic4::from_aval_bval(8, 0, 0));
  const auto value_path = value_bounded.mixed_path();
  require_error(
      "FSIM-UVM-REG-010",
      [&] { (void)value_bounded.model.backdoor_read(value_path); },
      "backdoor value-bit ceilings must reject before transport");
}

} // namespace fsim::tests::runtime
