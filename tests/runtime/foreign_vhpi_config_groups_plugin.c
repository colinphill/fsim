// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for range bounds, groups, attribute specifications and
// configuration declarations (IEEE 1076-2019 19.4, 19.5). Lines starting
// with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of eight buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[8][128];
    static int next;
    const vhpiCharT* value
        = object != NULL ? vhpi_get_str(property, object) : NULL;
    char* buffer = buffers[next];
    size_t index;
    next = (next + 1) % 8;
    if (value == NULL) {
        return "-";
    }
    for (index = 0U; value[index] != 0 && index + 1U < sizeof buffers[0];
         ++index) {
        buffer[index] = (char)tolower(value[index]);
    }
    buffer[index] = 0;
    return buffer;
}

static const char* class_name(vhpiIntT value)
{
    return value == vhpiSignalEC ? "signal" : "other";
}

// A subtype's first constraint and the kind and name of its bounds.
static void range(const char* name)
{
    vhpiHandleT subtype = vhpi_handle_by_name(name, NULL);
    vhpiHandleT constraint = vhpi_handle(vhpiConstraint, subtype);
    vhpiHandleT left = vhpi_handle(vhpiLeftExpr, constraint);
    vhpiHandleT right = vhpi_handle(vhpiRightExpr, constraint);
    printf("#c %s kind=%s left=%s", name + 4,
        text_property(constraint, vhpiKindStrP), text_property(left, vhpiNameP));
    if (strcmp(name, "top.warm") == 0) {
        printf(" kind=%s", text_property(left, vhpiKindStrP));
    }
    printf(" right=%s\n", text_property(right, vhpiNameP));
}

// The entity designators of an attribute specification or group: the
// objects their names denote (class entityDesignator is the superclass of
// decl, stmt and the others, Figure 30).
static void designators(vhpiHandleT owner)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiEntityDesignators, owner);
    vhpiHandleT item;
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("#c   designator %s %s\n", text_property(item, vhpiKindStrP),
            text_property(item, vhpiNameP));
    }
}

// The unit declarations of a physical type, its base unit and the literal
// that defines a secondary unit (Figure 20).
static void units(void)
{
    vhpiHandleT type = vhpi_handle_by_name("top.length", NULL);
    vhpiHandleT iterator = vhpi_iterator(vhpiUnitDecls, type);
    vhpiHandleT unit;
    printf("#c base unit %s\n",
        text_property(vhpi_handle(vhpiBaseUnit, type), vhpiNameP));
    while (iterator != NULL && (unit = vhpi_scan(iterator)) != NULL) {
        vhpiHandleT literal = vhpi_handle(vhpiPhysLiteral, unit);
        printf("#c unit %s literal=%s back=%s\n", text_property(unit, vhpiNameP),
            text_property(literal, vhpiNameP),
            literal != NULL ? text_property(vhpi_handle(vhpiUnitDecl, literal),
                                  vhpiNameP)
                            : "-");
    }
}

// The attribute specification of a signal and its attribute declaration.
static void attributes(void)
{
    vhpiHandleT signal = vhpi_handle_by_name("top.a1", NULL);
    vhpiHandleT iterator = vhpi_iterator(vhpiAttrSpecs, signal);
    vhpiHandleT specification = iterator != NULL ? vhpi_scan(iterator) : NULL;
    printf("#c a1 attrspec=%s attrdecl=%s\n",
        text_property(specification, vhpiKindStrP),
        text_property(vhpi_handle(vhpiAttrDecl, specification), vhpiNameP));
}

static void declarations(void)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT iterator = vhpi_iterator(vhpiDecls, root);
    vhpiHandleT item;
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        vhpiClassKindT kind = (vhpiClassKindT)vhpi_get(vhpiKindP, item);
        if (kind == vhpiGroupTempDeclK) {
            vhpiHandleT entries
                = vhpi_iterator(vhpiEntityClassEntries, item);
            vhpiHandleT entry;
            int total = 0;
            while (entries != NULL && vhpi_scan(entries) != NULL) {
                ++total;
            }
            printf("#c group template %s entries=%d\n",
                text_property(item, vhpiNameP), total);
            entries = vhpi_iterator(vhpiEntityClassEntries, item);
            while (entries != NULL && (entry = vhpi_scan(entries)) != NULL) {
                printf("#c   entry class=%s unconstrained=%d\n",
                    class_name(vhpi_get(vhpiEntityClassP, entry)),
                    (int)vhpi_get(vhpiIsUnconstrainedP, entry));
            }
        } else if (kind == vhpiGroupDeclK) {
            vhpiHandleT group_template = vhpi_handle(vhpiGroupTempDecl, item);
            vhpiHandleT list = vhpi_iterator(vhpiEntityDesignators, item);
            int total = 0;
            while (list != NULL && vhpi_scan(list) != NULL) {
                ++total;
            }
            printf("#c group %s template=%s designators=%d\n",
                text_property(item, vhpiNameP),
                text_property(group_template, vhpiNameP), total);
            designators(item);
        } else if (kind == vhpiAttrSpecK) {
            vhpiHandleT list = vhpi_iterator(vhpiEntityDesignators, item);
            int total = 0;
            while (list != NULL && vhpi_scan(list) != NULL) {
                ++total;
            }
            printf("#c attrspec %s designators=%d class=%s\n",
                text_property(item, vhpiNameP), total,
                class_name(vhpi_get(vhpiEntityClassP, item)));
            designators(item);
        }
    }
}

// An indexed name whose index is a locally static expression has a value
// (IEEE 1076-2019 22.3). It is the value expression of the signal
// assignment's waveform element (Figure 51).
static void dynamic_name(void)
{
    vhpiHandleT process = vhpi_handle_by_name("top.p", NULL);
    vhpiHandleT statements = vhpi_iterator(vhpiSeqStmts, process);
    vhpiHandleT assign = statements != NULL ? vhpi_scan(statements) : NULL;
    vhpiHandleT elements = vhpi_iterator(vhpiWaveformElems, assign);
    vhpiHandleT element = elements != NULL ? vhpi_scan(elements) : NULL;
    vhpiHandleT name = vhpi_handle(vhpiValExpr, element);
    vhpiValueT value;
    int status;
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    status = vhpi_get_value(name, &value);
    printf("#c dynamic kind=%s status=%d value=%d\n",
        text_property(name, vhpiKindStrP), status, (int)value.value.intg);
}

static void configurations(void)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT unit = vhpi_handle(vhpiDesignUnit, root);
    vhpiHandleT library = vhpi_handle(vhpiLibraryDecl, unit);
    vhpiHandleT units = vhpi_iterator(vhpiDesignUnits, library);
    vhpiHandleT item;
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        vhpiHandleT block;
        vhpiHandleT items;
        vhpiHandleT sub;
        int total = 0;
        if (vhpi_get(vhpiKindP, item) != vhpiConfigDeclK) {
            continue;
        }
        printf("#c config %s entity=%s\n", text_property(item, vhpiNameP),
            text_property(vhpi_handle(vhpiEntityDecl, item), vhpiFullNameP));
        block = vhpi_handle(vhpiBlockConfig, item);
        items = vhpi_iterator(vhpiConfigItems, block);
        while (items != NULL && vhpi_scan(items) != NULL) {
            ++total;
        }
        printf("#c   block spec=%s items=%d\n",
            text_property(block, vhpiSpecNameP), total);
        items = vhpi_iterator(vhpiConfigItems, block);
        while (items != NULL && (sub = vhpi_scan(items)) != NULL) {
            printf("#c   item kind=%s compname=%s instnames=%s open=%d "
                   "default=%d aspect=%s\n",
                text_property(sub, vhpiKindStrP),
                text_property(sub, vhpiCompNameP),
                text_property(sub, (vhpiStrPropertyT)vhpiInstNamesP),
                (int)vhpi_get(vhpiIsOpenP, sub),
                (int)vhpi_get(vhpiIsDefaultP, sub),
                text_property(vhpi_handle(vhpiEntityAspect, sub), vhpiFullNameP));
        }
    }
}

static void start(const vhpiCbDataT* data)
{
    (void)data;
    range("top.warm");
    range("top.short");
    range("top.frac");
    declarations();
    units();
    attributes();
    dynamic_name();
    configurations();
    fflush(stdout);
}

static void startup(void)
{
    vhpiCbDataT data;
    memset(&data, 0, sizeof data);
    data.reason = vhpiCbStartOfSimulation;
    data.cb_rtn = start;
    (void)vhpi_register_cb(&data, 0);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };
