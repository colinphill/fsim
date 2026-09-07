// SPDX-License-Identifier: Apache-2.0

#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/veriuser.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

using LinkProbe = PLI_INT32(FSIM_NATIVE_PLUGIN_CALL*)(void);

constexpr const char* standard_tf_symbols[] = {
    "io_mcdprintf", "io_printf", "mc_scan_plusargs", "tf_add_long",
    "tf_asynchoff", "tf_asynchon", "tf_clearalldelays", "tf_compare_long",
    "tf_copypvc_flag", "tf_divide_long", "tf_dofinish", "tf_dostop",
    "tf_error", "tf_evaluatep", "tf_exprinfo", "tf_getcstringp",
    "tf_getinstance", "tf_getlongp", "tf_getlongtime",
    "tf_getnextlongtime", "tf_getp", "tf_getpchange", "tf_getrealp",
    "tf_getrealtime", "tf_getroutine", "tf_gettflist", "tf_gettime",
    "tf_gettimeprecision", "tf_gettimeunit", "tf_getworkarea",
    "tf_iasynchoff", "tf_iasynchon", "tf_iclearalldelays",
    "tf_icopypvc_flag", "tf_ievaluatep", "tf_iexprinfo",
    "tf_igetcstringp", "tf_igetlongp", "tf_igetlongtime", "tf_igetp",
    "tf_igetpchange", "tf_igetrealp", "tf_igetrealtime", "tf_igetroutine",
    "tf_igettime", "tf_igettimeprecision", "tf_igettimeunit",
    "tf_igetworkarea", "tf_imipname", "tf_imovepvc_flag", "tf_inodeinfo",
    "tf_inump", "tf_ipropagatep", "tf_iputlongp", "tf_iputp",
    "tf_iputrealp", "tf_irosynchronize", "tf_isetdelay",
    "tf_isetlongdelay", "tf_isetrealdelay", "tf_isetworkarea",
    "tf_isizep", "tf_ispname", "tf_istrdelputp", "tf_istrgetp",
    "tf_istrlongdelputp", "tf_istrrealdelputp", "tf_isynchronize",
    "tf_itestpvc_flag", "tf_itypep", "tf_long_to_real",
    "tf_longtime_tostr", "tf_message", "tf_mipname", "tf_movepvc_flag",
    "tf_multiply_long", "tf_nodeinfo", "tf_nump", "tf_propagatep",
    "tf_putlongp", "tf_putp", "tf_putrealp", "tf_read_restart",
    "tf_real_to_long", "tf_rosynchronize", "tf_scale_longdelay",
    "tf_scale_realdelay", "tf_setdelay", "tf_setlongdelay",
    "tf_setrealdelay", "tf_setworkarea", "tf_sizep", "tf_spname",
    "tf_strdelputp", "tf_strgetp", "tf_strgettime", "tf_strlongdelputp",
    "tf_strrealdelputp", "tf_subtract_long", "tf_synchronize",
    "tf_testpvc_flag", "tf_text", "tf_typep", "tf_unscale_longdelay",
    "tf_unscale_realdelay", "tf_warning", "tf_write_save",
};

static_assert(sizeof(standard_tf_symbols) / sizeof(standard_tf_symbols[0]) ==
              107);

bool require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  const std::filesystem::path link_library_path{FSIM_TF_LINK_LIBRARY_PATH};
  const std::filesystem::path plugin_path{FSIM_TF_LINK_PROBE_PLUGIN_PATH};
  if (!require(std::filesystem::exists(link_library_path),
               "TF link library is missing") ||
      !require(std::filesystem::exists(plugin_path),
               "TF link probe image is missing")) {
    return 1;
  }

  std::string error;
  auto link_library =
      fsim::platform::DynamicLibrary::open(link_library_path, error);
  if (!require(link_library != nullptr && error.empty(),
               "TF link library did not load")) {
    return 1;
  }
  for (const char* const symbol : standard_tf_symbols) {
    if (!require(link_library->symbol(symbol, error) != nullptr &&
                     error.empty(),
                 "TF link library does not export every standard routine")) {
      return 1;
    }
  }
  for (const char* const forbidden : {
           "err_intercept", "veriuser_version_str", "endofcompile_routines",
           "vpi_printf"}) {
    if (!require(link_library->symbol(forbidden, error) == nullptr &&
                     !error.empty(),
                 "TF link library exports a plugin-owned or vendor symbol")) {
      return 1;
    }
  }

  error.clear();
  auto library = fsim::platform::DynamicLibrary::open(plugin_path, error);
  if (!require(library != nullptr && error.empty(),
               "TF link probe image did not load")) {
    return 1;
  }

  const auto descriptor_function =
      reinterpret_cast<fsim_native_plugin_descriptor_v3_get_fn>(
          library->symbol(FSIM_NATIVE_PLUGIN_DESCRIPTOR_SYMBOL, error));
  if (!require(descriptor_function != nullptr && error.empty(),
               "TF link probe lost its v3 descriptor")) {
    return 1;
  }
  const auto* descriptor = descriptor_function();
  if (!require(descriptor != nullptr &&
                   descriptor->abi_version == FSIM_NATIVE_PLUGIN_ABI_VERSION &&
                   descriptor->struct_size >= sizeof(*descriptor) &&
                   descriptor->pointer_bits == sizeof(void*) * 8u &&
                   descriptor->capabilities ==
                       FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
               "TF link probe descriptor is invalid")) {
    return 1;
  }

  const auto probe = reinterpret_cast<LinkProbe>(
      library->symbol("fsim_tf_link_probe", error));
  if (!require(probe != nullptr && error.empty(),
               "TF link probe symbol is missing")) {
    return 1;
  }
  return require(probe() == 1,
                 "TF shared/import link surface did not resolve")
             ? 0
             : 1;
}
