// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data,
    const std::size_t size) {
  if (size == 0) {
    return 0;
  }

  const auto language = static_cast<fsim::frontend::Language>(data[0] % 3U);
  const auto* text = reinterpret_cast<const char*>(data + 1);
  const auto result = fsim::frontend::parse_text(
      "<fuzz>", std::string_view{text, size - 1}, language);
  (void)result;
  return 0;
}
