// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 access routine conformance plug-in (IEEE 1364-2001 Clauses 22
// and 23): the tasks $acc_walk, $acc_av, $acc_watch and $acc_unwatch are registered
// through a veriusertfs table. Lines starting with "#c " form the checked
// trace.
#include "fsim/runtime/acc_user.h"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <stdio.h>
#include <string.h>

static void copy(char* out, size_t size, const char* text)
{
    snprintf(out, size, "%s", text != NULL ? text : "(null)");
}

// ACC returns names in one buffer, so each is copied before the next call.
static void describe(const char* label, handle object)
{
    char name[128];
    char full[256];
    if (object == NULL) {
        vpi_printf("#c %s (null)\n", label);
        return;
    }
    copy(name, sizeof name, acc_fetch_name(object));
    copy(full, sizeof full, acc_fetch_fullname(object));
    vpi_printf("#c %s name=%s full=%s type=%d fulltype=%d\n", label, name, full,
        (int)acc_fetch_type(object), (int)acc_fetch_fulltype(object));
}

static void show_value(const char* label, handle object, char* format)
{
    char text[128];
    copy(text, sizeof text, acc_fetch_value(object, format, NULL));
    vpi_printf("#c %s %s\n", label, text);
}

static void walk_hierarchy(handle top)
{
    handle child = NULL;
    handle net = NULL;
    handle variable = NULL;
    handle parameter = NULL;
    PLI_INT32 variable_types[3] = { accReg, accIntegerVar, 0 };
    PLI_INT32 count = 0;
    handle u1 = NULL;

    describe("module", top);
    describe("top parent", acc_handle_parent(top));
    while ((child = acc_next_child(top, child)) != NULL) {
        char definition[128];
        describe("child", child);
        copy(definition, sizeof definition, acc_fetch_defname(child));
        vpi_printf("#c child defname=%s\n", definition);
        u1 = child;
    }
    count = acc_count(acc_next_child, top);
    vpi_printf("#c children=%d\n", (int)count);
    while ((net = acc_next_net(top, net)) != NULL) {
        describe("net", net);
    }
    while ((variable = acc_next(variable_types, top, variable)) != NULL) {
        describe("variable", variable);
    }
    while ((parameter = acc_next_parameter(top, parameter)) != NULL) {
        describe("parameter", parameter);
        vpi_printf("#c parameter value=%g paramtype=%d\n",
            acc_fetch_paramval(parameter), (int)acc_fetch_paramtype(parameter));
    }
    if (u1 != NULL) {
        handle port = NULL;
        handle primitive = NULL;
        handle terminal = NULL;
        handle wire = acc_handle_by_name("w", u1);
        while ((port = acc_next_port(u1, port)) != NULL) {
            describe("port", port);
            vpi_printf("#c port direction=%d index=%d\n",
                (int)acc_fetch_direction(port), (int)acc_fetch_index(port));
        }
        while ((primitive = acc_next_primitive(u1, primitive)) != NULL) {
            describe("primitive", primitive);
            while ((terminal = acc_next_terminal(primitive, terminal)) != NULL) {
                vpi_printf("#c terminal type=%d fulltype=%d direction=%d index=%d\n",
                    (int)acc_fetch_type(terminal),
                    (int)acc_fetch_fulltype(terminal),
                    (int)acc_fetch_direction(terminal),
                    (int)acc_fetch_index(terminal));
            }
        }
        describe("wire parent", acc_handle_parent(wire));
        describe("wire scope", acc_handle_scope(wire));
    }
}

static void query_objects(handle top)
{
    handle vec = acc_handle_by_name("vec", top);
    PLI_INT32 msb = 0;
    PLI_INT32 lsb = 0;
    PLI_INT32 ranged;
    char* type_name;

    vpi_printf("#c size=%d\n", (int)acc_fetch_size(vec));
    ranged = acc_fetch_range(vec, &msb, &lsb);
    vpi_printf("#c range=%d msb=%d lsb=%d\n", (int)ranged, (int)msb, (int)lsb);
    show_value("vec", vec, "%b");
    vpi_printf("#c oftype reg=%d net=%d\n", (int)acc_object_of_type(vec, accReg),
        (int)acc_object_of_type(vec, accNet));
    type_name = acc_fetch_type_str(accNet);
    vpi_printf("#c type name=%s\n", type_name != NULL ? type_name : "(null)");

    // acc_handle_by_name with a null scope searches the calling module.
    describe("by name local", acc_handle_by_name("out", NULL));
    describe("by name nested", acc_handle_by_name("u1.w", NULL));
    describe("by name absolute", acc_handle_by_name("top.out", NULL));
    describe("by name missing", acc_handle_by_name("nosuch.x", NULL));
    describe("by name upward", acc_handle_by_name("out", acc_handle_by_name("u1", NULL)));
    describe("by object", acc_handle_object("u1.y"));
}

static void check_configuration(handle top)
{
    handle vec = acc_handle_by_name("vec", top);
    s_setval_value value;
    s_setval_delay delay;
    s_acc_vecval vector;

    // The default of a missing attribute comes from the call, until
    // accDefaultAttr0 asks for zero; initialization restores it.
    vpi_printf("#c attribute default=%d\n",
        (int)acc_fetch_attribute_int(vec, "nosuch", 7));
    vpi_printf("#c configure=%d\n", (int)acc_configure(accDefaultAttr0, "true"));
    vpi_printf("#c attribute zero=%d\n", (int)acc_fetch_attribute_int(vec, "nosuch"));
    (void)acc_initialize();
    vpi_printf("#c attribute reset=%d\n",
        (int)acc_fetch_attribute_int(vec, "nosuch", 9));

    // An error sets the flag whether or not it is displayed (22.7).
    (void)acc_configure(accDisplayErrors, "false");
    (void)acc_fetch_name(NULL);
    vpi_printf("#c error flag=%d\n", (int)acc_error_flag);
    (void)acc_initialize();
    (void)acc_fetch_name(NULL);
    vpi_printf("#c error flag=%d\n", (int)acc_error_flag);

    // Procedural assign and deassign (23.96).
    memset(&value, 0, sizeof value);
    memset(&delay, 0, sizeof delay);
    vector.aval = 5;
    vector.bval = 0;
    value.format = accVectorVal;
    value.value.vector = &vector;
    delay.model = accAssignFlag;
    vpi_printf("#c assign=%d\n", (int)acc_set_value(vec, &value, &delay));
    show_value("vec assigned", vec, "%b");
    value.format = accBinStrVal;
    delay.model = accDeassignFlag;
    vpi_printf("#c deassign=%d\n", (int)acc_set_value(vec, &value, &delay));
    show_value("vec deassigned", vec, "%b");
}

static PLI_INT32 walk_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    handle top;
    (void)data;
    (void)reason;
    (void)acc_initialize();
    top = acc_handle_parent(acc_handle_tfinst());
    walk_hierarchy(top);
    query_objects(top);
    check_configuration(top);
    acc_close();
    return 0;
}

// One step of the assign layer check: $acc_av(reg, op) assigns 4'b0101
// (1), forces 4'b1100 (2), releases (3), deassigns (4) or only shows (5)
// the register; the value after the step follows (IEEE 1364-2005 9.3,
// 23.96). Step 6 releases without showing, for a net that returns to its
// driver only when the simulation runs on.
static PLI_INT32 av_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    handle reg;
    int op;
    int rc = 0;
    s_setval_value value;
    s_setval_delay delay;
    s_acc_vecval vector;
    char label[32];
    (void)data;
    (void)reason;
    (void)acc_initialize();
    reg = acc_handle_tfarg(1);
    op = (int)tf_getp(2);
    memset(&value, 0, sizeof value);
    memset(&delay, 0, sizeof delay);
    vector.aval = op == 2 ? 12 : 5;
    vector.bval = 0;
    value.format = accVectorVal;
    value.value.vector = &vector;
    if (op == 1) {
        delay.model = accAssignFlag;
    } else if (op == 2) {
        delay.model = accForceFlag;
    } else if (op == 3 || op == 6) {
        delay.model = accReleaseFlag;
    } else if (op == 4) {
        delay.model = accDeassignFlag;
    }
    if ((op >= 1 && op <= 4) || op == 6) {
        rc = (int)acc_set_value(reg, &value, &delay);
    }
    if (op == 6) {
        acc_close();
        return rc;
    }
    snprintf(label, sizeof label, "av op=%d rc=%d", op, rc);
    show_value(label, reg, "%b");
    acc_close();
    return 0;
}

static char tag[] = "link";

static PLI_INT32 consumer(p_vc_record record)
{
    vpi_printf("#c vcl tag=%s reason=%d value=%d\n", record->user_data,
        (int)record->vc_reason, (int)record->out_value.logic_value);
    return 0;
}

static PLI_INT32 watch_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    (void)acc_initialize();
    acc_vcl_add(acc_handle_tfarg(1), consumer, tag, vcl_verilog_logic);
    vpi_printf("#c vcl added flag=%d\n", (int)acc_error_flag);
    acc_close();
    return 0;
}

static PLI_INT32 unwatch_calltf(PLI_INT32 data, PLI_INT32 reason)
{
    (void)data;
    (void)reason;
    (void)acc_initialize();
    acc_vcl_delete(acc_handle_tfarg(1), consumer, tag, vcl_verilog_logic);
    vpi_printf("#c vcl deleted flag=%d\n", (int)acc_error_flag);
    acc_close();
    return 0;
}

static s_tfcell table[] = {
    { usertask, 0, NULL, NULL, walk_calltf, NULL, "$acc_walk", 0, NULL, NULL, { 0 } },
    { usertask, 0, NULL, NULL, av_calltf, NULL, "$acc_av", 0, NULL, NULL, { 0 } },
    { usertask, 0, NULL, NULL, watch_calltf, NULL, "$acc_watch", 0, NULL, NULL, { 0 } },
    { usertask, 0, NULL, NULL, unwatch_calltf, NULL, "$acc_unwatch", 0, NULL, NULL, { 0 } },
    { 0, 0, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, { 0 } },
};

static void register_table(void)
{
    veriusertfs_register_table(table);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_table, 0 };
