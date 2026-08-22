// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_point_identity.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

std::filesystem::path checkout_root(const std::string_view leaf)
{
#if defined(_WIN32)
    return std::filesystem::path { "C:/fsim-coverage" } / leaf;
#else
    return std::filesystem::path { "/fsim-coverage" } / leaf;
#endif
}

fsim::frontend::CodeCoverageSourceIdentity make_source(
    const std::filesystem::path& root, const std::filesystem::path& source,
    const std::string_view contents)
{
    auto result = fsim::frontend::make_code_coverage_source_identity(
        root, source, bytes(contents));
    require(result.ok(), "test source identity must be valid");
    return std::move(*result.identity);
}

void require_error(const fsim::frontend::CodeCoveragePointIdentityError expected,
    const fsim::frontend::CodeCoverageSourceIdentity& source,
    const fsim::frontend::CodeCoverageLanguage language,
    const fsim::frontend::CodeCoverageConstructKind construct,
    const fsim::frontend::CodeCoverageSourceSpan span,
    const std::string_view message)
{
    const auto result = fsim::frontend::make_code_coverage_point_identity(
        source, language, construct, span);
    require(!result.ok() && !result.identity.has_value()
            && result.error == expected,
        message);
}

void test_stability_and_relocation()
{
    using namespace fsim::frontend;
    constexpr std::string_view contents = "module top; assign y = a; endmodule\n";
    const auto root_a = checkout_root("checkout-a");
    const auto root_b = checkout_root("checkout-b");
    const auto source_a = make_source(root_a, root_a / "rtl/top.sv", contents);
    const auto source_b = make_source(root_b, root_b / "rtl/top.sv", contents);
    const CodeCoverageSourceSpan span { 12U, 25U };
    const auto first = make_code_coverage_point_identity(source_a,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, span);
    const auto repeated = make_code_coverage_point_identity(source_a,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, span);
    const auto relocated = make_code_coverage_point_identity(source_b,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, span);
    require(first.ok() && repeated.ok() && relocated.ok(),
        "valid source constructs must produce point identities");
    require(first.identity == repeated.identity
            && repeated.identity == relocated.identity,
        "point identity must be deterministic and relocation independent");
    require(code_coverage_point_identity_hex(*first.identity)
            == "d8adc683731fb1114e3957ecec2b508c",
        "point identity bytes must remain canonical and fully retained");
    require(kCodeCoveragePointIdentitySchema == "fsim-code-coverage-point-v1"
            && kCodeCoveragePointIdentityDiagnostic == "FSIM-COV-003",
        "point identity schema and diagnostic must remain stable");
}

void test_all_identity_inputs()
{
    using namespace fsim::frontend;
    constexpr std::string_view contents = "module top; assign y = a; endmodule\n";
    const auto root = checkout_root("checkout");
    const auto source = make_source(root, "rtl/top.sv", contents);
    const auto changed_path = make_source(root, "rtl/other.sv", contents);
    const auto changed_content = make_source(
        root, "rtl/top.sv", "module top; assign y = b; endmodule\n");
    const CodeCoverageSourceSpan span { 12U, 25U };
    const auto baseline = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, span);
    const auto language = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::SystemVerilog,
        CodeCoverageConstructKind::Statement, span);
    const auto construct = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::BranchTrueArm, span);
    const auto begin = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, { 13U, 25U });
    const auto end = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, { 12U, 26U });
    const auto path = make_code_coverage_point_identity(changed_path,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, span);
    const auto content = make_code_coverage_point_identity(changed_content,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, span);
    require(baseline.ok() && language.ok() && construct.ok() && begin.ok()
            && end.ok() && path.ok() && content.ok(),
        "every differential fixture must produce an identity");
    require(baseline.identity != language.identity
            && baseline.identity != construct.identity
            && baseline.identity != begin.identity
            && baseline.identity != end.identity
            && baseline.identity != path.identity
            && baseline.identity != content.identity,
        "language, construct, span, path, and content must each affect identity");
}

void test_allocation_order_independence()
{
    using namespace fsim::frontend;
    const auto source = make_source(checkout_root("checkout"), "rtl/top.sv",
        "module top; assign y = a; endmodule\n");
    const auto first_a = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, { 12U, 25U });
    const auto first_b = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::BranchFalseArm, { 26U, 35U });
    const auto second_b = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::BranchFalseArm, { 26U, 35U });
    const auto second_a = make_code_coverage_point_identity(source,
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, { 12U, 25U });
    require(first_a.identity == second_a.identity
            && first_b.identity == second_b.identity
            && first_a.identity != first_b.identity,
        "point identity must not depend on request or allocation order");
}

void test_rejections()
{
    using namespace fsim::frontend;
    const auto source = make_source(checkout_root("checkout"), "rtl/top.sv",
        "module top; endmodule\n");
    require_error(CodeCoveragePointIdentityError::InvalidSourceIdentity, { },
        CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, { 0U, 1U },
        "an invalid source identity must be rejected");
    auto tampered_source = source;
    ++tampered_source.content_bytes;
    require_error(CodeCoveragePointIdentityError::InvalidSourceIdentity,
        tampered_source, CodeCoverageLanguage::Verilog,
        CodeCoverageConstructKind::Statement, { 0U, 1U },
        "a source byte extent must be authenticated by its identity");
    require_error(CodeCoveragePointIdentityError::InvalidLanguage, source,
        static_cast<CodeCoverageLanguage>(255U),
        CodeCoverageConstructKind::Statement, { 0U, 1U },
        "an unknown language must be rejected");
    require_error(CodeCoveragePointIdentityError::InvalidConstructKind, source,
        CodeCoverageLanguage::Vhdl,
        static_cast<CodeCoverageConstructKind>(255U), { 0U, 1U },
        "an unknown construct kind must be rejected");
    require_error(CodeCoveragePointIdentityError::ReversedSpan, source,
        CodeCoverageLanguage::Vhdl,
        CodeCoverageConstructKind::Statement, { 4U, 3U },
        "a reversed source span must be rejected");
    require_error(CodeCoveragePointIdentityError::EmptySpan, source,
        CodeCoverageLanguage::Vhdl,
        CodeCoverageConstructKind::Statement, { 4U, 4U },
        "an empty source span must be rejected");
    require_error(CodeCoveragePointIdentityError::SpanOutsideSource, source,
        CodeCoverageLanguage::Vhdl,
        CodeCoverageConstructKind::Statement,
        { 0U, static_cast<std::uint64_t>(source.content_bytes) + 1U },
        "a span beyond the identified content must be rejected");
}

void test_typed_names()
{
    using namespace fsim::frontend;
    require(code_coverage_language_name(CodeCoverageLanguage::Verilog)
                == "verilog"
            && code_coverage_language_name(CodeCoverageLanguage::SystemVerilog)
                == "systemverilog"
            && code_coverage_language_name(CodeCoverageLanguage::Vhdl)
                == "vhdl",
        "language identities must remain explicit and stable");
    require(code_coverage_construct_kind_name(
                CodeCoverageConstructKind::BranchImplicitArm)
                == "branch-implicit"
            && code_coverage_construct_kind_name(
                CodeCoverageConstructKind::AtomicCondition)
                == "atomic-condition"
            && code_coverage_construct_kind_name(
                CodeCoverageConstructKind::ToggleObject)
                == "toggle-object"
            && code_coverage_construct_kind_name(
                CodeCoverageConstructKind::FsmCurrentStateObject)
                == "fsm-current-state-object"
            && code_coverage_construct_kind_name(
                CodeCoverageConstructKind::FsmNextStateObject)
                == "fsm-next-state-object"
            && code_coverage_construct_kind_name(
                static_cast<CodeCoverageConstructKind>(255U))
                .empty(),
        "construct-kind identities must remain explicit and bounded");
}

} // namespace

int main()
{
    test_stability_and_relocation();
    test_all_identity_inputs();
    test_allocation_order_independence();
    test_rejections();
    test_typed_names();
    return 0;
}
