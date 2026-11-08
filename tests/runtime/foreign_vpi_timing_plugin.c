// SPDX-License-Identifier: Apache-2.0
// VPI module path and timing check conformance plug-in (IEEE 1800-2023
// 37.39, 37.40, 38.10, 38.36.3): the paths of a module with their types,
// polarities, conditions and terms, its timing checks with their terms,
// notifiers and limits, and cbTchkViolation callbacks that run when the
// stimulus violates $setup and $width. Lines starting with "#c " form the
// checked trace.
#include "fsim/runtime/vpi_abi.h"
#include "vpi_user.h"

#include <string.h>

static const char* name_of(vpiHandle handle)
{
    const char* name;
    if (handle == NULL) {
        return "NULL";
    }
    name = vpi_get_str(vpiFullName, handle);
    return name == NULL ? "?" : name;
}

static const char* polarity_name(PLI_INT32 value)
{
    return value == vpiPositive ? "positive"
        : value == vpiNegative  ? "negative"
        : value == vpiUnknown   ? "unknown"
                                : "?";
}

static const char* direction_name(PLI_INT32 value)
{
    return value == vpiInput ? "input"
        : value == vpiOutput ? "output"
        : value == vpiInout  ? "inout"
                             : "?";
}

static const char* check_name(PLI_INT32 value)
{
    switch (value) {
    case vpiSetup:
        return "setup";
    case vpiHold:
        return "hold";
    case vpiSetupHold:
        return "setuphold";
    case vpiWidth:
        return "width";
    case vpiPeriod:
        return "period";
    case vpiSkew:
        return "skew";
    case vpiRecovery:
        return "recovery";
    case vpiRemoval:
        return "removal";
    case vpiRecrem:
        return "recrem";
    case vpiNoChange:
        return "nochange";
    case vpiFullskew:
        return "fullskew";
    case vpiTimeskew:
        return "timeskew";
    default:
        return "?";
    }
}

static void print_terms(const char* label, vpiHandle path, PLI_INT32 relation)
{
    vpiHandle iterator = vpi_iterate(relation, path);
    vpiHandle term;
    while (iterator != NULL && (term = vpi_scan(iterator)) != NULL) {
        vpi_printf("#c   %s %s type=%s direction=%s edge=0x%x\n", label,
            name_of(vpi_handle(vpiExpr, term)), vpi_get_str(vpiType, term),
            direction_name(vpi_get(vpiDirection, term)),
            (unsigned)vpi_get(vpiEdge, term));
    }
}

// The delay object of a path (37.39 vpiDelay): a constant, the vpiListOp
// operation of several delays, or the vpiMinTypMaxOp operation of a triple,
// with its operands.
static void print_delay_object(vpiHandle object, int depth)
{
    const PLI_INT32 type = vpi_get(vpiType, object);
    vpiHandle iterator;
    vpiHandle operand;
    if (type == vpiConstant) {
        vpi_printf("#c %*sdelay %s %s\n", depth * 2, "",
            vpi_get_str(vpiType, object), vpi_get_str(vpiDecompile, object));
        return;
    }
    vpi_printf("#c %*sdelay %s op=%s\n", depth * 2, "",
        vpi_get_str(vpiType, object),
        vpi_get(vpiOpType, object) == vpiListOp         ? "list"
            : vpi_get(vpiOpType, object) == vpiMinTypMaxOp ? "mintypmax"
                                                           : "?");
    iterator = vpi_iterate(vpiOperand, object);
    while (iterator != NULL && (operand = vpi_scan(iterator)) != NULL) {
        print_delay_object(operand, depth + 1);
    }
}

// vpi_get_delays with mtm_flag on a path: the first transition's minimum,
// typical and maximum values (38.10).
static void print_path_mtm(vpiHandle path)
{
    s_vpi_time times[3];
    s_vpi_delay delays;
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = 1;
    delays.time_type = vpiSimTime;
    delays.mtm_flag = 1;
    delays.append_flag = 0;
    delays.pulsere_flag = 0;
    vpi_get_delays(path, &delays);
    vpi_printf("#c   mtm %u,%u,%u\n", (unsigned)times[0].low,
        (unsigned)times[1].low, (unsigned)times[2].low);
}

static void print_path(int index, vpiHandle path)
{
    vpi_printf("#c path %d type=%s pathtype=%d polarity=%s datapolarity=%s "
               "ifnone=%d line=%d module=%s instance=%s condition=%s file=%s\n",
        index, vpi_get_str(vpiType, path), (int)vpi_get(vpiPathType, path),
        polarity_name(vpi_get(vpiPolarity, path)),
        polarity_name(vpi_get(vpiDataPolarity, path)),
        (int)vpi_get(vpiModPathHasIfNone, path), (int)vpi_get(vpiLineNo, path),
        name_of(vpi_handle(vpiModule, path)),
        name_of(vpi_handle(vpiInstance, path)),
        name_of(vpi_handle(vpiCondition, path)),
        strstr(vpi_get_str(vpiFile, path), "foreign_vpi_timing.sv") != NULL
            ? "ok"
            : "bad");
    print_terms("in", path, vpiModPathIn);
    print_terms("out", path, vpiModPathOut);
    print_terms("data", path, vpiModDataPathIn);
    print_delay_object(vpi_handle(vpiDelay, path), 1);
    print_path_mtm(path);
}

static void print_check_term(const char* label, vpiHandle term)
{
    if (term == NULL) {
        vpi_printf("#c   %s NULL\n", label);
        return;
    }
    vpi_printf("#c   %s %s type=%s edge=0x%x condition=%s\n", label,
        name_of(vpi_handle(vpiExpr, term)), vpi_get_str(vpiType, term),
        (unsigned)vpi_get(vpiEdge, term),
        name_of(vpi_handle(vpiCondition, term)));
}

static void print_check(int index, vpiHandle check)
{
    s_vpi_time times[2];
    s_vpi_delay delays;
    PLI_INT32 type = vpi_get(vpiTchkType, check);
    vpiHandle iterator;
    int terms = 0;
    memset(times, 0, sizeof times);
    delays.da = times;
    delays.no_of_delays = type == vpiSetupHold ? 2 : 1;
    delays.time_type = vpiSimTime;
    delays.mtm_flag = 0;
    delays.append_flag = 0;
    delays.pulsere_flag = 0;
    vpi_get_delays(check, &delays);
    iterator = vpi_iterate(vpiExpr, check);
    while (iterator != NULL && vpi_scan(iterator) != NULL) {
        ++terms;
    }
    vpi_printf("#c tchk %d type=%s(%d) object=%s line=%d module=%s instance=%s "
               "terms=%d limits=%u,%u file=%s\n",
        index, check_name(type), (int)type, vpi_get_str(vpiType, check),
        (int)vpi_get(vpiLineNo, check), name_of(vpi_handle(vpiModule, check)),
        name_of(vpi_handle(vpiInstance, check)), terms, (unsigned)times[0].low,
        (unsigned)times[1].low,
        strstr(vpi_get_str(vpiFile, check), "foreign_vpi_timing.sv") != NULL
            ? "ok"
            : "bad");
    // The arguments of the check as expr objects (37.40 detail 2): the
    // events as terms, then the limits, threshold and notifier.
    iterator = vpi_iterate(vpiExpr, check);
    while (iterator != NULL) {
        vpiHandle argument = vpi_scan(iterator);
        PLI_INT32 argument_type;
        if (argument == NULL) {
            break;
        }
        argument_type = vpi_get(vpiType, argument);
        vpi_printf("#c   expr %s %s\n", vpi_get_str(vpiType, argument),
            argument_type == vpiConstant ? vpi_get_str(vpiDecompile, argument)
                : argument_type == vpiTchkTerm ? "-"
                                               : name_of(argument));
    }
    vpi_printf("#c   delay %s\n",
        vpi_handle(vpiDelay, check) == NULL
            ? "NULL"
            : vpi_get_str(vpiDecompile, vpi_handle(vpiDelay, check)));
    print_check_term("ref", vpi_handle(vpiTchkRefTerm, check));
    print_check_term("data", vpi_handle(vpiTchkDataTerm, check));
    vpi_printf("#c   notifier %s\n", name_of(vpi_handle(vpiTchkNotifier, check)));
}

static const char* value_of(vpiHandle handle)
{
    s_vpi_value value;
    if (handle == NULL) {
        return "NULL";
    }
    value.format = vpiBinStrVal;
    vpi_get_value(handle, &value);
    return value.value.str;
}

static PLI_INT32 violated(p_cb_data data)
{
    s_vpi_time time = { vpiSimTime, 0, 0, 0.0 };
    vpiHandle check = data->obj;
    vpiHandle notifier = vpi_handle(vpiTchkNotifier, check);
    vpiHandle reference = vpi_handle(vpiTchkRefTerm, check);
    vpiHandle data_term = vpi_handle(vpiTchkDataTerm, check);
    vpi_get_time(NULL, &time);
    vpi_printf("#c violation %s %u type=%s ref=%s data=%s notifier=%s=%s\n",
        data->user_data, (unsigned)time.low,
        check_name(vpi_get(vpiTchkType, check)),
        name_of(vpi_handle(vpiExpr, reference)),
        data_term == NULL ? "NULL" : name_of(vpi_handle(vpiExpr, data_term)),
        name_of(notifier), value_of(notifier));
    return 0;
}

static PLI_INT32 final_values(p_cb_data data)
{
    static const char* const names[] = { "top.u.ntf_setup", "top.u.ntf_hold",
        "top.u.ntf_sh", "top.u.ntf_width" };
    unsigned index;
    (void)data;
    for (index = 0; index < 4; ++index) {
        vpiHandle notifier = vpi_handle_by_name((PLI_BYTE8*)names[index], NULL);
        vpi_printf("#c final %s=%s\n", names[index], value_of(notifier));
    }
    return 0;
}

static void watch(vpiHandle object, const char* label)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbTchkViolation;
    data.cb_rtn = violated;
    data.obj = object;
    data.user_data = (PLI_BYTE8*)label;
    (void)vpi_register_cb(&data);
}

static PLI_INT32 start_of_simulation(p_cb_data data)
{
    static s_vpi_time time = { vpiSimTime, 0, 75, 0.0 };
    vpiHandle module = vpi_handle_by_name((PLI_BYTE8*)"top.u", NULL);
    vpiHandle iterator;
    vpiHandle item;
    vpiHandle width_check = NULL;
    vpiHandle removed;
    s_cb_data final;
    int index = 0;
    (void)data;
    if (module == NULL) {
        vpi_printf("#c no top.u\n");
        return 0;
    }
    iterator = vpi_iterate(vpiModPath, module);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        print_path(index++, item);
    }
    index = 0;
    iterator = vpi_iterate(vpiTchk, module);
    while (iterator != NULL && (item = vpi_scan(iterator)) != NULL) {
        print_check(index++, item);
        if (vpi_get(vpiTchkType, item) == vpiWidth) {
            width_check = item;
        }
    }
    // One callback for every check, one for the $width check only, and a
    // third removed again before any violation.
    watch(NULL, "all");
    watch(width_check, "width");
    memset(&final, 0, sizeof final);
    final.reason = cbTchkViolation;
    final.cb_rtn = violated;
    final.user_data = (PLI_BYTE8*)"removed";
    removed = vpi_register_cb(&final);
    if (removed != NULL) {
        (void)vpi_remove_cb(removed);
    }
    memset(&final, 0, sizeof final);
    final.reason = cbAfterDelay;
    final.cb_rtn = final_values;
    final.time = &time;
    (void)vpi_register_cb(&final);
    return 0;
}

static void startup(void)
{
    s_cb_data data;
    memset(&data, 0, sizeof data);
    data.reason = cbStartOfSimulation;
    data.cb_rtn = start_of_simulation;
    (void)vpi_register_cb(&data);
}

FSIM_VPI_EXPORT void (*vlog_startup_routines[])(void) = { startup, 0 };
