// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for package body design units, vhpiPrimaryUnit of a package
// body, vhpiDepUnits and vhpiUses (IEEE 1076-2019 19.4.5, Figure 4). Lines
// starting with "#c " form the checked trace.
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

// The dependent units of a design unit in library work, comma separated.
static void dependencies(vhpiHandleT unit)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiDepUnits, unit);
    vhpiHandleT item;
    const char* separator = "";
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        const char* name = text_property(item, vhpiFullNameP);
        if (strncmp(name, "work.", 5U) == 0) {
            printf("%s%s", separator, name);
            separator = ",";
        }
    }
}

// The names of the specifications of a design unit, comma separated.
static void print_specs(const char* label, vhpiHandleT unit)
{
    vhpiHandleT specs = vhpi_iterator(vhpiSpecs, unit);
    vhpiHandleT spec;
    const char* separator = "";
    printf("#c specs %s %s %s:", label, text_property(unit, vhpiNameP),
        text_property(unit, vhpiKindStrP));
    while (specs != NULL && (spec = vhpi_scan(specs)) != NULL) {
        printf("%s%s", separator, text_property(spec, vhpiNameP));
        separator = ",";
    }
    printf("\n");
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
    vhpiHandleT unit = vhpi_handle(vhpiDesignUnit, root);
    vhpiHandleT library = vhpi_handle(vhpiLibraryDecl, unit);
    vhpiHandleT units;
    vhpiHandleT item;
    (void)data;
    units = vhpi_iterator(vhpiDesignUnits, library);
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        vhpiHandleT primary;
        if (vhpi_get(vhpiKindP, item) != vhpiPackBodyK) {
            continue;
        }
        primary = vhpi_handle(vhpiPrimaryUnit, item);
        printf("#c body %s primary=%s kind=%s deps=", text_property(item, vhpiNameP),
            text_property(primary, vhpiFullNameP),
            text_property(primary, vhpiKindStrP));
        dependencies(item);
        printf("\n");
    }
    units = vhpi_iterator(vhpiDesignUnits, library);
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        vhpiHandleT uses;
        vhpiHandleT used;
        const char* separator = "";
        if (vhpi_get(vhpiKindP, item) != vhpiArchBodyK) {
            continue;
        }
        printf("#c arch %s deps=", text_property(item, vhpiNameP));
        dependencies(item);
        printf(" uses=");
        uses = vhpi_iterator(vhpiUses, item);
        while (uses != NULL && (used = vhpi_scan(uses)) != NULL) {
            printf("%s%s", separator, text_property(used, vhpiNameP));
            separator = ",";
        }
        printf("\n");
    }
    // The attribute specifications of a design unit belong to the unit whose
    // declarative part holds them (7.2, Figure 33): the entity's to the
    // entity declaration, the architecture's to the architecture body, both
    // for the units of the library and for the root instance's unit.
    units = vhpi_iterator(vhpiDesignUnits, library);
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        int kind = (int)vhpi_get(vhpiKindP, item);
        if (kind == vhpiEntityDeclK || kind == vhpiArchBodyK) {
            print_specs("library", item);
        }
    }
    print_specs("root", unit);
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
