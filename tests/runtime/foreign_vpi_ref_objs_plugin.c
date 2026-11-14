// SPDX-License-Identifier: Apache-2.0
// Reference objects (IEEE 1800-2023 37.15). Lines starting with "#c " form
// the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "none";
}

static const char* full(vpiHandle object)
{
    const char* name;
    if (object == NULL) {
        return "null";
    }
    name = vpi_get_str(vpiFullName, object);
    return name != NULL ? name : text(vpi_get_str(vpiName, object));
}

// A ref obj: its name, size, parent, actual and typespec.
static void describe(const char* tag, vpiHandle ref)
{
    vpiHandle parent;
    vpiHandle actual;
    vpiHandle typespec;
    if (ref == NULL) {
        vpi_printf("#c %s null\n", tag);
        return;
    }
    parent = vpi_handle(vpiParent, ref);
    actual = vpi_handle(vpiActual, ref);
    typespec = vpi_handle(vpiTypespec, ref);
    vpi_printf("#c %s type=%d name=%s size=%d parent=%s actual=%s "
               "typespec=%d\n",
        tag, (int)vpi_get(vpiType, ref), text(vpi_get_str(vpiName, ref)),
        (int)vpi_get(vpiSize, ref),
        parent != NULL ? "ref" : "none", full(actual),
        typespec != NULL ? (int)vpi_get(vpiType, typespec) : 0);
}

static void ports(void)
{
    vpiHandle instance = vpi_handle_by_name("top.u1", NULL);
    vpiHandle list = instance != NULL ? vpi_iterate(vpiPort, instance) : NULL;
    vpiHandle port;
    while (list != NULL && (port = vpi_scan(list)) != NULL) {
        vpiHandle low = vpi_handle(vpiLowConn, port);
        vpi_printf("#c port %s lowconn-type=%d\n", text(vpi_get_str(vpiName, port)),
            low != NULL ? (int)vpi_get(vpiType, low) : 0);
        if (low != NULL && vpi_get(vpiType, low) == vpiRefObj) {
            describe("port-ref", low);
        }
    }
}

// The uses in the initial block: r[0] = 1'b0.
static void uses(void)
{
    vpiHandle instance = vpi_handle_by_name("top.u1", NULL);
    vpiHandle processes
        = instance != NULL ? vpi_iterate(vpiProcess, instance) : NULL;
    vpiHandle process;
    while (processes != NULL && (process = vpi_scan(processes)) != NULL) {
        vpiHandle body = vpi_handle(vpiStmt, process);
        vpiHandle list = body != NULL ? vpi_iterate(vpiStmt, body) : NULL;
        vpiHandle statement;
        while (list != NULL && (statement = vpi_scan(list)) != NULL) {
            vpiHandle lhs = vpi_handle(vpiLhs, statement);
            vpiHandle parent;
            if (lhs == NULL || vpi_get(vpiType, lhs) != vpiRefObj) {
                continue;
            }
            describe("use", lhs);
            parent = vpi_handle(vpiParent, lhs);
            if (parent != NULL) {
                describe("use-parent", parent);
            }
        }
    }
}

// The ref formal of task poke, outside and inside a frame.
static void formals(const char* tag)
{
    vpiHandle task = vpi_handle_by_name("top.u1.poke", NULL);
    vpiHandle list = task != NULL ? vpi_iterate(vpiIODecl, task) : NULL;
    vpiHandle decl;
    while (list != NULL && (decl = vpi_scan(list)) != NULL) {
        vpiHandle expr = vpi_handle(vpiExpr, decl);
        char label[64];
        strcpy(label, tag);
        strcat(label, "-");
        strcat(label, text(vpi_get_str(vpiName, decl)));
        if (expr == NULL) {
            vpi_printf("#c %s no ref obj\n", label);
        } else {
            describe(label, expr);
        }
    }
}

static PLI_INT32 frame_start(p_cb_data data)
{
    /* Each frame the call of a ref formal starts: the formal's ref obj, whose
       actual is bound to the running frame (frames of other callables have
       none). */
    vpiHandle task = vpi_handle_by_name("top.u1.poke", NULL);
    vpiHandle list = task != NULL ? vpi_iterate(vpiIODecl, task) : NULL;
    vpiHandle decl;
    (void)data;
    while (list != NULL && (decl = vpi_scan(list)) != NULL) {
        vpiHandle expr = vpi_handle(vpiExpr, decl);
        if (expr != NULL && vpi_handle(vpiActual, expr) != NULL) {
            describe("frame", expr);
        }
    }
    return 0;
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    (void)data;
    ports();
    uses();
    formals("static");
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfFrame;
    callback.cb_rtn = frame_start;
    (void)vpi_register_cb(&callback);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };
