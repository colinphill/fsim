// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the design unit classes of the library information model:
// vhpiDecls, vhpiGenericDecls, vhpiPortDecls, vhpiStmts and vhpiDepUnits of
// entity declarations, architecture bodies and packages, and the library STD
// with its package STANDARD (IEEE 1076-2019 19.4.5, 12.4, Figure 4). Lines
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

// The names of the objects of a relation, comma separated.
static void print_names(
    const char* label, vhpiOneToManyT relation, vhpiHandleT unit)
{
    vhpiHandleT iterator = vhpi_iterator(relation, unit);
    vhpiHandleT item;
    const char* separator = "";
    printf(" %s=", label);
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("%s%s", separator, text_property(item, vhpiNameP));
        separator = ",";
    }
}

// The number of objects of a relation.
static void print_count(
    const char* label, vhpiOneToManyT relation, vhpiHandleT unit)
{
    vhpiHandleT iterator = vhpi_iterator(relation, unit);
    int total = 0;
    while (iterator != NULL && vhpi_scan(iterator) != NULL) {
        ++total;
    }
    printf(" %s=%d", label, total);
}

// The full names of the units a unit depends on, comma separated.
static void print_dependencies(vhpiHandleT unit)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiDepUnits, unit);
    vhpiHandleT item;
    const char* separator = "";
    printf(" deps=");
    while (iterator != NULL && (item = vhpi_scan(iterator)) != NULL) {
        printf("%s%s", separator, text_property(item, vhpiFullNameP));
        separator = ",";
    }
}

// The unit of a kind and name in a library, or NULL.
static vhpiHandleT find_unit(vhpiHandleT library, int kind, const char* name)
{
    vhpiHandleT units = vhpi_iterator(vhpiDesignUnits, library);
    vhpiHandleT item;
    while (units != NULL && (item = vhpi_scan(units)) != NULL) {
        if ((int)vhpi_get(vhpiKindP, item) == kind
            && strcmp(text_property(item, vhpiNameP), name) == 0) {
            return item;
        }
    }
    return NULL;
}

// The libraries, comma separated.
static vhpiHandleT find_library(const char* name)
{
    vhpiHandleT libraries = vhpi_iterator(vhpiLibraryDecls, NULL);
    vhpiHandleT item;
    const char* separator = "";
    vhpiHandleT result = NULL;
    printf("#c libraries ");
    while (libraries != NULL && (item = vhpi_scan(libraries)) != NULL) {
        printf("%s%s", separator, text_property(item, vhpiLibLogicalNameP));
        separator = ",";
        if (strcmp(text_property(item, vhpiLibLogicalNameP), name) == 0) {
            result = item;
        }
    }
    printf("\n");
    return result;
}

static void print_unit(
    vhpiHandleT library, int kind, const char* label, const char* name)
{
    vhpiHandleT unit = find_unit(library, kind, name);
    printf("#c %s %s", label, name);
    if (unit == NULL) {
        printf(" missing\n");
        return;
    }
    if (kind == vhpiEntityDeclK) {
        print_names("generics", vhpiGenericDecls, unit);
        print_names("ports", vhpiPortDecls, unit);
    }
    if (strcmp(name, "standard") == 0) {
        // STANDARD's declarations are the catalog test's business
        // (vhpi-standard-catalog); the default standard is VHDL-2008.
        print_count("decls", vhpiDecls, unit);
    } else {
        print_names("decls", vhpiDecls, unit);
    }
    if (kind == vhpiEntityDeclK || kind == vhpiArchBodyK) {
        print_names("stmts", vhpiStmts, unit);
    }
    print_dependencies(unit);
    printf("\n");
}

// The port associations of a component instantiation, `formal=actual`
// comma separated.
static void print_associations(vhpiHandleT instance)
{
    vhpiHandleT associations = vhpi_iterator(vhpiPortAssocs, instance);
    vhpiHandleT association;
    const char* separator = "";
    printf(" ports=");
    while (associations != NULL
        && (association = vhpi_scan(associations)) != NULL) {
        printf("%s%s=%s", separator,
            text_property(vhpi_handle(vhpiFormal, association), vhpiNameP),
            text_property(vhpi_handle(vhpiActual, association), vhpiNameP));
        separator = ",";
    }
}

// The statements of a statement region that are component instantiations
// or blocks, with their port associations and nested relations (vhpiStmts,
// vhpiCompInstStmts, vhpiBlockStmts, vhpiInternalRegions, vhpiPortAssocs).
static void print_regions(vhpiHandleT unit)
{
    vhpiHandleT statements = vhpi_iterator(vhpiStmts, unit);
    vhpiHandleT statement;
    while (statements != NULL && (statement = vhpi_scan(statements)) != NULL) {
        int kind = (int)vhpi_get(vhpiKindP, statement);
        if (kind == vhpiCompInstStmtK) {
            printf("#c instance %s", text_property(statement, vhpiNameP));
            print_associations(statement);
            printf("\n");
        } else if (kind == vhpiBlockStmtK) {
            printf("#c block %s", text_property(statement, vhpiNameP));
            print_names("instances", vhpiCompInstStmts, statement);
            print_names("blocks", vhpiBlockStmts, statement);
            print_names("internal", vhpiInternalRegions, statement);
            printf("\n");
            print_regions(statement);
        }
    }
}

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT work = find_library("work");
    vhpiHandleT std = find_library("std");
    (void)data;
    print_unit(work, vhpiEntityDeclK, "entity", "leaf");
    print_unit(work, vhpiArchBodyK, "arch", "l");
    print_unit(work, vhpiEntityDeclK, "entity", "top");
    print_unit(work, vhpiArchBodyK, "arch", "t");
    print_regions(find_unit(work, vhpiArchBodyK, "t"));
    print_unit(work, vhpiPackDeclK, "package", "pk");
    print_unit(std, vhpiPackDeclK, "std", "standard");
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
