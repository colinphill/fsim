// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

void verify_vhdl2019_simulator_api(const std::filesystem::path& directory);
void verify_vhdl2019_directory_api(const std::filesystem::path& directory);
void verify_vhdl2019_environment_api(const std::filesystem::path& directory);
void verify_vhdl2019_assert_api(const std::filesystem::path& directory);
void verify_vhdl2019_governed_packages(const std::filesystem::path& directory);
