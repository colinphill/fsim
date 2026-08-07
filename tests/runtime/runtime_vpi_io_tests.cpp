// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_io.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {

namespace {

using namespace fsim::runtime;

void require_vpi_io(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct VpiIoRoot {
  std::filesystem::path path;

  VpiIoRoot() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    path = std::filesystem::temp_directory_path()
        / ("fsim-vpi-io-" + std::to_string(nonce));
    std::error_code error;
    require_vpi_io(
        std::filesystem::create_directories(path, error) && !error,
        "VPI I/O temporary root creation failed");
  }

  ~VpiIoRoot() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

std::string read_vpi_io_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

SystemVerilogVpiIoConfiguration io_configuration(
    const std::uint64_t identity,
    const std::filesystem::path& root,
    std::string& output,
    std::vector<SystemVerilogVpiIoDiagnostic>& diagnostics) {
  SystemVerilogVpiIoConfiguration configuration;
  configuration.simulation_identity = identity;
  configuration.file_root = root;
  configuration.arguments = {"fsim", "+seed=7", "--trace"};
  configuration.product = "fsim-test";
  configuration.output = [&](const std::string_view text) {
    output.append(text);
  };
  configuration.diagnostic =
      [&](const SystemVerilogVpiIoDiagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
      };
  return configuration;
}

}  // namespace

void test_systemverilog_vpi_io_descriptors() {
  VpiIoRoot root;
  std::string output;
  std::vector<SystemVerilogVpiIoDiagnostic> diagnostics;
  SystemVerilogVpiIoService io{
      io_configuration(920, root.path, output, diagnostics)};
  const auto stdout_handle = io.standard_output();
  const auto first = io.open_mcd("first.log");
  const auto second = io.open_mcd("second.log");
  const auto descriptor =
      io.open_file_descriptor("descriptor.log", "w");
  require_vpi_io(
      io.valid() && io.simulation_identity() == 920
          && stdout_handle.value == 1U
          && first && second && descriptor
          && first.value.value == 2U
          && second.value.value == 4U
          && descriptor.value.value == UINT32_C(0x80000001)
          && io.open_files() == 3,
      "VPI I/O did not allocate deterministic cross-platform MCD/FD values");

  const std::array channels{
      stdout_handle, first.value, second.value};
  const auto combined = io.combine_mcd(channels);
  SystemVerilogVpiIoDescriptorKind stdout_kind{};
  SystemVerilogVpiIoDescriptorKind mcd_kind{};
  SystemVerilogVpiIoDescriptorKind descriptor_kind{};
  require_vpi_io(
      combined && combined.value.value == 7U
          && io.descriptor_kind(stdout_handle, stdout_kind)
              == SystemVerilogVpiIoError::None
          && io.descriptor_kind(combined.value, mcd_kind)
              == SystemVerilogVpiIoError::None
          && io.descriptor_kind(descriptor.value, descriptor_kind)
              == SystemVerilogVpiIoError::None
          && stdout_kind
              == SystemVerilogVpiIoDescriptorKind::StandardOutput
          && mcd_kind
              == SystemVerilogVpiIoDescriptorKind::Multichannel
          && descriptor_kind
              == SystemVerilogVpiIoDescriptorKind::FileDescriptor,
      "VPI I/O descriptor classification lost the fixed 32-bit encoding");

  require_vpi_io(
      io.write(combined.value, "fanout")
              == SystemVerilogVpiIoError::None
          && io.vlog("-vlog") == SystemVerilogVpiIoError::None
          && io.write(descriptor.value, "fd")
              == SystemVerilogVpiIoError::None
          && io.flush(combined.value)
              == SystemVerilogVpiIoError::None
          && io.flush(descriptor.value)
              == SystemVerilogVpiIoError::None
          && output == "fanout-vlog",
      "VPI I/O MCD/vlog/FD routing or flushing failed");

  require_vpi_io(
      io.close(combined.value) == SystemVerilogVpiIoError::None
          && io.open_files() == 1
          && read_vpi_io_file(root.path / "first.log") == "fanout"
          && read_vpi_io_file(root.path / "second.log") == "fanout"
          && io.write(first.value, "stale")
              == SystemVerilogVpiIoError::StaleHandle,
      "VPI I/O composite close did not release every selected MCD channel");

  require_vpi_io(
      io.close(descriptor.value) == SystemVerilogVpiIoError::None
          && read_vpi_io_file(root.path / "descriptor.log") == "fd",
      "VPI I/O file descriptor close lost owned output");
  const auto appended =
      io.open_file_descriptor("descriptor.log", "a");
  require_vpi_io(
      appended
          && appended.value.value == UINT32_C(0x80000002)
          && io.write(appended.value, "+append")
              == SystemVerilogVpiIoError::None
          && io.close(appended.value)
              == SystemVerilogVpiIoError::None
          && read_vpi_io_file(root.path / "descriptor.log")
              == "fd+append",
      "VPI I/O append mode or monotonic descriptor identity failed");

  std::string foreign_output;
  std::vector<SystemVerilogVpiIoDiagnostic> foreign_diagnostics;
  SystemVerilogVpiIoService foreign{
      io_configuration(
          921, root.path, foreign_output, foreign_diagnostics)};
  require_vpi_io(
      foreign.write(stdout_handle, "cross")
              == SystemVerilogVpiIoError::CrossService
          && foreign.close(descriptor.value)
              == SystemVerilogVpiIoError::CrossService
          && foreign.combine_mcd(channels).error
              == SystemVerilogVpiIoError::CrossService,
      "VPI I/O handles crossed simulation-service ownership");

  require_vpi_io(
      io.arguments()
              == std::vector<std::string>{
                  "fsim", "+seed=7", "--trace"}
          && io.product() == "fsim-test"
          && io.version() == "0.1.0-dev",
      "VPI I/O argv or canonical product/version identity changed");
}

void test_systemverilog_vpi_io_diagnostics_and_teardown() {
  VpiIoRoot root;
  std::string output;
  std::vector<SystemVerilogVpiIoDiagnostic> diagnostics;
  SystemVerilogVpiIoService io{
      io_configuration(922, root.path, output, diagnostics)};
  const std::array<std::string_view, 2> format_arguments{
      "compile", "17"};
  for (const auto severity : {
           SystemVerilogVpiIoSeverity::Note,
           SystemVerilogVpiIoSeverity::Warning,
           SystemVerilogVpiIoSeverity::Error,
           SystemVerilogVpiIoSeverity::Fatal}) {
    require_vpi_io(
        io.report(
            severity, "phase {} {{ok}} {}", format_arguments)
            == SystemVerilogVpiIoError::None,
        "VPI I/O formatted severity routing failed");
  }
  require_vpi_io(
      diagnostics.size() == 4
          && diagnostics[0].severity
              == SystemVerilogVpiIoSeverity::Note
          && diagnostics[1].severity
              == SystemVerilogVpiIoSeverity::Warning
          && diagnostics[2].severity
              == SystemVerilogVpiIoSeverity::Error
          && diagnostics[3].severity
              == SystemVerilogVpiIoSeverity::Fatal
          && diagnostics[0].message == "phase compile {ok} 17",
      "VPI I/O diagnostics lost exact severity or bounded formatting");

  const std::array<std::string_view, 1> one_argument{"one"};
  const std::string oversized(4'097, 'x');
  require_vpi_io(
      io.report(
          SystemVerilogVpiIoSeverity::Note,
          "{} {}",
          one_argument) == SystemVerilogVpiIoError::InvalidFormat
          && io.report(
                 SystemVerilogVpiIoSeverity::Note,
                 "plain",
                 one_argument)
              == SystemVerilogVpiIoError::InvalidFormat
          && io.report(
                 static_cast<SystemVerilogVpiIoSeverity>(99), "bad")
              == SystemVerilogVpiIoError::InvalidSeverity
          && io.vlog(oversized)
              == SystemVerilogVpiIoError::StringTooLong
          && io.open_mcd("../escape.log").error
              == SystemVerilogVpiIoError::InvalidPath
          && io.open_file_descriptor("bad.log", "r").error
              == SystemVerilogVpiIoError::InvalidMode,
      "VPI I/O malformed format, severity, path, mode, or bound was accepted");

  const auto owned = io.open_mcd("owned.log");
  require_vpi_io(
      owned && io.write(owned.value, "owned")
          == SystemVerilogVpiIoError::None,
      "VPI I/O teardown fixture write failed");
  io.teardown();
  io.teardown();
  require_vpi_io(
      !io.valid() && io.open_files() == 0
          && read_vpi_io_file(root.path / "owned.log") == "owned"
          && io.write(owned.value, "closed")
              == SystemVerilogVpiIoError::Closed
          && io.close(owned.value)
              == SystemVerilogVpiIoError::Closed
          && io.vlog("closed") == SystemVerilogVpiIoError::Closed
          && io.open_mcd("after.log").error
              == SystemVerilogVpiIoError::Closed,
      "VPI I/O deterministic teardown did not close ownership and all surfaces");

  SystemVerilogVpiIoConfiguration invalid;
  invalid.simulation_identity = 0;
  SystemVerilogVpiIoService invalid_service{std::move(invalid)};
  require_vpi_io(
      !invalid_service.valid()
          && invalid_service.configuration_error()
              == SystemVerilogVpiIoError::InvalidService,
      "VPI I/O accepted a zero simulation identity");

  SystemVerilogVpiIoConfiguration throwing_output;
  throwing_output.simulation_identity = 923;
  throwing_output.file_root = root.path;
  throwing_output.output = [](std::string_view) {
    throw std::runtime_error("output sink");
  };
  throwing_output.diagnostic = [](const auto&) {
    throw std::runtime_error("diagnostic sink");
  };
  SystemVerilogVpiIoService throwing{std::move(throwing_output)};
  require_vpi_io(
      throwing.vlog("contained")
              == SystemVerilogVpiIoError::SinkFailed
          && throwing.report(
                 SystemVerilogVpiIoSeverity::Error, "contained")
              == SystemVerilogVpiIoError::SinkFailed,
      "VPI I/O output or diagnostic sink exception escaped its boundary");
}

}  // namespace fsim::tests::runtime
