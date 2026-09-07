// SPDX-License-Identifier: Apache-2.0

#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/veriuser.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using Probe = PLI_INT32 (*)(void);

constexpr std::array standard_acc_symbols{
    "acc_append_delays",       "acc_append_pulsere",
    "acc_close",               "acc_collect",
    "acc_compare_handles",     "acc_configure",
    "acc_count",               "acc_fetch_argc",
    "acc_fetch_argv",          "acc_fetch_attribute",
    "acc_fetch_attribute_int", "acc_fetch_attribute_str",
    "acc_fetch_defname",       "acc_fetch_delay_mode",
    "acc_fetch_delays",        "acc_fetch_direction",
    "acc_fetch_edge",          "acc_fetch_fullname",
    "acc_fetch_fulltype",      "acc_fetch_index",
    "acc_fetch_itfarg",        "acc_fetch_itfarg_int",
    "acc_fetch_itfarg_str",    "acc_fetch_location",
    "acc_fetch_name",          "acc_fetch_paramtype",
    "acc_fetch_paramval",      "acc_fetch_polarity",
    "acc_fetch_precision",     "acc_fetch_pulsere",
    "acc_fetch_range",         "acc_fetch_size",
    "acc_fetch_tfarg",         "acc_fetch_tfarg_int",
    "acc_fetch_tfarg_str",     "acc_fetch_timescale_info",
    "acc_fetch_type",          "acc_fetch_type_str",
    "acc_fetch_value",         "acc_free",
    "acc_handle_by_name",      "acc_handle_condition",
    "acc_handle_conn",         "acc_handle_datapath",
    "acc_handle_hiconn",       "acc_handle_interactive_scope",
    "acc_handle_itfarg",       "acc_handle_loconn",
    "acc_handle_modpath",      "acc_handle_notifier",
    "acc_handle_object",       "acc_handle_parent",
    "acc_handle_path",         "acc_handle_pathin",
    "acc_handle_pathout",      "acc_handle_port",
    "acc_handle_scope",        "acc_handle_simulated_net",
    "acc_handle_tchk",         "acc_handle_tchkarg1",
    "acc_handle_tchkarg2",     "acc_handle_terminal",
    "acc_handle_tfarg",        "acc_handle_tfinst",
    "acc_initialize",          "acc_next",
    "acc_next_bit",            "acc_next_cell",
    "acc_next_cell_load",      "acc_next_child",
    "acc_next_driver",         "acc_next_hiconn",
    "acc_next_input",          "acc_next_load",
    "acc_next_loconn",         "acc_next_modpath",
    "acc_next_net",            "acc_next_output",
    "acc_next_parameter",      "acc_next_port",
    "acc_next_portout",        "acc_next_primitive",
    "acc_next_scope",          "acc_next_specparam",
    "acc_next_tchk",           "acc_next_terminal",
    "acc_next_topmod",         "acc_object_in_typelist",
    "acc_object_of_type",      "acc_product_type",
    "acc_product_version",     "acc_release_object",
    "acc_replace_delays",      "acc_replace_pulsere",
    "acc_reset_buffer",        "acc_set_interactive_scope",
    "acc_set_pulsere",         "acc_set_scope",
    "acc_set_value",           "acc_vcl_add",
    "acc_vcl_delete",          "acc_version",
};

static_assert(standard_acc_symbols.size() == 102U);

bool require(const bool condition, const char* const message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
std::filesystem::path cached_path(const std::filesystem::path& source,
                                  const std::string_view language) {
  return source.parent_path() /
         (source.stem().string() + "_cached_" + std::string{language} +
          source.extension().string());
}

bool run_probe(fsim::platform::DynamicLibrary& library,
               const std::string_view symbol, const PLI_INT32 expected) {
  std::string error;
  const auto probe =
      reinterpret_cast<Probe>(library.symbol(symbol, error));
  return require(probe != nullptr && error.empty(),
                 "ACC plug-in probe symbol is missing") &&
         require(probe() == expected,
                 "ACC plug-in probe did not execute through the public ABI");
}

bool copy_artifact(const std::filesystem::path& source,
                   const std::filesystem::path& destination) {
  std::error_code error;
  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::copy_file(source, destination,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
  return require(!error, "ACC plug-in cache artifact could not be staged");
}

}  // namespace

int main() {
  const std::filesystem::path link_path{FSIM_ACC_LINK_LIBRARY_PATH};
  const std::filesystem::path c_path{FSIM_ACC_C_PLUGIN_PATH};
  const std::filesystem::path cpp_path{FSIM_ACC_CPP_PLUGIN_PATH};
  if (!require(std::filesystem::exists(link_path),
               "ACC shared/import link surface is missing") ||
      !require(std::filesystem::exists(c_path),
               "C ACC probe image is missing") ||
      !require(std::filesystem::exists(cpp_path),
               "C++ ACC probe image is missing")) {
    return 1;
  }

  std::string error;
  auto link = fsim::platform::DynamicLibrary::open(link_path, error);
  if (!require(link != nullptr && error.empty(),
               "ACC shared/import link surface did not load")) {
    return 1;
  }
  for (const char* const symbol : standard_acc_symbols) {
    if (!require(link->symbol(symbol, error) != nullptr && error.empty(),
                 "ACC link surface does not export every standard routine")) {
      return 1;
    }
  }
  if (!require(link->symbol("acc_error_flag", error) != nullptr &&
                   error.empty(),
               "ACC link surface does not export the standard error flag")) {
    return 1;
  }
  for (const char* const forbidden : {
           "acc_vendor_fast_handle", "acc_handle_by_name_fast",
           "acc_mti_intercept", "acc_vpi_printf"}) {
    if (!require(link->symbol(forbidden, error) == nullptr && !error.empty(),
                 "ACC link surface exports a vendor extension")) {
      return 1;
    }
  }

  if (!require(!fsim::platform::DynamicLibrary::is_loaded(c_path) &&
                   !fsim::platform::DynamicLibrary::is_loaded(cpp_path),
               "ACC probe images start unloaded")) {
    return 1;
  }
  auto c_library = fsim::platform::DynamicLibrary::open(c_path, error);
  auto cpp_library = fsim::platform::DynamicLibrary::open(cpp_path, error);
  if (!require(c_library != nullptr && cpp_library != nullptr && error.empty(),
               "independent C and C++ ACC images did not load") ||
      !require(fsim::platform::DynamicLibrary::is_loaded(c_path) &&
                   fsim::platform::DynamicLibrary::is_loaded(cpp_path),
               "ACC loader did not retain both native images") ||
      !run_probe(*c_library, "fsim_acc_link_probe", 182) ||
      !run_probe(*cpp_library, "fsim_acc_cpp_probe", 183)) {
    return 1;
  }
  c_library.reset();
  cpp_library.reset();
  if (!require(!fsim::platform::DynamicLibrary::is_loaded(c_path) &&
                   !fsim::platform::DynamicLibrary::is_loaded(cpp_path),
               "ACC images did not unload after their final owner")) {
    return 1;
  }

  const auto c_cache = cached_path(c_path, "c");
  const auto cpp_cache = cached_path(cpp_path, "cpp");
  if (!copy_artifact(c_path, c_cache) || !copy_artifact(cpp_path, cpp_cache)) {
    return 1;
  }
  auto c_cached = fsim::platform::DynamicLibrary::open(c_cache, error);
  auto cpp_cached = fsim::platform::DynamicLibrary::open(cpp_cache, error);
  const bool cache_ok =
      require(c_cached != nullptr && cpp_cached != nullptr && error.empty(),
              "cached ACC plug-in artifacts did not load") &&
      run_probe(*c_cached, "fsim_acc_link_probe", 182) &&
      run_probe(*cpp_cached, "fsim_acc_cpp_probe", 183);
  c_cached.reset();
  cpp_cached.reset();
  const bool unloaded =
      require(!fsim::platform::DynamicLibrary::is_loaded(c_cache) &&
                  !fsim::platform::DynamicLibrary::is_loaded(cpp_cache),
              "cached ACC images did not unload after their final owner");
  std::error_code cleanup_error;
  const bool c_removed = std::filesystem::remove(c_cache, cleanup_error);
  cleanup_error.clear();
  const bool cpp_removed = std::filesystem::remove(cpp_cache, cleanup_error);
  return cache_ok && unloaded &&
                 require(c_removed && cpp_removed && !cleanup_error,
                         "ACC plug-in cache artifacts were not cleaned up")
             ? 0
             : 1;
}
