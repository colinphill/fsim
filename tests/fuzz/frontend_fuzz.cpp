// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/preprocessor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data,
    const std::size_t size) {
  if (size == 0) {
    return 0;
  }

  const auto language = static_cast<fsim::frontend::Language>(data[0] % 3U);
  const auto* text = reinterpret_cast<const char*>(data + 1);
  fsim::frontend::ParseResult result;
  if (language == fsim::frontend::Language::Vhdl2008) {
    result = fsim::frontend::parse_text(
        "<fuzz>", std::string_view{text, size - 1}, language);
  } else {
    auto preprocessed = fsim::frontend::preprocess_verilog(
        {"<fuzz>", std::string{text, size - 1}},
        language);
    result = fsim::frontend::parse_verilog(
        std::move(preprocessed.lexed),
        language == fsim::frontend::Language::SystemVerilog2017);
  }
  (void)result;
  return 0;
}
