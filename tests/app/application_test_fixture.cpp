// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fsim::test {

namespace {

long long fsim_test_process_id() {
#if defined(_WIN32)
  return static_cast<long long>(_getpid());
#else
  return static_cast<long long>(::getpid());
#endif
}

void make_tree_writable(const std::filesystem::path& root) noexcept {
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    return;
  }
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end;
       iterator.increment(error)) {
    std::filesystem::permissions(
        iterator->path(), std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add, error);
    error.clear();
  }
  std::filesystem::permissions(
      root, std::filesystem::perms::owner_all,
      std::filesystem::perm_options::add, error);
}

// A directory no other test process uses: concurrent ctest processes may
// start in the same clock tick, so the name also carries the process id and
// a random word, and create_directory must create it anew.
std::filesystem::path unique_test_directory() {
  std::random_device entropy;
  const auto root = std::filesystem::temp_directory_path();
  for (;;) {
    const auto candidate =
        root / ("fsim-application-test-" + std::to_string(fsim_test_process_id()) + "-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "-" + std::to_string(entropy()));
    if (std::filesystem::create_directory(candidate)) {
      return candidate;
    }
  }
}

}  // namespace

ApplicationTestFixture::ApplicationTestFixture()
    : directory(unique_test_directory()) {
  create_common_sources();
  create_mixed_language_sources();
  create_systemc_sources();
}

ApplicationTestFixture::~ApplicationTestFixture() {
  make_tree_writable(directory);
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
config.source_sets.push_back(std::move(systemc_sources));
  return config;
}

}  // namespace fsim::test
