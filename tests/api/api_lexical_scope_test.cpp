// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"
#include "fsim/support/path.hpp"

#include "api_workspace_support.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

fsim_string_view_t text(const char* value)
{
    return { value, std::char_traits<char>::length(value) };
}

int collect_object(
    fsim_session_t,
    const fsim_object_t object,
    void* user_data)
{
    static_cast<std::vector<fsim_object_t>*>(user_data)->push_back(object);
    return 1;
}

} // namespace

// A named block and a variable whose escaped names contain a period (IEEE
// 1800-2023 5.6.1) are one scope and one variable, named as written with
// the terminating space: the debugger's scopes follow the blocks that
// enclose a local, not the periods of its displayed name.
void test_lexical_scope_api(const std::filesystem::path& directory)
{
    {
        std::ofstream source(directory / "escaped_scopes.sv");
        source << R"(
module escaped_scopes;
  logic q;
  initial begin : \a.b
    automatic logic \v.w = 1'b1;
    begin : inner
      automatic logic x = 1'b0;
      q = x;
    end : inner
    q = \v.w ;
  end
endmodule
)";
    }
    fsim::tests::api_workspace::expect(directory,
        { "compile", "--lang", "systemverilog", "--standard", "2017",
            "--library", "work", "escaped_scopes.sv" });
    fsim::tests::api_workspace::expect(directory,
        { "elaborate", "--snapshot", "escaped-scopes",
            "sv:work.escaped_scopes" });
    fsim_session_t session = FSIM_INVALID_SESSION;
    assert(fsim_session_create(nullptr, &session) == FSIM_STATUS_OK);
    assert(fsim_session_open_snapshot(session,
               fsim::support::path_to_utf8(directory).c_str(),
               "escaped-scopes")
        == FSIM_STATUS_OK);

    const auto info = [&](const fsim_object_t object) {
        fsim_object_info_t result { };
        result.struct_size = sizeof(result);
        result.api_version = FSIM_API_VERSION;
        assert(fsim_session_get_object_info(session, object, &result)
            == FSIM_STATUS_OK);
        return result;
    };
    const auto name = [](const fsim_string_view_t view) {
        return std::string { view.data, view.size };
    };

    fsim_object_t process = FSIM_INVALID_OBJECT;
    assert(fsim_session_find_object(
               session, text("escaped_scopes.process_0"), &process)
        == FSIM_STATUS_OK);
    std::vector<fsim_object_t> process_children;
    assert(fsim_session_visit_children(
               session, process, collect_object, &process_children)
        == FSIM_STATUS_OK);
    assert(process_children.size() == 1);
    const auto block = process_children.front();
    const auto block_info = info(block);
    assert(block_info.kind == FSIM_OBJECT_SCOPE);
    assert(block_info.parent == process);
    assert(name(block_info.name) == "\\a.b ");

    std::vector<fsim_object_t> block_children;
    assert(fsim_session_visit_children(
               session, block, collect_object, &block_children)
        == FSIM_STATUS_OK);
    assert(block_children.size() == 2);
    bool variable_seen { };
    bool inner_seen { };
    for (const auto child : block_children) {
        const auto child_info = info(child);
        assert(child_info.parent == block);
        if (child_info.kind == FSIM_OBJECT_VARIABLE) {
            assert(name(child_info.name) == "\\v.w ");
            variable_seen = true;
        } else {
            assert(child_info.kind == FSIM_OBJECT_SCOPE);
            assert(name(child_info.name) == "inner");
            std::vector<fsim_object_t> inner_children;
            assert(fsim_session_visit_children(
                       session, child, collect_object, &inner_children)
                == FSIM_STATUS_OK);
            assert(inner_children.size() == 1);
            const auto local = info(inner_children.front());
            assert(local.kind == FSIM_OBJECT_VARIABLE);
            assert(local.parent == child);
            assert(name(local.name) == "x");
            inner_seen = true;
        }
    }
    assert(variable_seen && inner_seen);
    assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
}
