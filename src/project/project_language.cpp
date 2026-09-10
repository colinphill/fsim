// SPDX-License-Identifier: Apache-2.0
#include "fsim/project/project.hpp"

#include "../diagnostic/artifact_identity.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace fsim::project {
namespace {

std::string lowercase(std::string_view value)
{
    std::string result { value };
    std::ranges::transform(result, result.begin(), [](const char character) {
        return static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    });
    return result;
}

} // namespace

std::string_view to_string(const Language language) noexcept {
  switch (language) {
    case Language::vhdl:
      return "vhdl";
    case Language::verilog:
        return "verilog";
    case Language::system_verilog:
        return "systemverilog";
    case Language::systemc:
        return "systemc";
    }
    return "systemverilog";
}

std::string_view to_string(const VhdlStandard standard) noexcept
{
    switch (standard) {
    case VhdlStandard::vhdl_1987:
        return "1987";
    case VhdlStandard::vhdl_1993:
        return "1993";
    case VhdlStandard::vhdl_2000:
        return "2000";
    case VhdlStandard::vhdl_2002:
        return "2002";
    case VhdlStandard::vhdl_2008:
        return "2008";
    case VhdlStandard::vhdl_2019:
        return "2019";
    }
    return "2008";
}

std::string_view to_string(const VerilogStandard standard) noexcept
{
    switch (standard) {
    case VerilogStandard::verilog_1995:
        return "1995";
    case VerilogStandard::verilog_2001:
        return "2001";
    case VerilogStandard::verilog_2001_noconfig:
        return "2001-noconfig";
    case VerilogStandard::verilog_2005:
        return "2005";
    }
    return "2005";
}

std::string_view to_string(const SystemVerilogStandard standard) noexcept
{
    switch (standard) {
    case SystemVerilogStandard::systemverilog_2005:
        return "2005";
    case SystemVerilogStandard::systemverilog_2009:
        return "2009";
    case SystemVerilogStandard::systemverilog_2012:
        return "2012";
    case SystemVerilogStandard::systemverilog_2017:
        return "2017";
    case SystemVerilogStandard::systemverilog_2023:
        return "2023";
    }
    return "2017";
}

std::string_view to_string(const Optimization optimization) noexcept
{
    switch (optimization) {
    case Optimization::o0:
        return "O0";
    case Optimization::o1:
        return "O1";
    case Optimization::o2:
        return "O2";
    case Optimization::o3:
        return "O3";
    }
    return "O2";
}

std::string_view to_string(const DelayMode mode) noexcept {
  switch (mode) {
    case DelayMode::minimum:
      return "min";
    case DelayMode::typical:
      return "typ";
    case DelayMode::maximum:
      return "max";
  }
  return "typ";
}

std::string_view to_string(const TraceFormat format) noexcept {
  switch (format) {
    case TraceFormat::automatic:
      return "auto";
    case TraceFormat::vcd:
      return "vcd";
    case TraceFormat::fst:
      return "fst";
  }
  return "auto";
}

std::string_view to_string(const TraceCompression compression) noexcept {
  switch (compression) {
    case TraceCompression::automatic:
      return "auto";
    case TraceCompression::none:
      return "none";
    case TraceCompression::deterministic:
      return "deterministic";
  }
  return "auto";
}

std::string_view to_string(const SystemVerilogUvmRelease release) noexcept {
  switch (release) {
    case SystemVerilogUvmRelease::none:
      return "none";
    case SystemVerilogUvmRelease::uvm_1_2:
      return "1.2";
    case SystemVerilogUvmRelease::ieee_1800_2_2020_3_1:
      return "2020.3.1";
  }
  return "none";
}

std::optional<Language> parse_language(const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "vhdl" || normalized == "vhdl-87"
        || normalized == "vhdl-1987" || normalized == "vhdl-93"
        || normalized == "vhdl-1993" || normalized == "vhdl-00"
        || normalized == "vhdl-2000" || normalized == "vhdl-02"
        || normalized == "vhdl-2002" || normalized == "vhdl-08"
        || normalized == "vhdl-2008" || normalized == "vhdl-19"
        || normalized == "vhdl-2019") {
        return Language::vhdl;
    }
    if (normalized == "verilog" || normalized == "v"
        || normalized == "verilog-95" || normalized == "verilog-1995"
        || normalized == "verilog-01" || normalized == "verilog-2001"
        || normalized == "verilog-2001-noconfig"
        || normalized == "verilog-05" || normalized == "verilog-2005") {
        return Language::verilog;
    }
    if (normalized == "systemverilog" || normalized == "system-verilog"
        || normalized == "sv" || normalized == "sv-05"
        || normalized == "sv-2005" || normalized == "systemverilog-2005"
        || normalized == "sv-09" || normalized == "sv-2009"
        || normalized == "systemverilog-2009" || normalized == "sv-12"
        || normalized == "sv-2012" || normalized == "systemverilog-2012"
        || normalized == "sv-17" || normalized == "sv-2017"
        || normalized == "systemverilog-2017") {
        return Language::system_verilog;
    }
    if (normalized == "systemc" || normalized == "sc") {
        return Language::systemc;
    }
    return std::nullopt;
}

std::optional<VhdlStandard> parse_vhdl_standard(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "87" || normalized == "1987"
        || normalized == "vhdl-87" || normalized == "vhdl-1987") {
        return VhdlStandard::vhdl_1987;
    }
    if (normalized == "93" || normalized == "1993"
        || normalized == "vhdl-93" || normalized == "vhdl-1993") {
        return VhdlStandard::vhdl_1993;
    }
    if (normalized == "00" || normalized == "2000"
        || normalized == "vhdl-00" || normalized == "vhdl-2000") {
        return VhdlStandard::vhdl_2000;
    }
    if (normalized == "02" || normalized == "2002"
        || normalized == "vhdl-02" || normalized == "vhdl-2002") {
        return VhdlStandard::vhdl_2002;
    }
    if (normalized == "08" || normalized == "2008"
        || normalized == "vhdl-08" || normalized == "vhdl-2008") {
        return VhdlStandard::vhdl_2008;
    }
    if (normalized == "19" || normalized == "2019"
        || normalized == "vhdl-19" || normalized == "vhdl-2019") {
        return VhdlStandard::vhdl_2019;
    }
    return std::nullopt;
}

std::optional<VerilogStandard> parse_verilog_standard(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "95" || normalized == "1995"
        || normalized == "v95" || normalized == "verilog-95"
        || normalized == "verilog-1995") {
        return VerilogStandard::verilog_1995;
    }
    if (normalized == "01" || normalized == "2001"
        || normalized == "v2001" || normalized == "verilog-01"
        || normalized == "verilog-2001") {
        return VerilogStandard::verilog_2001;
    }
    if (normalized == "2001-noconfig"
        || normalized == "v2001-noconfig"
        || normalized == "verilog-2001-noconfig") {
        return VerilogStandard::verilog_2001_noconfig;
    }
    if (normalized == "05" || normalized == "2005"
        || normalized == "v2005" || normalized == "verilog-05"
        || normalized == "verilog-2005") {
        return VerilogStandard::verilog_2005;
    }
    return std::nullopt;
}

std::optional<SystemVerilogStandard> parse_systemverilog_standard(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized == "05" || normalized == "2005"
        || normalized == "sv-05" || normalized == "sv-2005"
        || normalized == "systemverilog-2005") {
        return SystemVerilogStandard::systemverilog_2005;
    }
    if (normalized == "09" || normalized == "2009"
        || normalized == "sv-09" || normalized == "sv-2009"
        || normalized == "systemverilog-2009") {
        return SystemVerilogStandard::systemverilog_2009;
    }
    if (normalized == "12" || normalized == "2012"
        || normalized == "sv-12" || normalized == "sv-2012"
        || normalized == "systemverilog-2012") {
        return SystemVerilogStandard::systemverilog_2012;
    }
    if (normalized == "17" || normalized == "2017"
        || normalized == "sv-17" || normalized == "sv-2017"
        || normalized == "systemverilog-2017") {
        return SystemVerilogStandard::systemverilog_2017;
    }
    if (normalized == "23" || normalized == "2023"
        || normalized == "sv-23" || normalized == "sv-2023"
        || normalized == "systemverilog-2023") {
        return SystemVerilogStandard::systemverilog_2023;
    }
    return std::nullopt;
}

std::string_view default_standard(const Language language) noexcept
{
    switch (language) {
    case Language::vhdl:
        return "2008";
    case Language::verilog:
        return "2005";
    case Language::system_verilog:
        return "2017";
    case Language::systemc:
        return "2023-subset";
    }
    return "2017";
}

std::optional<std::string_view> canonical_standard(
    const Language language,
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    switch (language) {
    case Language::vhdl:
        if (const auto standard = parse_vhdl_standard(normalized)) {
            return to_string(*standard);
        }
        break;
    case Language::verilog:
        if (const auto standard = parse_verilog_standard(normalized)) {
            return to_string(*standard);
        }
        break;
    case Language::system_verilog:
        if (const auto standard = parse_systemverilog_standard(normalized)) {
            return to_string(*standard);
        }
        break;
    case Language::systemc:
        if (normalized == "2023-subset" || normalized == "2023") {
            return "2023-subset";
        }
        break;
    }
    return std::nullopt;
}

std::optional<std::string_view> parse_compatibility_switch(
    const std::string_view spelling) noexcept
{
    auto normalized = lowercase(spelling);
    std::ranges::replace(normalized, '_', '-');
    constexpr std::array switches {
        std::string_view { "keyword-profile" },
        std::string_view { "implicit-net" },
        std::string_view { "port-connection" },
        std::string_view { "sizing" },
        std::string_view { "lifetime" },
        std::string_view { "scheduler-assertion" },
        std::string_view { "configuration" },
    };
    const auto found = std::ranges::find(switches, normalized);
    return found == switches.end()
        ? std::nullopt
        : std::optional<std::string_view> { *found };
}

std::string compatibility_profile(
    const std::vector<std::string>& switches)
{
    if (switches.empty()) {
        return "none";
    }
    std::vector<std::string_view> canonical;
    canonical.reserve(switches.size());
    for (const auto& spelling : switches) {
        if (const auto parsed = parse_compatibility_switch(spelling)) {
            canonical.push_back(*parsed);
        }
    }
    constexpr std::array order {
        std::string_view { "keyword-profile" },
        std::string_view { "implicit-net" },
        std::string_view { "port-connection" },
        std::string_view { "sizing" },
        std::string_view { "lifetime" },
        std::string_view { "scheduler-assertion" },
        std::string_view { "configuration" },
    };
    std::ranges::sort(canonical, [&](const auto left, const auto right) {
        return std::ranges::find(order, left)
            < std::ranges::find(order, right);
    });
    canonical.erase(std::unique(canonical.begin(), canonical.end()), canonical.end());
    std::string result;
    for (const auto item : canonical) {
        if (!result.empty()) {
            result.push_back(',');
        }
        result.append(item);
    }
    return result.empty() ? "none" : result;
}

std::optional<SystemVerilogUvmRelease> parse_systemverilog_uvm_release(
    const std::string_view spelling) noexcept
{
    const auto normalized = lowercase(spelling);
    if (normalized.empty() || normalized == "none") {
        return SystemVerilogUvmRelease::none;
    }
    if (normalized == "1.2" || normalized == "uvm-1.2"
        || normalized == "uvm_1_2") {
        return SystemVerilogUvmRelease::uvm_1_2;
    }
    if (normalized == "2020.3.1" || normalized == "uvm-2020.3.1"
        || normalized == "uvm_2020_3_1"
        || normalized == "ieee-1800.2-2020-3.1") {
        return SystemVerilogUvmRelease::ieee_1800_2_2020_3_1;
    }
    return std::nullopt;
}

SystemVerilogUvmCompatibility systemverilog_uvm_compatibility(
    const SystemVerilogUvmRelease release) noexcept
{
    switch (release) {
    case SystemVerilogUvmRelease::uvm_1_2:
        return {
            release, 1, 2,
            true, true, true, true,
            true, true,
            false, false, false
        };
    case SystemVerilogUvmRelease::ieee_1800_2_2020_3_1:
      return {
          release, 2020, 3,
          false, false, false, false,
          true, true,
          true, true, true};
    case SystemVerilogUvmRelease::none:
      return {};
    }
  return {};
}

std::optional<DelayMode> parse_delay_mode(
    const std::string_view spelling) noexcept {
  const auto normalized = lowercase(spelling);
  if (normalized == "min" || normalized == "minimum") {
    return DelayMode::minimum;
  }
  if (normalized == "typ" || normalized == "typical") {
    return DelayMode::typical;
  }
  if (normalized == "max" || normalized == "maximum") {
    return DelayMode::maximum;
  }
  return std::nullopt;
}

std::optional<TraceFormat> parse_trace_format(
    const std::string_view spelling) noexcept {
  const auto normalized = lowercase(spelling);
  if (normalized == "auto" || normalized == "automatic") {
    return TraceFormat::automatic;
  }
  if (normalized == "vcd") {
    return TraceFormat::vcd;
  }
  if (normalized == "fst") {
    return TraceFormat::fst;
  }
  return std::nullopt;
}

std::optional<TraceCompression> parse_trace_compression(
    const std::string_view spelling) noexcept {
  const auto normalized = lowercase(spelling);
  if (normalized == "auto" || normalized == "automatic") {
    return TraceCompression::automatic;
  }
  if (normalized == "none" || normalized == "off") {
    return TraceCompression::none;
  }
  if (normalized == "deterministic" || normalized == "fixed" ||
      normalized == "fst-v1") {
    return TraceCompression::deterministic;
  }
  return std::nullopt;
}

} // namespace fsim::project
