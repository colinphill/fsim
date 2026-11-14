// SPDX-License-Identifier: Apache-2.0
// VPI data-model conformance plug-in: structures, unions, enumerations,
// packed arrays, net arrays, ports, generate scope arrays and instance
// arrays, typespecs, definitions and parameters (IEEE 1800-2023 37.5, 37.10,
// 37.11, 37.14, 37.16-37.18, 37.25, 37.26, 37.28, 37.85).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"

#include <stdio.h>
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

// A string variable announces its changes (IEEE 1800-2023 37.17, 38.36.1).
static PLI_INT32 string_changed(p_cb_data data)
{
    vpi_printf("#c sv changed value=%s\n",
        data->value != NULL && data->value->format == vpiStringVal
            ? text(data->value->value.str)
            : "-");
    return 0;
}

static void string_watch(void)
{
    static s_vpi_value value = { vpiStringVal, { 0 } };
    static s_vpi_time time = { vpiSuppressTime, 0, 0, 0.0 };
    s_cb_data callback;
    vpiHandle object = vpi_handle_by_name("top.sv", NULL);
    vpi_printf("#c sv type=%s\n", text(vpi_get_str(vpiType, object)));
    memset(&callback, 0, sizeof callback);
    callback.reason = cbValueChange;
    callback.cb_rtn = string_changed;
    callback.obj = object;
    callback.time = &time;
    callback.value = &value;
    vpi_printf("#c sv watch=%d\n", vpi_register_cb(&callback) != NULL);
}

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
    string_watch();
    return 0;
}

static const char* name_of(vpiHandle object)
{
    return object != NULL ? text(vpi_get_str(vpiName, object)) : "-";
}

// The ranges of a typespec, leftmost first (37.22, 37.25 detail 10).
static void range_text(vpiHandle typespec, char* out, size_t size)
{
    vpiHandle iterator = vpi_iterate(vpiRange, typespec);
    vpiHandle range;
    size_t used = 0U;
    out[0] = '\0';
    while (iterator != NULL && (range = vpi_scan(iterator)) != NULL) {
        vpiHandle left = vpi_handle(vpiLeftRange, range);
        vpiHandle right = vpi_handle(vpiRightRange, range);
        s_vpi_value l = { vpiIntVal, { 0 } };
        s_vpi_value r = { vpiIntVal, { 0 } };
        if (left == NULL || right == NULL) {
            used += (size_t)snprintf(out + used, size - used, "[]");
        } else {
            vpi_get_value(left, &l);
            vpi_get_value(right, &r);
            used += (size_t)snprintf(out + used, size - used, "[%d:%d]",
                (int)l.value.integer, (int)r.value.integer);
        }
        if (used >= size) {
            break;
        }
    }
}

// A typespec unwound through vpiElemTypespec (37.25 detail 8).
static void typespec_chain(const char* label, vpiHandle typespec)
{
    int level = 0;
    while (typespec != NULL && level < 8) {
        char ranges[128];
        vpiHandle alias = vpi_handle(vpiTypedefAlias, typespec);
        range_text(typespec, ranges, sizeof ranges);
        vpi_printf("#c ts %s.%d type=%s name=%s ranges=%s alias=%s:%s\n",
            label, level, text(vpi_get_str(vpiType, typespec)),
            text(vpi_get_str(vpiName, typespec)), ranges[0] ? ranges : "-",
            alias != NULL ? text(vpi_get_str(vpiType, alias)) : "-",
            alias != NULL ? text(vpi_get_str(vpiName, alias)) : "-");
        typespec = vpi_handle(vpiElemTypespec, typespec);
        ++level;
    }
}

// Typespecs, typespec members and enumeration constants (37.25).
static void typespecs(void)
{
    static char* names[] = { "top.c", "top.t", "top.arr",
        "top.pk", "top.iv", "top.q", "top.la", "top.ly", "top.wa", "top.rc",
        "top.p2v" };
    vpiHandle typespec;
    vpiHandle iterator;
    vpiHandle object;
    for (size_t index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        typespec = vpi_handle(vpiTypespec, vpi_handle_by_name(names[index], NULL));
        typespec_chain(names[index] + 4, typespec);
    }
    typespec = vpi_handle(vpiTypespec, vpi_handle_by_name("top.c", NULL));
    object = vpi_handle(vpiBaseTypespec, typespec);
    vpi_printf("#c c base=%s signed=%d\n",
        object != NULL ? text(vpi_get_str(vpiType, object)) : "-",
        (int)vpi_get(vpiSigned, object));
    iterator = vpi_iterate(vpiEnumConst, typespec);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        s_vpi_value value = { vpiIntVal, { 0 } };
        vpi_get_value(object, &value);
        vpi_printf("#c c const %s type=%s size=%d value=%d\n",
            text(vpi_get_str(vpiName, object)),
            text(vpi_get_str(vpiType, object)), (int)vpi_get(vpiSize, object),
            (int)value.value.integer);
    }
    typespec = vpi_handle(vpiTypespec, vpi_handle_by_name("top.rc", NULL));
    vpi_printf("#c rc packed=%d\n", (int)vpi_get(vpiPacked, typespec));
    iterator = vpi_iterate(vpiTypespecMember, typespec);
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpiHandle member = vpi_handle(vpiTypespec, object);
        vpi_printf("#c rc member %s type=%s typespec=%s rand=%d\n",
            text(vpi_get_str(vpiName, object)),
            text(vpi_get_str(vpiType, object)),
            text(vpi_get_str(vpiType, member)),
            (int)vpi_get(vpiRandType, object));
    }
    typespec = vpi_handle(vpiTypespec, vpi_handle_by_name("top.q", NULL));
    vpi_printf("#c q arraytype=%d\n", (int)vpi_get(vpiArrayType, typespec));
    // A member's typespec is its structure's member's (37.26).
    iterator = vpi_iterate(vpiMember, vpi_handle_by_name("top.pr", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        typespec_chain(text(vpi_get_str(vpiName, object)),
            vpi_handle(vpiTypespec, object));
    }
}

static void late_checks(void);

// Where an instance's definition is (37.10) and parameters (37.28).
static void definitions(void)
{
    static char* names[] = { "top", "top.u0", "top.ua[1]", "top.bus" };
    vpiHandle iterator;
    vpiHandle object;
    for (size_t index = 0U; index < sizeof names / sizeof names[0]; ++index) {
        const char* file;
        const char* base;
        object = vpi_handle_by_name(names[index], NULL);
        file = vpi_get_str(vpiDefFile, object);
        base = file != NULL ? strrchr(file, '/') : NULL;
        vpi_printf("#c def %s file=%s line=%d library=%s cell=%s name=%s\n",
            names[index], base != NULL ? base + 1 : text(file),
            (int)vpi_get(vpiDefLineNo, object),
            text(vpi_get_str(vpiLibrary, object)),
            text(vpi_get_str(vpiCell, object)),
            text(vpi_get_str(vpiDefName, object)));
    }
    iterator = vpi_iterate(vpiParameter, vpi_handle_by_name("top", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        s_vpi_value value = { vpiIntVal, { 0 } };
        const char* parameter = vpi_get_str(vpiName, object);
        // RP and SP are checked in late_checks.
        if (parameter != NULL
            && (strcmp(parameter, "RP") == 0 || strcmp(parameter, "SP") == 0)) {
            continue;
        }
        vpi_get_value(object, &value);
        vpi_printf("#c param %s type=%s local=%d typespec=%s size=%d "
                   "value=%d\n",
            text(vpi_get_str(vpiName, object)),
            text(vpi_get_str(vpiType, object)),
            (int)vpi_get(vpiLocalParam, object),
            text(vpi_get_str(vpiType, vpi_handle(vpiTypespec, object))),
            (int)vpi_get(vpiSize, object), (int)value.value.integer);
    }
    object = vpi_handle_by_name("top.la", NULL);
    vpi_printf("#c la local=%d\n", (int)vpi_get(vpiLocalParam, object));
    vpi_printf("#c top top=%d member=%d u0 top=%d member=%d ua[1] member=%d\n",
        (int)vpi_get(vpiTop, vpi_handle_by_name("top", NULL)),
        (int)vpi_get(vpiArrayMember, vpi_handle_by_name("top", NULL)),
        (int)vpi_get(vpiTop, vpi_handle_by_name("top.u0", NULL)),
        (int)vpi_get(vpiArrayMember, vpi_handle_by_name("top.u0", NULL)),
        (int)vpi_get(vpiArrayMember, vpi_handle_by_name("top.ua[1]", NULL)));
    // An instance array's dimension (37.11).
    object = vpi_handle_by_name("top.ua", NULL);
    {
        char ranges[64];
        s_vpi_value left = { vpiIntVal, { 0 } };
        range_text(object, ranges, sizeof ranges);
        vpi_get_value(vpi_handle(vpiLeftRange, object), &left);
        vpi_printf("#c ua ranges=%s left=%d\n", ranges,
            (int)left.value.integer);
    }
    object = vpi_handle_by_name("top", NULL);
    vpi_printf("#c top nettype=%d unconn=%d delaymode=%d protected=%d\n",
        (int)vpi_get(vpiDefNetType, object),
        (int)vpi_get(vpiUnconnDrive, object),
        (int)vpi_get(vpiDefDelayMode, object),
        (int)vpi_get(vpiProtected, object));
    vpi_printf("#c nettype lz=%d tn=%d implicit lz=%d imp=%d\n",
        (int)vpi_get(vpiNetType, vpi_handle_by_name("top.lz", NULL)),
        (int)vpi_get(vpiNetType, vpi_handle_by_name("top.tn", NULL)),
        (int)vpi_get(vpiImplicitDecl, vpi_handle_by_name("top.lz", NULL)),
        (int)vpi_get(vpiImplicitDecl, vpi_handle_by_name("top.imp", NULL)));
    // Generate scopes: the implicit name, and a genvar's local parameter
    // (37.85 details 2, 3).
    {
        static char* scopes[] = { "top.g[1]", "top.genblk2" };
        for (size_t index = 0U; index < sizeof scopes / sizeof scopes[0];
             ++index) {
            object = vpi_handle_by_name(scopes[index], NULL);
            vpi_printf("#c scope %s found=%d implicit=%d member=%d\n",
                scopes[index], object != NULL ? 1 : 0,
                (int)vpi_get(vpiImplicitDecl, object),
                (int)vpi_get(vpiArrayMember, object));
        }
    }
    iterator = vpi_iterate(vpiParameter, vpi_handle_by_name("top.g[1]", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        s_vpi_value value = { vpiIntVal, { 0 } };
        vpi_get_value(object, &value);
        vpi_printf("#c g[1] param %s local=%d value=%d\n",
            text(vpi_get_str(vpiName, object)),
            (int)vpi_get(vpiLocalParam, object), (int)value.value.integer);
    }
    late_checks();
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
    typespecs();
    definitions();
}

// Port bits, high connections that are expressions, typedefs of a scope and
// a parameter named by a typedef (IEEE 1800-2023 37.5, 37.14, 37.25, 37.28).
static void late_checks(void)
{
    static char* instances[] = { "top.u1", "top.u2", "top.u3" };
    vpiHandle iterator;
    vpiHandle object;
    vpiHandle port;
    int count = 0;
    iterator = vpi_iterate(vpiPort, vpi_handle_by_name("top.u0", NULL));
    while (iterator != NULL && (port = vpi_scan(iterator)) != NULL) {
        vpiHandle bits = vpi_iterate(vpiBit, port);
        count = 0;
        while (bits != NULL && (object = vpi_scan(bits)) != NULL) {
            vpiHandle parent = vpi_handle(vpiParent, object);
            vpi_printf("#c u0 port %s bit %d type=%s size=%d name=%s "
                       "parent=%s\n",
                name_of(port), count, text(vpi_get_str(vpiType, object)),
                (int)vpi_get(vpiSize, object),
                text(vpi_get_str(vpiName, object)), name_of(parent));
            ++count;
        }
        vpi_printf("#c u0 port %s bits=%d\n", name_of(port), count);
    }
    for (size_t index = 0U; index < sizeof instances / sizeof instances[0];
         ++index) {
        vpiHandle high;
        port = vpi_scan(vpi_iterate(vpiPort,
            vpi_handle_by_name(instances[index], NULL)));
        high = vpi_handle(vpiHighConn, port);
        vpi_printf("#c %s.a high type=%s optype=%d parent=%s\n",
            instances[index], high != NULL ? text(vpi_get_str(vpiType, high)) : "-",
            high != NULL ? (int)vpi_get(vpiOpType, high) : -1,
            high != NULL
                ? text(vpi_get_str(vpiType, vpi_handle(vpiParent, high)))
                : "-");
    }
    iterator = vpi_iterate(vpiTypedef, vpi_handle_by_name("top", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpiHandle key = vpi_handle(vpiIndexTypespec, object);
        typespec_chain(text(vpi_get_str(vpiName, object)), object);
        vpi_printf("#c td %s index=%s\n", name_of(object),
            key != NULL ? text(vpi_get_str(vpiType, key)) : "-");
    }
    // Typedefs of a generate scope, a package and $unit (37.85 detail 5,
    // 3.12.1, 37.10).
    {
        static char* owners[] = { "top.g[1]", "pkg_p", "$unit" };
        // A package's full name ends in `::` (37.10).
        static char* lookups[] = { "top.g[1]", "pkg_p::", "$unit" };
        for (size_t index = 0U; index < sizeof owners / sizeof owners[0];
             ++index) {
            int found = 0;
            iterator = vpi_iterate(vpiTypedef,
                vpi_handle_by_name(lookups[index], NULL));
            while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
                const char* typedef_name = vpi_get_str(vpiName, object);
                if (typedef_name == NULL) {
                    continue;
                }
                if (index != 2U || strcmp(typedef_name, "colors") == 0
                    || strcmp(typedef_name, "rec_t") == 0) {
                    vpi_printf("#c typedef %s %s type=%s\n", owners[index],
                        typedef_name, text(vpi_get_str(vpiType, object)));
                }
                ++found;
            }
            vpi_printf("#c typedefs %s found=%d\n", owners[index],
                found != 0);
        }
    }
    // The genvars of an instance, and the implicit scope of an unnamed loop
    // with its genvar as a local parameter (37.85 details 2, 3).
    iterator = vpi_iterate(vpiGenVar, vpi_handle_by_name("top", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c genvar %s type=%s full=%s\n", name_of(object),
            text(vpi_get_str(vpiType, object)),
            text(vpi_get_str(vpiFullName, object)));
    }
    iterator = vpi_iterate(vpiParameter,
        vpi_handle_by_name("top.genblk3[1]", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        s_vpi_value value = { vpiIntVal, { 0 } };
        vpi_get_value(object, &value);
        vpi_printf("#c genblk3[1] param %s local=%d value=%d\n",
            name_of(object), (int)vpi_get(vpiLocalParam, object),
            (int)value.value.integer);
    }
    // A parameter declared with a typedef of a real or a string (37.28).
    {
        s_vpi_value real_value = { vpiRealVal, { 0 } };
        s_vpi_value string_value = { vpiStringVal, { 0 } };
        object = vpi_handle_by_name("top.RP", NULL);
        vpi_get_value(object, &real_value);
        vpi_printf("#c RP type=%s local=%d typespec=%s name=%s value=%.2f\n",
            text(vpi_get_str(vpiType, object)),
            (int)vpi_get(vpiLocalParam, object),
            text(vpi_get_str(vpiType, vpi_handle(vpiTypespec, object))),
            text(vpi_get_str(vpiName, vpi_handle(vpiTypespec, object))),
            real_value.value.real);
        object = vpi_handle_by_name("top.SP", NULL);
        vpi_get_value(object, &string_value);
        vpi_printf("#c SP type=%s local=%d typespec=%s name=%s value=%s\n",
            text(vpi_get_str(vpiType, object)),
            (int)vpi_get(vpiLocalParam, object),
            text(vpi_get_str(vpiType, vpi_handle(vpiTypespec, object))),
            text(vpi_get_str(vpiName, vpi_handle(vpiTypespec, object))),
            text(string_value.value.str));
    }
    // More than one delay is an operation of vpiOpType vpiListOp (37.47).
    iterator = vpi_iterate(vpiContAssign, vpi_handle_by_name("top", NULL));
    while (iterator != NULL && (object = vpi_scan(iterator)) != NULL) {
        vpiHandle delay = vpi_handle(vpiDelay, object);
        vpiHandle operands;
        vpiHandle operand;
        int operand_count = 0;
        char values[64] = "";
        if (delay == NULL) {
            continue;
        }
        operands = vpi_iterate(vpiOperand, delay);
        while (operands != NULL && (operand = vpi_scan(operands)) != NULL) {
            s_vpi_value value = { vpiIntVal, { 0 } };
            vpi_get_value(operand, &value);
            snprintf(values + strlen(values), sizeof values - strlen(values),
                "%s%d", operand_count != 0 ? "," : "", (int)value.value.integer);
            ++operand_count;
        }
        vpi_printf("#c delay type=%s optype=%d operands=%d values=%s\n",
            text(vpi_get_str(vpiType, delay)), (int)vpi_get(vpiOpType, delay),
            operand_count, values);
    }
    object = vpi_handle(vpiTypespec, vpi_handle_by_name("top.NP", NULL));
    typespec_chain("NP", object);
    object = vpi_handle_by_name("top.NP", NULL);
    vpi_printf("#c NP type=%s local=%d size=%d\n",
        text(vpi_get_str(vpiType, object)), (int)vpi_get(vpiLocalParam, object),
        (int)vpi_get(vpiSize, object));
    // An unpacked structure parameter is a constant structure var: members
    // by iteration and by name read from its value, its typedef's typespec,
    // and it iterates with the variables, not the parameters.
    {
        s_vpi_value member_value = { vpiIntVal, { 0 } };
        object = vpi_handle_by_name("top.RS", NULL);
        show("RS", object);
        members("RS", object, vpiMember);
        vpi_get_value(vpi_handle_by_name("top.RS.a", NULL), &member_value);
        vpi_printf("#c RS.a int=%d\n", (int)member_value.value.integer);
        show_hex("RS.b", vpi_handle_by_name("top.RS.b", NULL));
        typespec_chain("RS", vpi_handle(vpiTypespec, object));
        vpi_printf("#c RS scope parameter=%d variables=%d\n",
            in_scope(vpiParameter, "RS"), in_scope(vpiVariables, "RS"));
    }
    // The else branch of a generate if: its scope, its variable and the
    // named block of its process; the if branch has no scope (27.5).
    object = vpi_handle_by_name("top.ge_else", NULL);
    vpi_printf("#c else scope type=%s name=%s\n",
        text(vpi_get_str(vpiType, object)), text(vpi_get_str(vpiName, object)));
    object = vpi_handle_by_name("top.ge_else.b1", NULL);
    vpi_printf("#c else variable type=%s full=%s\n",
        text(vpi_get_str(vpiType, object)),
        text(vpi_get_str(vpiFullName, object)));
    object = vpi_handle_by_name("top.ge_else.eb", NULL);
    vpi_printf("#c else block type=%s full=%s parent=%s\n",
        text(vpi_get_str(vpiType, object)),
        text(vpi_get_str(vpiFullName, object)),
        object != NULL
            ? text(vpi_get_str(vpiName, vpi_handle(vpiScope, object)))
            : "-");
    vpi_printf("#c else absent if=%d\n",
        vpi_handle_by_name("top.ge_if", NULL) == NULL);
    object = vpi_handle_by_name("top.shr.sb", NULL);
    vpi_printf("#c shared else block type=%s s2=%d s1=%d\n",
        text(vpi_get_str(vpiType, object)),
        vpi_handle_by_name("top.shr.s2", NULL) != NULL,
        vpi_handle_by_name("top.shr.s1", NULL) != NULL);
    // Its statements are the selected branch's (27.5).
    {
        vpiHandle const statements
            = object != NULL ? vpi_iterate(vpiStmt, object) : NULL;
        vpiHandle const statement
            = statements != NULL ? vpi_scan(statements) : NULL;
        vpi_printf("#c shared else block stmt=%s lhs=%s\n",
            statement != NULL ? text(vpi_get_str(vpiType, statement)) : "-",
            statement != NULL
                ? text(vpi_get_str(vpiName, vpi_handle(vpiLhs, statement)))
                : "-");
        if (statement != NULL) {
            vpi_free_object(statements);
        }
    }
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
