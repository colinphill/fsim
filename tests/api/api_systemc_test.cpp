// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

fsim_string_view_t text(const char* value)
{
    return { value, std::strlen(value) };
}

int collect_object(
    fsim_session_t, const fsim_object_t object, void* user_data)
{
    static_cast<std::vector<fsim_object_t>*>(user_data)
        ->push_back(object);
    return 1;
}

} // namespace

void test_systemc_api(const std::filesystem::path& systemc_manifest_path)
{
    fsim_session_t systemc_session = FSIM_INVALID_SESSION;
    assert(
        fsim_session_create(nullptr, &systemc_session)
        == FSIM_STATUS_OK);
    assert(
        fsim_session_load_project(
            systemc_session,
            fsim::support::path_to_utf8(systemc_manifest_path).c_str())
        == FSIM_STATUS_OK);
    assert(fsim_session_check(systemc_session) == FSIM_STATUS_OK);
    assert(fsim_session_build(systemc_session) == FSIM_STATUS_OK);

    const auto find_systemc = [&](const char* path) {
        fsim_object_t object = FSIM_INVALID_OBJECT;
        const auto status = fsim_session_find_object(
            systemc_session, text(path), &object);
        if (status != FSIM_STATUS_OK) {
            fsim_object_t diagnostic_root = FSIM_INVALID_OBJECT;
            std::vector<fsim_object_t> diagnostic_pending;
            if (fsim_session_root(systemc_session, &diagnostic_root)
                == FSIM_STATUS_OK) {
                diagnostic_pending.push_back(diagnostic_root);
                std::cerr << "missing SystemC API object '" << path
                          << "'; hierarchy:";
                for (std::size_t index = 0;
                     index < diagnostic_pending.size(); ++index) {
                    const auto child_object = diagnostic_pending[index];
                    fsim_object_info_t child_info { };
                    child_info.struct_size = sizeof(child_info);
                    child_info.api_version = FSIM_API_VERSION;
                    if (fsim_session_get_object_info(
                            systemc_session, child_object, &child_info)
                        == FSIM_STATUS_OK) {
                        std::cerr << " "
                                  << std::string_view {
                                         child_info.full_name.data,
                                         child_info.full_name.size };
                    }
                    (void)fsim_session_visit_children(
                        systemc_session,
                        child_object,
                        collect_object,
                        &diagnostic_pending);
                }
                std::cerr << "\n";
            }
        }
        assert(status == FSIM_STATUS_OK);
        assert(object != FSIM_INVALID_OBJECT);
        return object;
    };
    const auto systemc_info = [&](const fsim_object_t object) {
        fsim_object_info_t object_info { };
        object_info.struct_size = sizeof(object_info);
        object_info.api_version = FSIM_API_VERSION;
        assert(
            fsim_session_get_object_info(
                systemc_session, object, &object_info)
            == FSIM_STATUS_OK);
        return object_info;
    };

    fsim_object_t systemc_root = FSIM_INVALID_OBJECT;
    assert(
        fsim_session_root(systemc_session, &systemc_root)
        == FSIM_STATUS_OK);
    const auto input = find_systemc("named.input");
    const auto child = find_systemc("named.child");
    const auto state = find_systemc("named.child.state");
    const auto view_object = find_systemc("named.child.view");
    const auto kernel = find_systemc("named.$accellera_kernel");
    fsim_object_t removed_legacy_object = FSIM_INVALID_OBJECT;
    for (const auto* legacy_path : {
             "named.child.pulse",
             "named.child.metadata",
             "named.child.run",
         }) {
        assert(
            fsim_session_find_object(
                systemc_session,
                text(legacy_path),
                &removed_legacy_object)
            == FSIM_STATUS_INVALID_HANDLE);
    }
    assert(systemc_info(input).kind == FSIM_OBJECT_PORT);
    assert(systemc_info(input).parent == systemc_root);
    assert(systemc_info(child).kind == FSIM_OBJECT_SCOPE);
    assert(systemc_info(child).parent == systemc_root);
    assert(systemc_info(state).kind == FSIM_OBJECT_SIGNAL);
    assert(systemc_info(state).parent == child);
    assert(systemc_info(view_object).kind == FSIM_OBJECT_EXPORT);
    assert(systemc_info(view_object).parent == child);
    assert(systemc_info(kernel).kind == FSIM_OBJECT_PROCESS);
    assert(systemc_info(kernel).parent == systemc_root);
    assert(state != view_object);

    std::vector<fsim_object_t> systemc_root_children;
    assert(
        fsim_session_visit_children(
            systemc_session,
            systemc_root,
            collect_object,
            &systemc_root_children)
        == FSIM_STATUS_OK);
    assert(
        std::ranges::find(systemc_root_children, input)
        != systemc_root_children.end());
    assert(
        std::ranges::find(systemc_root_children, child)
        != systemc_root_children.end());
    assert(
        std::ranges::find(systemc_root_children, kernel)
        != systemc_root_children.end());
    std::vector<fsim_object_t> systemc_child_objects;
    assert(
        fsim_session_visit_children(
            systemc_session,
            child,
            collect_object,
            &systemc_child_objects)
        == FSIM_STATUS_OK);
    assert(systemc_child_objects.size() == 2);
    for (const auto object : { state, view_object }) {
        assert(
            std::ranges::find(systemc_child_objects, object)
            != systemc_child_objects.end());
    }

    const fsim_string_view_t one { "1", 1 };
    assert(
        fsim_session_deposit(systemc_session, state, one)
        == FSIM_STATUS_OK);
    char systemc_value[2] { };
    size_t systemc_required { };
    assert(
        fsim_session_read_value(
            systemc_session,
            view_object,
            systemc_value,
            sizeof(systemc_value),
            &systemc_required)
        == FSIM_STATUS_OK);
    assert(std::string_view { systemc_value } == "1");
    assert(
        fsim_session_destroy(systemc_session)
        == FSIM_STATUS_OK);
}
