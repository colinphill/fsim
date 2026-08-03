// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <chrono>
#include <filesystem>
#include <utility>

namespace fsim::test {

ApplicationTestFixture::ApplicationTestFixture()
    : directory(
          std::filesystem::temp_directory_path()
          / ("fsim-application-test-"
             + std::to_string(
                 std::chrono::steady_clock::now()
                     .time_since_epoch()
                     .count()))) {
  std::filesystem::create_directories(directory);
  create_common_sources();
  create_mixed_language_sources();
  create_systemc_sources();
}

ApplicationTestFixture::~ApplicationTestFixture() {
  std::error_code error;
  std::filesystem::remove_all(directory, error);
}

project::Config ApplicationTestFixture::base_config() const {
fsim::project::Config config;
config.base_directory = directory;
config.project.name = "application-test";
config.project.top = "sv:work.tb";
config.project.time_resolution = "1ns";
config.build.cache_path = directory / "cache";
config.run.max_deltas = 1000;
fsim::project::SourceSet sources;
sources.language = fsim::project::Language::system_verilog;
sources.standard = "2017";
sources.library = "work";
sources.files.push_back(source);
config.source_sets.push_back(std::move(sources));
fsim::project::SourceSet systemc_sources;
systemc_sources.language = fsim::project::Language::systemc;
systemc_sources.standard = "2023-subset";
systemc_sources.library = "models";
systemc_sources.files.push_back(systemc_source);
systemc_sources.include_directories.emplace_back(
    std::filesystem::path{FSIM_TEST_SOURCE_DIR} / "include");
config.source_sets.push_back(std::move(systemc_sources));
  return config;
}

}  // namespace fsim::test
