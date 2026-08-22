// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_source_identity.hpp"

#include "fsim/support/path.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

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
    return fsim::support::path_from_utf8("C:/fsim-coverage/") / leaf;
#else
    return fsim::support::path_from_utf8("/fsim-coverage/") / leaf;
#endif
}

void require_error(const fsim::frontend::CodeCoverageSourceIdentityError expected,
    const std::filesystem::path& root,
    const std::filesystem::path& source, const std::string_view contents,
    const std::string_view message,
    const fsim::frontend::CodeCoverageSourceIdentityLimits limits = { })
{
    const auto result = fsim::frontend::make_code_coverage_source_identity(
        root, source, bytes(contents), limits);
    require(!result.ok() && !result.identity.has_value()
            && result.error == expected,
        message);
}

void test_relocation_and_normalization()
{
    using namespace fsim::frontend;
    const auto root_a = checkout_root("checkout-a");
    const auto root_b = checkout_root("checkout-b");
    constexpr std::string_view contents = "module top; endmodule\n";

    const auto first = make_code_coverage_source_identity(root_a,
        root_a / "rtl" / "." / "generated" / ".." / "top.sv",
        bytes(contents));
    const auto relocated = make_code_coverage_source_identity(
        root_b, root_b / "rtl" / "top.sv", bytes(contents));
    const auto relative = make_code_coverage_source_identity(
        root_b, "rtl/top.sv", bytes(contents));
    require(first.ok() && relocated.ok() && relative.ok(),
        "valid absolute and relative source paths must produce identities");
    require(first.identity == relocated.identity
            && relocated.identity == relative.identity,
        "checkout relocation and lexical spelling must not change identity");
    require(first.identity->logical_path == "rtl/top.sv",
        "the logical source path must be normalized and checkout relative");
    require(first.identity->content_bytes == contents.size(),
        "the source identity must retain its exact byte extent");
    require(kCodeCoverageSourceIdentitySchema
                == "fsim-code-coverage-source-v1"
            && kCodeCoverageSourceIdentityDiagnostic == "FSIM-COV-002",
        "source identity schema and diagnostic must remain stable");
    require(code_coverage_source_content_hex(*first.identity)
            == "bbfca2afc8562f8675a4e3f474a685b4f47d7728ae08df9a2f1a2a8bb77826e7",
        "source content must retain its exact SHA-256 identity");
    require(code_coverage_source_identity_hex(*first.identity).size() == 64U,
        "the composite source identity must retain all SHA-256 bits");
}

void test_path_and_content_distinctions()
{
    using namespace fsim::frontend;
    const auto root = checkout_root("checkout");
    const auto baseline = make_code_coverage_source_identity(
        root, "rtl/top.sv", bytes("module top; endmodule\n"));
    const auto changed_path = make_code_coverage_source_identity(
        root, "verification/top.sv", bytes("module top; endmodule\n"));
    const auto changed_content = make_code_coverage_source_identity(
        root, "rtl/top.sv", bytes("module top; wire changed; endmodule\n"));
    const auto empty_content = make_code_coverage_source_identity(
        root, "rtl/top.sv", bytes(""));
    require(baseline.ok() && changed_path.ok() && changed_content.ok()
            && empty_content.ok(),
        "distinct valid sources must produce identities");
    require(baseline.identity->content_digest
            == changed_path.identity->content_digest,
        "path spelling must not contaminate content identity");
    require(baseline.identity->digest != changed_path.identity->digest,
        "distinct logical paths must not alias");
    require(baseline.identity->content_digest
            != changed_content.identity->content_digest,
        "distinct source content must not alias");
    require(baseline.identity->digest != changed_content.identity->digest
            && baseline.identity->digest != empty_content.identity->digest,
        "the composite identity must retain every content distinction");
}

void test_path_rejections()
{
    using namespace fsim::frontend;
    const auto root = checkout_root("checkout");
    require_error(CodeCoverageSourceIdentityError::CheckoutRootRequired, { },
        "rtl/top.sv", "x", "an empty checkout root must be rejected");
    require_error(CodeCoverageSourceIdentityError::SourcePathRequired, root,
        { }, "x", "an empty source path must be rejected");
    require_error(CodeCoverageSourceIdentityError::SourceOutsideCheckout, root,
        root, "x", "the checkout directory is not a source identity");
    require_error(CodeCoverageSourceIdentityError::SourceOutsideCheckout, root,
        root / ".." / "outside.sv", "x",
        "a lexical traversal outside the checkout must be rejected");
    require_error(CodeCoverageSourceIdentityError::SourceOutsideCheckout, root,
        checkout_root("other") / "top.sv", "x",
        "an absolute source from another checkout must be rejected");

    const std::string embedded_nul { "rtl/\0top.sv", 11U };
    require_error(CodeCoverageSourceIdentityError::InvalidPathEncoding, root,
        fsim::support::path_from_utf8(embedded_nul), "x",
        "an embedded NUL must not enter a logical source identity");
#if !defined(_WIN32)
    std::string invalid_utf8 { "rtl/" };
    invalid_utf8.push_back(static_cast<char>(0xc0U));
    invalid_utf8.push_back(static_cast<char>(0xafU));
    invalid_utf8.append("top.sv");
    require_error(CodeCoverageSourceIdentityError::InvalidPathEncoding, root,
        std::filesystem::path { invalid_utf8 }, "x",
        "non-UTF-8 path bytes must not enter a portable source identity");
#endif
}

void test_resource_rejections()
{
    using namespace fsim::frontend;
    const auto root = checkout_root("checkout");
    require_error(CodeCoverageSourceIdentityError::PathLimit, root,
        "rtl/top.sv", "x", "logical path bytes must be bounded", { 9U, 1U });
    require_error(CodeCoverageSourceIdentityError::ContentLimit, root,
        "top.sv", "xy", "source content bytes must be bounded", { 6U, 1U });
}

} // namespace

int main()
{
    test_relocation_and_normalization();
    test_path_and_content_distinctions();
    test_path_rejections();
    test_resource_rejections();
    return 0;
}
