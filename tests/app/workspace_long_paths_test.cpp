// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Run the workspace commands where the workspace root is short enough to be a
// working directory on any Windows host, but the sources, include files,
// artifacts, snapshots, traces, and simulation output below it exceed
// MAX_PATH. Test executables carry no long-path manifest, so this exercises
// fsim's own extended-length I/O even when the host enables long paths.

namespace {

namespace fs = std::filesystem;
namespace native_fs = fsim::support::native_fs;

constexpr std::size_t kWorkspaceRootLength = 190U;
constexpr std::size_t kMaxPath = 260U;

class Workspace final {
public:
    Workspace()
        : previous_(fs::current_path())
    {
        static std::atomic_uint64_t sequence { 0U };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        base_ = fs::temp_directory_path() / ("fsim-long-paths-"
            + std::to_string(stamp) + "-" + std::to_string(sequence++));
        root = base_;
        const auto length = fs::absolute(root).native().size();
        if (length + 2U < kWorkspaceRootLength) {
            root /= std::string(kWorkspaceRootLength - length - 1U, 'w');
        }
        native_fs::create_directories(root);
        fs::current_path(root);
    }

    ~Workspace()
    {
        std::error_code error;
        fs::current_path(previous_, error);
        auto iterator = native_fs::recursive_directory_iterator(base_, error);
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            native_fs::permissions(iterator->path(), fs::perms::owner_all,
                fs::perm_options::add, error);
            error.clear();
            iterator.increment(error);
        }
        error.clear();
        native_fs::remove_all(base_, error);
    }

    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    fs::path root;

private:
    fs::path previous_;
    fs::path base_;
};

struct Capture {
    std::string output;
    std::string error;
};

Capture invoke(std::vector<std::string> arguments)
{
    arguments.insert(arguments.begin(), "fsim");
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments)
        raw.push_back(argument.c_str());
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int status = fsim::cli::run(static_cast<int>(raw.size()), raw.data(),
        fsim::app::make_cli_services(input), output, error);
    if (status != 0) {
        std::cerr << "Unexpected long-path command result " << status << ":";
        for (const auto& argument : arguments)
            std::cerr << ' ' << argument;
        std::cerr << '\n' << output.str() << error.str();
    }
    assert(status == 0);
    // Extended-length spellings stay at the I/O seams.
    for (const auto* text : { &output, &error }) {
        assert(text->str().find("\\\\?\\") == std::string::npos);
        assert(text->str().find("//?/") == std::string::npos);
    }
    return { output.str(), error.str() };
}

void write_file(const fs::path& path, const std::string_view text)
{
    native_fs::create_directories(path.parent_path());
    auto output = native_fs::open_ofstream(path, std::ios::binary);
    output << text;
    assert(output.good());
}

std::string read_file(const fs::path& path)
{
    auto input = native_fs::open_ifstream(path, std::ios::binary);
    assert(input.good());
    return { std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { } };
}

fs::path deep(const fs::path& base)
{
    auto path = base;
    for (const char marker : { 'a', 'b', 'c' })
        path /= std::string(60U, marker);
    return path;
}

std::size_t longest_path(const fs::path& root)
{
    std::size_t longest = 0U;
    std::error_code error;
    auto iterator = native_fs::recursive_directory_iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto ordinary = fsim::support::path_from_native_io(iterator->path());
        longest = std::max(longest, ordinary.native().size());
        iterator.increment(error);
    }
    assert(!error);
    return longest;
}

} // namespace

int main()
{
    Workspace workspace;
    assert(fs::absolute(workspace.root).native().size() < 248U);

    const auto sources = deep("rtl");
    const auto output_directory = deep("out");
    const auto source = sources / "top.sv";
    const auto output_file = output_directory / "log.txt";
    const auto vcd = output_directory / "wave.vcd";
    const auto fst = output_directory / "wave.fst";
    assert(fs::absolute(source).native().size() > kMaxPath);
    native_fs::create_directories(output_directory);

    write_file(sources / "defs.svh",
        "`define LONG_PATH_VALUE 42\n"
        "`define OUTPUT_FILE \"" + fsim::support::path_to_utf8(output_file) + "\"\n");
    write_file(source,
        "`include \"defs.svh\"\n"
        "module top;\n"
        "  reg clk = 1'b0;\n"
        "  integer fd;\n"
        "  always #1 clk = ~clk;\n"
        "  initial begin\n"
        "    fd = $fopen(`OUTPUT_FILE, \"w\");\n"
        "    $fdisplay(fd, \"LONG_PATH_FILE_PASS\");\n"
        "    $fclose(fd);\n"
        "    #10 $display(\"LONG_PATH_SIM_PASS %0d\", `LONG_PATH_VALUE);\n"
        "    $finish;\n"
        "  end\n"
        "endmodule\n");

    invoke({ "compile", "--library", "work", fsim::support::path_to_utf8(source) });
    assert(invoke({ "library", "objects", "work" }).output.find("top")
        != std::string::npos);
    invoke({ "elaborate", "work.top" });
    invoke({ "elaborate", "work.top", "--snapshot", "retained" });
    invoke({ "elaborate", "work.top", "--snapshot", "native", "--aot" });

    const auto interpreted = invoke({ "simulate", "--engine", "interpreter", "--file-root", ".",
        "--trace", fsim::support::path_to_utf8(vcd), "--trace-format", "vcd" });
    assert(interpreted.output.find("LONG_PATH_SIM_PASS 42") != std::string::npos);
    assert(read_file(output_file).find("LONG_PATH_FILE_PASS") != std::string::npos);
    assert(native_fs::file_size(vcd) > 0U);

    native_fs::remove(output_file);
    const auto compiled = invoke({ "simulate", "--snapshot", "native", "--file-root", ".",
        "--trace", fsim::support::path_to_utf8(fst), "--trace-format", "fst" });
    assert(compiled.output.find("LONG_PATH_SIM_PASS 42") != std::string::npos);
    assert(read_file(output_file).find("LONG_PATH_FILE_PASS") != std::string::npos);
    assert(native_fs::file_size(fst) > 0U);

    const auto retained = invoke({ "simulate", "--snapshot", "retained", "--file-root", "." });
    assert(retained.output.find("LONG_PATH_SIM_PASS 42") != std::string::npos);

#if defined(FSIM_HAS_TCL)
    // Tcl's own file commands on the same kind of paths, from a script whose
    // path is itself beyond MAX_PATH. Windows builds patch Tcl for this; see
    // third_party/tcl-9.0.4/README.md.
    const auto tcl_directory = deep("tcl");
    const auto tcl_script = tcl_directory / "probe.tcl";
    assert(fs::absolute(tcl_script).native().size() > kMaxPath);
    write_file(tcl_script, R"TCL(
set base [file join tcl [string repeat a 60] [string repeat b 60] [string repeat c 60]]
foreach root [list [file join $base relative nested] [file join [pwd] $base absolute nested]] {
    file mkdir $root
    set data [file join $root data.tcl]
    set f [open $data w]
    puts $f {set long_path_value 42}
    close $f
    set long_path_value 0
    source $data
    if {$long_path_value != 42} { error "source lost its value: $root" }
    if {[llength [glob -directory $root *.tcl]] != 1} { error "glob missed $data" }
    file stat $data stat
    if {$stat(size) == 0} { error "stat lost the size of $data" }
    file copy $data [file join $root copy.tcl]
    file rename -force [file join $root copy.tcl] $data
    file mkdir [file join $root tree leaf]
    file copy [file join $root tree] [file join $root tree-copy]
    if {![file isdirectory [file join $root tree-copy leaf]]} { error "directory copy lost its leaf" }
    file delete -force [file join $root tree] [file join $root tree-copy]
    if {[file exists [file join $root tree]]} { error "recursive delete left $root/tree" }
}
set f [open [file join $base result.txt] w]
puts $f TCL_LONG_PATH_PASS
close $f
)TCL");
    invoke({ "tcl", fsim::support::path_to_utf8(tcl_script) });
    assert(read_file(tcl_directory / "result.txt").find("TCL_LONG_PATH_PASS")
        != std::string::npos);
#endif

    // The workspace state itself must have gone beyond MAX_PATH, or this test
    // proves nothing about long paths.
    assert(longest_path(workspace.root / ".fsim") > kMaxPath);
    assert(longest_path(workspace.root) > kMaxPath);
    std::cout << "workspace long-path tests passed\n";
    return 0;
}
