// SPDX-License-Identifier: Apache-2.0
// VPI data-model conformance plug-in: structures, unions, enumerations,
// packed arrays, net arrays, ports, generate scope arrays and instance
// arrays (IEEE 1800-2023 37.11, 37.14, 37.16-37.18, 37.26, 37.85).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <string.h>

static const char* text(const char* value)
{
    return value != NULL ? value : "-";
}

static void show(const char* label, vpiHandle object)
{
    if (object == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    vpi_printf("#c %s type=%s name=%s full=%s size=%d vector=%d packed=%d\n",
        label, text(vpi_get_str(vpiType, object)),
        text(vpi_get_str(vpiName, object)),
        text(vpi_get_str(vpiFullName, object)), (int)vpi_get(vpiSize, object),
        (int)vpi_get(vpiVector, object), (int)vpi_get(vpiPacked, object));
}

static void show_hex(const char* label, vpiHandle object)
{
    s_vpi_value value = { vpiHexStrVal, { 0 } };
    if (object == NULL) {
        vpi_printf("#c %s missing\n", label);
        return;
    }
    vpi_get_value(object, &value);
    vpi_printf("#c %s hex=%s\n", label,
        value.format == vpiHexStrVal ? text(value.value.str) : "none");
}

static void members(const char* label, vpiHandle object, PLI_INT32 relation)
{
    vpiHandle iterator = vpi_iterate(relation, object);
    vpiHandle member;
    int count = 0;
    while (iterator != NULL && (member = vpi_scan(iterator)) != NULL) {
        vpiHandle parent = vpi_handle(vpiParent, member);
        vpi_printf("#c %s[%d] type=%s name=%s full=%s parent=%s su=%d pa=%d\n",
            label, count, text(vpi_get_str(vpiType, member)),
            text(vpi_get_str(vpiName, member)),
            text(vpi_get_str(vpiFullName, member)),
            parent != NULL ? text(vpi_get_str(vpiName, parent)) : "-",
            (int)vpi_get(vpiStructUnionMember, member),
            (int)vpi_get(vpiPackedArrayMember, member));
        ++count;
    }
    vpi_printf("#c %s count=%d\n", label, count);
}

static int in_scope(PLI_INT32 relation, const char* name)
{
    vpiHandle top = vpi_handle_by_name("top", NULL);
    vpiHandle iterator = vpi_iterate(relation, top);
    vpiHandle object;
    int found = 0;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        const char* object_name = vpi_get_str(vpiName, object);
        if (object_name != NULL && strcmp(object_name, name) == 0) {
            found = 1;
        }
    }
    return found;
}

static void hierarchy(void);

static PLI_INT32 check(p_cb_data data)
{
    vpiHandle pr = vpi_handle_by_name("top.pr", NULL);
    vpiHandle rc = vpi_handle_by_name("top.rc", NULL);
    vpiHandle un = vpi_handle_by_name("top.un", NULL);
    vpiHandle st = vpi_handle_by_name("top.st", NULL);
    vpiHandle pa = vpi_handle_by_name("top.pa", NULL);
    vpiHandle pk = vpi_handle_by_name("top.pk", NULL);
    vpiHandle ra = vpi_handle_by_name("top.ra", NULL);
    vpiHandle wa = vpi_handle_by_name("top.wa", NULL);
    s_vpi_value value = { vpiIntVal, { 0 } };
    (void)data;

    // A packed structure: a vector of its bits, members by name (37.26).
    show("pr", pr);
    members("pr", pr, vpiMember);
    show_hex("pr", pr);
    show_hex("pr.hi", vpi_handle_by_name("top.pr.hi", NULL));
    show_hex("pr.lo", vpi_handle_by_name("top.pr.lo", NULL));

    // An unpacked structure: its size is the number of members and it has
    // no value (37.26 details 5, 7).
    show("rc", rc);
    members("rc", rc, vpiMember);
    vpi_get_value(vpi_handle_by_name("top.rc.a", NULL), &value);
    vpi_printf("#c rc.a int=%d\n", (int)value.value.integer);
    show_hex("rc.b", vpi_handle_by_name("top.rc.b", NULL));

    // A packed union and a structure member of it.
    show("un", un);
    members("un", un, vpiMember);
    show("un.p", vpi_handle_by_name("top.un.p", NULL));
    show_hex("un.p.hi", vpi_handle_by_name("top.un.p.hi", NULL));

    // An enumeration variable (37.17).
    show("st", st);
    value.format = vpiIntVal;
    vpi_get_value(st, &value);
    vpi_printf("#c st int=%d\n", (int)value.value.integer);

    // A packed array of structures: its size in bits, one dimension per
    // vpiElement, the largest containing packed array as parent (37.18).
    show("pa", pa);
    members("pa", pa, vpiElement);
    show("pa[1]", vpi_handle_by_index(pa, 1));
    show_hex("pa[1]", vpi_handle_by_index(pa, 1));
    show_hex("pa[0].lo", vpi_handle_by_name("top.pa[0].lo", NULL));

    // A logic variable with several packed dimensions stays a logic var.
    show("pk", pk);

    // Arrays of variables and of nets (37.16, 37.17).
    show("ra", ra);
    show("ra[1]", vpi_handle_by_index(ra, 1));
    show_hex("ra[1]", vpi_handle_by_index(ra, 1));
    show("wa", wa);
    show("wa[1]", vpi_handle_by_index(wa, 1));
    show_hex("wa[1]", vpi_handle_by_index(wa, 1));

    // vpiReg returns logic vars only; vpiVariables every variable.
    vpi_printf("#c reg pk=%d pr=%d variables pr=%d rc=%d st=%d pa=%d\n",
        in_scope(vpiReg, "pk"), in_scope(vpiReg, "pr"),
        in_scope(vpiVariables, "pr"), in_scope(vpiVariables, "rc"),
        in_scope(vpiVariables, "st"), in_scope(vpiVariables, "pa"));
    vpi_printf("#c netarray wa=%d regarray ra=%d wa=%d\n",
        in_scope(vpiNetArray, "wa"), in_scope(vpiRegArray, "ra"),
        in_scope(vpiRegArray, "wa"));
    hierarchy();
    return 0;
}

static const char* name_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiName, object)) : "-";
}

// Modports and an interface port's reference object (37.7, 37.15).
static void interfaces(void)
{
    vpiHandle bus = vpi_handle_by_name("top.bus", NULL);
    vpiHandle iterator = vpi_iterate(vpiModport, bus);
    vpiHandle object;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpiHandle io = vpi_iterate(vpiIODecl, object);
        vpiHandle declaration;
        vpi_printf("#c modport %s type=%s interface=%s\n", name_of(object),
            text(vpi_get_str(vpiType, object)),
            text(vpi_get_str(vpiFullName, vpi_handle(vpiInterface, object))));
        while (io != NULL && (declaration = vpi_scan(io)) != NULL) {
            vpi_printf("#c io %s type=%s dir=%d expr=%s\n",
                name_of(declaration), text(vpi_get_str(vpiType, declaration)),
                (int)vpi_get(vpiDirection, declaration),
                text(vpi_get_str(vpiFullName,
                    vpi_handle(vpiExpr, declaration))));
        }
    }
    iterator = vpi_iterate(vpiPort, vpi_handle_by_name("top.us", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpiHandle low = vpi_handle(vpiLowConn, object);
        vpiHandle actual = vpi_handle(vpiActual, low);
        vpi_printf("#c us port %s porttype=%d high=%s low=%s lowfull=%s "
                   "actual=%s:%s\n",
            name_of(object), (int)vpi_get(vpiPortType, object),
            text(vpi_get_str(vpiFullName, vpi_handle(vpiHighConn, object))),
            text(vpi_get_str(vpiType, low)),
            text(vpi_get_str(vpiFullName, low)),
            text(vpi_get_str(vpiType, actual)), name_of(actual));
    }
    object = vpi_handle_by_name("top.us.b", NULL);
    vpi_printf("#c us.b type=%s\n", text(vpi_get_str(vpiType, object)));
    object = vpi_handle_by_name("top.us.b.data", NULL);
    vpi_printf("#c us.b.data full=%s\n", text(vpi_get_str(vpiFullName, object)));
    show_hex("us.b.data", object);
}

// Ports, generate scope arrays and instance arrays (37.11, 37.14, 37.85).
static void hierarchy(void)
{
    vpiHandle u0 = vpi_handle_by_name("top.u0", NULL);
    vpiHandle g = vpi_handle_by_name("top.g", NULL);
    vpiHandle ua = vpi_handle_by_name("top.ua", NULL);
    vpiHandle iterator = vpi_iterate(vpiPort, u0);
    vpiHandle object;
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c port %s type=%s index=%d dir=%d size=%d byname=%d "
                   "low=%s high=%s full=%s\n",
            name_of(object), text(vpi_get_str(vpiType, object)),
            (int)vpi_get(vpiPortIndex, object),
            (int)vpi_get(vpiDirection, object), (int)vpi_get(vpiSize, object),
            (int)vpi_get(vpiConnByName, object),
            text(vpi_get_str(vpiFullName, vpi_handle(vpiLowConn, object))),
            text(vpi_get_str(vpiFullName, vpi_handle(vpiHighConn, object))),
            text(vpi_get_str(vpiFullName, object)));
    }
    object = vpi_handle_by_name("top.u0.a", NULL);
    vpi_printf("#c u0.a type=%s dir=%d ports=%s\n",
        text(vpi_get_str(vpiType, object)), (int)vpi_get(vpiDirection, object),
        name_of(vpi_scan(vpi_iterate(vpiPorts, object))));
    vpi_printf("#c la portinst=%s\n",
        name_of(vpi_scan(vpi_iterate(vpiPortInst,
            vpi_handle_by_name("top.la", NULL)))));
    iterator = vpi_iterate(vpiPort, vpi_handle_by_name("top.ua[0]", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c ua[0] port %s byname=%d high=%s\n", name_of(object),
            (int)vpi_get(vpiConnByName, object),
            text(vpi_get_str(vpiFullName, vpi_handle(vpiHighConn, object))));
    }

    vpi_printf("#c g type=%s full=%s size=%d\n", text(vpi_get_str(vpiType, g)),
        text(vpi_get_str(vpiFullName, g)), (int)vpi_get(vpiSize, g));
    iterator = vpi_iterate(vpiGenScope, g);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c g element %s\n", text(vpi_get_str(vpiFullName, object)));
    }
    vpi_printf("#c g[1] %s\n",
        text(vpi_get_str(vpiFullName, vpi_handle_by_index(g, 1))));
    vpi_printf("#c ua type=%s size=%d\n", text(vpi_get_str(vpiType, ua)),
        (int)vpi_get(vpiSize, ua));
    iterator = vpi_iterate(vpiModule, ua);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c ua element %s\n", text(vpi_get_str(vpiFullName, object)));
    }
    // The bits of a vector, left to right (37.16, 37.17).
    iterator = vpi_iterate(vpiBit, vpi_handle_by_name("top.la", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        s_vpi_value bit = { vpiBinStrVal, { 0 } };
        vpi_get_value(object, &bit);
        vpi_printf("#c la bit %s type=%s value=%s\n",
            text(vpi_get_str(vpiName, object)),
            text(vpi_get_str(vpiType, object)), text(bit.value.str));
    }
    vpi_printf("#c scopes g=%d g[0]=%d genarray g=%d modulearray ua=%d\n",
        in_scope(vpiInternalScope, "g"), in_scope(vpiInternalScope, "g[0]"),
        in_scope(vpiGenScopeArray, "g"), in_scope(vpiModuleArray, "ua"));
    interfaces();
}

static PLI_INT32 start(p_cb_data data)
{
    s_cb_data callback;
    s_vpi_time time = { vpiSimTime, 0, 1, 0.0 };
    (void)data;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbAfterDelay;
    callback.cb_rtn = check;
    callback.time = &time;
    vpi_register_cb(&callback);
    return 0;
}

static void register_start(void)
{
    s_cb_data callback;
    memset(&callback, 0, sizeof callback);
    callback.reason = cbStartOfSimulation;
    callback.cb_rtn = start;
    vpi_register_cb(&callback);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { register_start, 0 };
