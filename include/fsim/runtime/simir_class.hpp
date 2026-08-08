// SPDX-License-Identifier: Apache-2.0
#pragma once

struct ClassAllocate {
  RegisterId destination{};
  std::string specialization_identity;
  std::string declared_type;
  std::vector<RegisterId> constructor_actuals;
  std::vector<std::uint8_t> constructor_actual_kinds;
  std::vector<std::string> constructor_actual_names;
};

struct ClassPropertyRead {
  RegisterId destination{};
  RegisterId receiver{};
  std::string property_identity;
  std::uint32_t width{};
};

struct ClassPropertyWrite {
  RegisterId receiver{};
  RegisterId source{};
  std::string property_identity;
};

struct ClassMethodCall {
  RegisterId destination{};
  RegisterId receiver{};
  std::string method_identity;
  std::vector<RegisterId> actuals;
  std::vector<std::string> actual_names;
  std::vector<std::uint8_t> actual_directions;
  std::uint32_t result_width{};
  bool virtual_dispatch{true};
  std::vector<std::uint8_t> actual_kinds{};
};

struct ClassStaticPropertyRead {
  RegisterId destination{};
  std::string property_identity;
  std::uint32_t width{};
};

struct ClassStaticPropertyWrite {
  RegisterId source{};
  std::string property_identity;
};

struct ClassStaticMethodCall {
  RegisterId destination{};
  std::string method_identity;
  std::vector<RegisterId> actuals;
  std::vector<std::string> actual_names;
  std::vector<std::uint8_t> actual_directions;
  std::uint32_t result_width{};
  std::vector<std::uint8_t> actual_kinds{};
};
