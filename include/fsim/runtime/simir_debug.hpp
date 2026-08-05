// SPDX-License-Identifier: Apache-2.0
#pragma once

struct DebugLocal {
  std::string name;
  std::string type_name;
  RegisterId register_id{};
  std::size_t width{};
  SourceLocation source;
  std::optional<std::int32_t> integer_lower;
  std::optional<std::int32_t> integer_upper;
  ValueKind value_kind{ValueKind::logic4};
  std::vector<std::string> enumeration_literals;
  SystemVerilogScalarKind systemverilog_scalar{
      SystemVerilogScalarKind::None};
};

struct DebugStringLocal {
  std::string name;
  StringRegisterId register_id{};
  SourceLocation source;
};

struct DebugContainerLocal {
  std::string name;
  ContainerRegisterId register_id{};
  ContainerType type;
  SourceLocation source;
};
