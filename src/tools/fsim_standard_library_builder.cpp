// SPDX-License-Identifier: Apache-2.0
//
// fsim_standard_library_builder: compiles one VHDL edition's standard library
// packages (IEEE 1076-2019 16: STD.TEXTIO, STD.ENV, IEEE.STD_LOGIC_1164,
// IEEE.NUMERIC_BIT, IEEE.NUMERIC_STD, their UNSIGNED companions,
// IEEE.MATH_REAL, IEEE.MATH_COMPLEX) from the sources of the edition's tree
// into the managed libraries STD and IEEE of a workspace directory (gate BD
// build D2): <output>/.fsim/libraries/{std,ieee} hold a library.sqlite3
// catalog and one .fsimobj object per design unit, the form `fsim library
// map` reads, and <output>/standard-library.key the key they were built with.
// The sources are copied to <output>/src/<library>/ so that the objects name
// them relative to the directory, wherever it is installed.
//
//   fsim_standard_library_builder --edition <1987|1993|2000|2002|2008|2019>
//       --source-root <fsim source dir> --output <directory>
//
// With --uvm-release (gate BD build D2b) it instead compiles one UVM kit
// (<source-root> is the kit directory, e.g. third_party/uvm-1.2/kit) into the
// managed library `uvm` of <output>: the kit's src tree is copied to
// <output>/src (the include root of uvm_macros.svh), <output>/src/uvm_pkg.sv
// is compiled as SystemVerilog 1800-2017 with no defines and the source
// set's uvm_release, so the UVM roles are stamped.
//
//   fsim_standard_library_builder --uvm-release <uvm-1.2|uvm-2020.3.1>
//       --source-root <kit directory> --output <directory>
#include "application_internal.hpp"
#include "application_standard_library_artifact.hpp"
#include "application_workspace.hpp"
#include "application_workspace_internal.hpp"
#include "application_workspace_objects.hpp"
#include "application_workspace_store.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/project/project.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/semantic/vhdl_native_intrinsics.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsim::frontend::VhdlStandard;
namespace fs = std::filesystem;

struct Edition {
    std::string_view name;
    VhdlStandard standard;
};

constexpr Edition kEditions[] {
    { "1987", VhdlStandard::Vhdl1987 },
    { "1993", VhdlStandard::Vhdl1993 },
    { "2000", VhdlStandard::Vhdl2000 },
    { "2002", VhdlStandard::Vhdl2002 },
    { "2008", VhdlStandard::Vhdl2008 },
    { "2019", VhdlStandard::Vhdl2019 },
};

// The covered declarations, in analysis order, where the tree has them.
// math_complex stays ordinary VHDL over math_real: its body is analyzed too.
constexpr std::string_view kStdFiles[] {
    "std/textio.vhdl",
    "std/env.vhdl",
};
// VHDL-2000 shares the 2002 tree; its TEXTIO is the tree's textio-2000.vhdl
// (IEEE Std 1076-2000 14.3 READLINE has L: out LINE, owner 11-19).
constexpr std::string_view kStdFiles2000[] {
    "std/textio-2000.vhdl",
};
constexpr std::string_view kIeeeFiles[] {
    "ieee/std_logic_1164.vhdl",
    "ieee/numeric_bit.vhdl",
    "ieee/numeric_std.vhdl",
    "ieee/numeric_bit_unsigned.vhdl",
    "ieee/numeric_std_unsigned.vhdl",
    "ieee/math_real.vhdl",
    "ieee/math_complex.vhdl",
    "ieee/math_complex-body.vhdl",
};

int usage(const std::string_view message)
{
    std::cerr << "fsim_standard_library_builder: " << message << '\n'
              << "usage: fsim_standard_library_builder --edition "
                 "<1987|1993|2000|2002|2008|2019> --source-root <dir> "
                 "--output <directory>\n"
                 "       fsim_standard_library_builder --uvm-release "
                 "<uvm-1.2|uvm-2020.3.1> --source-root <kit dir> "
                 "--output <directory>\n";
    return 2;
}

// The whole content of `path`, or nullopt.
std::optional<std::string> read_bytes(const fs::path& path)
{
    auto input = fsim::support::native_fs::open_ifstream(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    return std::string { std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { } };
}

// The UVM mode (see the file comment).
int build_uvm(const std::string_view release_text,
    const std::string& root_text, const std::string& output_text)
{
    using fsim::project::SystemVerilogUvmRelease;
    SystemVerilogUvmRelease release { SystemVerilogUvmRelease::none };
    if (release_text == "uvm-1.2") {
        release = SystemVerilogUvmRelease::uvm_1_2;
    } else if (release_text == "uvm-2020.3.1") {
        release = SystemVerilogUvmRelease::ieee_1800_2_2020_3_1;
    } else {
        return usage("unknown UVM release '" + std::string { release_text } + "'");
    }
    const auto root = fs::absolute(fsim::support::path_from_utf8(root_text))
                          .lexically_normal();
    const auto output = fs::absolute(fsim::support::path_from_utf8(output_text))
                            .lexically_normal();
    fsim::diagnostic::Engine diagnostics;
    const auto fail = [&](const std::string_view message) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
        std::cerr << "fsim_standard_library_builder: " << message << '\n';
        return 1;
    };
    {
        // Every build starts from nothing (key first, so a failed build
        // leaves no key).
        std::error_code code;
        fsim::support::native_fs::remove(
            output / std::string { fsim::app::standard_library_key_file_name }, code);
        fsim::support::native_fs::remove_all(output / ".fsim", code);
        fsim::support::native_fs::remove_all(output / "src", code);
        if (code) {
            return fail("cannot clear '" + fsim::support::path_to_utf8(output) + "'");
        }
    }
    // The kit's src tree becomes <output>/src.
    const auto kit_src = root / "src";
    std::error_code code;
    if (!fsim::support::native_fs::is_regular_file(kit_src / "uvm_pkg.sv", code)) {
        return fail("'" + fsim::support::path_to_utf8(kit_src / "uvm_pkg.sv")
            + "' is not a file");
    }
    for (auto entry = fsim::support::native_fs::recursive_directory_iterator(
             kit_src, code);
         !code && entry != fs::recursive_directory_iterator { };
         entry.increment(code)) {
        if (!entry->is_regular_file()) {
            continue;
        }
        const auto bytes = read_bytes(entry->path());
        if (!bytes) {
            return fail("cannot read '" + fsim::support::path_to_utf8(entry->path()) + "'");
        }
        const auto copy = output / "src"
            / entry->path().lexically_relative(kit_src);
        std::error_code create;
        fsim::support::native_fs::create_directories(copy.parent_path(), create);
        auto stream = fsim::support::native_fs::open_ofstream(
            copy, std::ios::binary | std::ios::trunc);
        stream.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
        stream.close();
        if (!stream) {
            return fail("cannot write '" + fsim::support::path_to_utf8(copy) + "'");
        }
    }
    if (code) {
        return fail("cannot list '" + fsim::support::path_to_utf8(kit_src) + "'");
    }

    // The same compile `fsim compile --library uvm --uvm-release R
    // -I <kit>/src <kit>/src/uvm_pkg.sv` does (the corpus and tests pass no
    // defines); uvm_release stamps the UVM roles.
    fsim::project::Config config;
    config.base_directory = output;
    fsim::project::SourceSet set;
    set.language = fsim::project::Language::system_verilog;
    set.standard = "2017";
    set.library = "uvm";
    set.uvm_release = release;
    set.include_directories.push_back(output / "src");
    set.files.push_back(output / "src" / "uvm_pkg.sv");
    config.source_sets.push_back(std::move(set));
    fsim::cli::Invocation invocation;
    invocation.verbosity = fsim::cli::Verbosity::quiet;
    std::ostringstream ignored;
    if (fsim::app::application_detail::compile_uvm_library(
            invocation, config, diagnostics, ignored, ignored)
            != 0
        || diagnostics.has_error()) {
        return fail("compiling the library UVM failed");
    }

    // The library loads as any mapped library does.
    const fsim::app::workspace::Store store(
        output, fsim::app::workspace::StandardLibraryStore { });
    const auto catalogs = fsim::app::application_detail::workspace_catalogs(
        store, nullptr, diagnostics);
    if (!catalogs) {
        return fail("the compiled library's catalogs cannot be read");
    }
    const auto selections
        = fsim::app::application_detail::workspace_object_selections(
            store, *catalogs, { }, { }, diagnostics);
    const auto loaded = fsim::app::application_detail::load_workspace_objects(
        selections, diagnostics, false, output);
    if (!loaded || diagnostics.has_error()) {
        return fail("the compiled library does not load");
    }

    // No absolute path may reach the objects: they are installed.
    for (auto entry = fsim::support::native_fs::recursive_directory_iterator(
             output / ".fsim", code);
         !code && entry != fs::recursive_directory_iterator { };
         entry.increment(code)) {
        if (!entry->is_regular_file() || entry->path().extension() != ".fsimobj") {
            continue;
        }
        const auto bytes = read_bytes(entry->path());
        if (!bytes) {
            return fail("cannot read '" + fsim::support::path_to_utf8(entry->path()) + "'");
        }
        for (const auto& absolute : { fsim::support::path_to_utf8(root),
                 fsim::support::path_to_utf8(output) }) {
            if (bytes->find(absolute) != std::string::npos) {
                return fail("'" + fsim::support::path_to_utf8(entry->path())
                    + "' holds the absolute path '" + absolute + "'");
            }
        }
    }
    if (code) {
        return fail("cannot list '" + fsim::support::path_to_utf8(output / ".fsim") + "'");
    }

    std::string error;
    if (!fsim::app::write_uvm_library_key(output,
            fsim::app::expected_uvm_library_artifact_key(release), error)) {
        return fail(error);
    }
    return 0;
}

} // namespace

int main(const int argc, const char* const* const argv)
{
    std::string edition_text;
    std::string uvm_text;
    std::string root_text;
    std::string output_text;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option { argv[index] };
        if (index + 1 >= argc) {
            return usage("option '" + std::string { option } + "' needs a value");
        }
        const std::string value { argv[++index] };
        if (option == "--edition") {
            edition_text = value;
        } else if (option == "--uvm-release") {
            uvm_text = value;
        } else if (option == "--source-root") {
            root_text = value;
        } else if (option == "--output") {
            output_text = value;
        } else {
            return usage("unknown option '" + std::string { option } + "'");
        }
    }
    if (!uvm_text.empty()) {
        if (!edition_text.empty() || root_text.empty() || output_text.empty()) {
            return usage("--uvm-release takes --source-root and --output, "
                         "and not --edition");
        }
        return build_uvm(uvm_text, root_text, output_text);
    }
    if (edition_text.empty() || root_text.empty() || output_text.empty()) {
        return usage("--edition, --source-root and --output are required");
    }
    const auto edition = std::ranges::find(
        kEditions, edition_text, &Edition::name);
    if (edition == std::end(kEditions)) {
        return usage("unknown edition '" + edition_text + "'");
    }
    const auto standard = edition->standard;
    const auto root = fs::absolute(fsim::support::path_from_utf8(root_text))
                          .lexically_normal();
    const auto tree = root / fsim::app::standard_library_source_tree(standard);

    fsim::diagnostic::Engine diagnostics;
    const auto fail = [&](const std::string_view message) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
        std::cerr << "fsim_standard_library_builder: " << message << '\n';
        return 1;
    };

    const auto output = fs::absolute(fsim::support::path_from_utf8(output_text))
                            .lexically_normal();
    // Every build compiles the libraries from nothing: objects of an earlier
    // build (other sources, natives or fsim) are never an input.
    {
        std::error_code code;
        fsim::support::native_fs::remove(output / std::string { fsim::app::standard_library_key_file_name }, code);
        fsim::support::native_fs::remove_all(output / ".fsim", code);
        if (code) {
            return fail("cannot clear '" + fsim::support::path_to_utf8(output / ".fsim") + "'");
        }
    }

    // The sources, copied beside the libraries (a copy is rewritten only
    // when its content differs, so an unchanged tree rebuilds nothing).
    const auto copy_sources = [&](const std::string_view library,
                                  const auto& files)
        -> std::optional<std::vector<fs::path>> {
        std::vector<fs::path> copies;
        for (const auto file : files) {
            const auto source = (tree / fs::path { file }).lexically_normal();
            std::error_code code;
            if (!fsim::support::native_fs::is_regular_file(source, code)) {
                continue;
            }
            const auto bytes = read_bytes(source);
            if (!bytes) {
                std::cerr << "fsim_standard_library_builder: cannot read '"
                          << fsim::support::path_to_utf8(source) << "'\n";
                return std::nullopt;
            }
            const auto copy = output / "src" / std::string { library }
                / fs::path { file }.filename();
            if (read_bytes(copy) != bytes) {
                fsim::support::native_fs::create_directories(copy.parent_path(), code);
                auto stream = fsim::support::native_fs::open_ofstream(
                    copy, std::ios::binary | std::ios::trunc);
                stream.write(bytes->data(),
                    static_cast<std::streamsize>(bytes->size()));
                stream.close();
                if (!stream) {
                    std::cerr << "fsim_standard_library_builder: cannot write '"
                              << fsim::support::path_to_utf8(copy) << "'\n";
                    return std::nullopt;
                }
            }
            copies.push_back(copy);
        }
        return copies;
    };

    // STD first: IEEE's packages use STD.TEXTIO.
    const auto compile_library = [&](const std::string_view library,
                                     const auto& files) {
        const auto copies = copy_sources(library, files);
        if (!copies) {
            return false;
        }
        if (copies->empty()) {
            return true;
        }
        fsim::project::Config config;
        config.base_directory = output;
        fsim::project::SourceSet set;
        set.language = fsim::project::Language::vhdl;
        set.standard = std::string { edition->name };
        set.library = std::string { library };
        set.files = *copies;
        config.source_sets.push_back(std::move(set));
        fsim::cli::Invocation invocation;
        invocation.verbosity = fsim::cli::Verbosity::quiet;
        std::ostringstream ignored;
        return fsim::app::application_detail::compile_standard_library(
                   invocation, config, diagnostics, ignored, ignored, standard)
            == 0
            && !diagnostics.has_error();
    };
    if (!(standard == VhdlStandard::Vhdl2000
                ? compile_library("std", kStdFiles2000)
                : compile_library("std", kStdFiles))) {
        return fail("compiling the library STD failed");
    }
    if (!compile_library("ieee", kIeeeFiles)) {
        return fail("compiling the library IEEE failed");
    }

    // The libraries load as any mapped library does.
    const fsim::app::workspace::Store store(
        output, fsim::app::workspace::StandardLibraryStore { });
    const auto catalogs = fsim::app::application_detail::workspace_catalogs(
        store, nullptr, diagnostics);
    if (!catalogs) {
        return fail("the compiled libraries' catalogs cannot be read");
    }
    const auto selections
        = fsim::app::application_detail::workspace_object_selections(
            store, *catalogs, { }, { }, diagnostics);
    auto loaded = fsim::app::application_detail::load_workspace_objects(
        selections, diagnostics, false, output);
    if (!loaded || diagnostics.has_error()) {
        return fail("the compiled libraries do not load");
    }
    const auto& design = static_cast<const fsim::semantic::CompiledDesign&>(*loaded);

    // Natives are never executed from a body: none may carry one. Every
    // table item of the edition's packages is stamped on exactly one loaded
    // declaration (the stamp survives the objects' round trip).
    std::size_t stamped = 0;
    for (const auto& declaration : design.vhdl_hir.declarations()) {
        if (declaration.native_intrinsic == 0) {
            continue;
        }
        ++stamped;
        if (declaration.callable && declaration.callable->defined) {
            return fail("the native-stamped declaration '" + declaration.name
                + "' has a subprogram body");
        }
    }
    std::size_t expected = 0;
    for (const auto& entry : fsim::semantic::vhdl_native_intrinsics()) {
        if ((entry.editions >> static_cast<unsigned>(standard)) & 1U) {
            ++expected;
        }
    }
    if (stamped != expected) {
        return fail("the loaded libraries carry " + std::to_string(stamped)
            + " native-stamped declarations; the native table has "
            + std::to_string(expected) + " for this edition");
    }

    // No absolute path may reach the objects: they are installed. (The
    // catalogs' source keys are the managed-library form's canonical source
    // identities, used only to replace a recompiled source's units.)
    std::error_code code;
    for (auto entry = fsim::support::native_fs::recursive_directory_iterator(
             output / ".fsim", code);
         !code && entry != fs::recursive_directory_iterator { };
         entry.increment(code)) {
        if (!entry->is_regular_file() || entry->path().extension() != ".fsimobj") {
            continue;
        }
        const auto bytes = read_bytes(entry->path());
        if (!bytes) {
            return fail("cannot read '" + fsim::support::path_to_utf8(entry->path()) + "'");
        }
        for (const auto& absolute : { fsim::support::path_to_utf8(root),
                 fsim::support::path_to_utf8(output) }) {
            if (bytes->find(absolute) != std::string::npos) {
                return fail("'" + fsim::support::path_to_utf8(entry->path())
                    + "' holds the absolute path '" + absolute + "'");
            }
        }
    }
    if (code) {
        return fail("cannot list '" + fsim::support::path_to_utf8(output / ".fsim") + "'");
    }

    std::string error;
    if (!fsim::app::write_standard_library_key(output,
            fsim::app::expected_standard_library_artifact_key(standard), error)) {
        return fail(error);
    }
    return 0;
}
