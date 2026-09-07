// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/support/path.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace fsim;

artifact::CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 41U };
}

artifact::CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    artifact::CoverageDatabaseDigest result;
    for (std::size_t index = 0U; index < result.size(); ++index)
        result[index] = static_cast<std::byte>(seed + index);
    return result;
}

artifact::CoverageDatabaseContents database(const std::uint64_t run,
    const std::uint8_t fingerprint_seed = 40U)
{
    using namespace artifact;
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(fingerprint_seed);
    contents.sources = {
        { id(1U), "rtl/design.sv", 100U, digest(1U) },
    };
    contents.runs = { { id(run), "run-" + std::to_string(run),
        "fsim 3.0.0-dev", run, 100U, 2U,
        CoverageDatabaseRunStatus::Complete } };
    contents.metrics = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(11U), id(1U), { },
            id(run), 1U, 0U, false, false, 10U },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(12U), id(1U), { },
            id(run), 0U, 0U, false, false, 11U },
    };
    return contents;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path = std::filesystem::temp_directory_path()
            / ("fsim-coverage-command-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct CommandResult {
    int status { };
    std::string output;
    std::string error;
};

CommandResult run(const std::vector<std::string>& arguments)
{
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments)
        raw.push_back(argument.c_str());
    std::ostringstream output;
    std::ostringstream error;
    const auto status = cli::run(static_cast<int>(raw.size()), raw.data(),
        app::make_cli_services(), output, error);
    return { status, output.str(), error.str() };
}

std::string text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { } };
}

} // namespace

int main()
{
    TemporaryDirectory directory;
    const auto first = directory.path / "first.fsimcov";
    const auto second = directory.path / "second.fsimcov";
    const auto historical = directory.path / "historical.fsimcov";
    const auto merged = directory.path / "merged.fsimcov";
    const auto partial = directory.path / "partial.fsimcov";
    const auto lcov = directory.path / "coverage.info";
    assert(artifact::write_coverage_database_atomically(first, database(1U))
            .ok());
    assert(artifact::write_coverage_database_atomically(second, database(2U))
            .ok());
    assert(artifact::write_coverage_database_atomically(
        historical, database(3U, 41U))
            .ok());

    const auto first_text = support::path_to_utf8(first);
    const auto second_text = support::path_to_utf8(second);
    const auto historical_text = support::path_to_utf8(historical);
    const auto merged_text = support::path_to_utf8(merged);
    const auto partial_text = support::path_to_utf8(partial);
    const auto lcov_text = support::path_to_utf8(lcov);

    auto result = run({ "fsim", "coverage", "merge", "--output",
        merged_text, first_text, second_text });
    assert(result.status == 0 && result.error.empty());
    assert(result.output.find("coverage merge wrote 2 input(s)")
        != std::string::npos);
    const auto merged_contents = artifact::read_coverage_database(merged);
    assert(merged_contents.ok() && merged_contents.contents->runs.size() == 2U);

    result = run({ "fsim", "coverage", "report", merged_text });
    assert(result.status == 0 && result.error.empty());
    assert(result.output.find("fsim coverage report v3") != std::string::npos);
    assert(result.output.find("family=statement total=2 covered=1")
        != std::string::npos);

    result = run({ "fsim", "coverage", "report", "--format", "lcov",
        "--output", lcov_text, merged_text });
    assert(result.status == 0 && result.output.empty() && result.error.empty());
    const auto lcov_contents = text(lcov);
    assert(lcov_contents.find("SF:rtl/design.sv") != std::string::npos);
    assert(lcov_contents.find("DA:10,2") != std::string::npos);
    assert(lcov_contents.find("DA:11,0") != std::string::npos);

    result = run({ "fsim", "coverage", "report", "--threshold",
        "statement=50", merged_text });
    assert(result.status == 0 && result.error.empty()
        && !result.output.empty());
    result = run({ "fsim", "coverage", "report", "--threshold",
        "statement=100", merged_text });
    assert(result.status == 4 && !result.output.empty());
    assert(result.error.find("FSIM-COV-047") != std::string::npos);
    result = run({ "fsim", "coverage", "report", "--threshold", "line=1",
        merged_text });
    assert(result.status == 4 && !result.output.empty());
    result = run({ "fsim", "coverage", "report", "--threshold", "line=0",
        merged_text });
    assert(result.status == 0 && result.error.empty());
    result = run({ "fsim", "coverage", "report", "--threshold",
        "unknown=1", merged_text });
    assert(result.status == 1
        && result.error.find("unknown coverage threshold metric")
            != std::string::npos);

    result = run({ "fsim", "coverage", "merge", "--partial", "--output",
        partial_text, first_text, historical_text });
    assert(result.status == 0 && result.error.empty());
    const auto partial_contents = artifact::read_coverage_database(partial);
    assert(partial_contents.ok() && partial_contents.contents->runs.size() == 2U);

    const auto rejected = directory.path / "rejected.fsimcov";
    const auto rejected_text = support::path_to_utf8(rejected);
    result = run({ "fsim", "coverage", "merge", "--output", rejected_text,
        first_text, historical_text });
    assert(result.status == 1 && !std::filesystem::exists(rejected));
    assert(result.error.find("FSIM-COV-047") != std::string::npos);

    const auto corrupt = directory.path / "corrupt.fsimcov";
    {
        std::ofstream output(corrupt, std::ios::binary);
        output << "not a coverage database";
    }
    result = run({ "fsim", "coverage", "report",
        support::path_to_utf8(corrupt) });
    assert(result.status == 1
        && result.error.find("cannot read coverage database")
            != std::string::npos);

    assert(run({ "fsim", "coverage", "unknown" }).status == 2);
    assert(run({ "fsim", "coverage", "merge", first_text }).status == 2);
    assert(run({ "fsim", "coverage", "report", first_text, second_text })
               .status
        == 2);
    assert(run({ "fsim", "coverage", "report", "--format", "yaml",
                   first_text })
               .status
        == 2);
    assert(run({ "fsim", "coverage", "report", "--threshold", "line=101",
                   first_text })
               .status
        == 2);
    assert(run({ "fsim", "coverage", "report", "--threshold",
                   "statement=50", "--threshold", "Statement=60", first_text })
               .status
        == 2);
    assert(run({ "fsim", "coverage", "report", "--partial", first_text })
               .status
        == 2);
    assert(run({ "fsim", "coverage", "report", "--engine", "compiled",
                   first_text })
               .status
        == 2);
}
