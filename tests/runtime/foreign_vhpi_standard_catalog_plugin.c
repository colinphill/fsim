// SPDX-License-Identifier: Apache-2.0
// VHPI plug-in for the declarations of package STD.STANDARD (IEEE 1076-2019
// 16.3, 12.4, 19.4.5): the number of its types, range records, functions,
// impure functions and attributes, a checksum over the signatures of its
// functions in declaration order, and the declarations the implicit
// `use STD.STANDARD.all` makes visible to a design unit. Lines starting with
// "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// A string property lower-cased into one of eight buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[8][160];
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

static uint32_t hash_text(uint32_t hash, const char* text)
{
    while (*text != 0) {
        hash = (hash ^ (uint32_t)(unsigned char)*text++) * 16777619U;
    }
    return hash;
}

// The signature `name(type;type)>result` of a function, hashed (FNV-1a).
static uint32_t hash_function(uint32_t hash, vhpiHandleT function)
{
    vhpiHandleT parameters = vhpi_iterator(vhpiParamDecls, function);
    vhpiHandleT parameter;
    const char* separator = "";
    hash = hash_text(hash, text_property(function, vhpiNameP));
    hash = hash_text(hash, "(");
    while (parameters != NULL && (parameter = vhpi_scan(parameters)) != NULL) {
        hash = hash_text(hash, separator);
        hash = hash_text(
            hash, text_property(vhpi_handle(vhpiType, parameter), vhpiNameP));
        separator = ";";
    }
    hash = hash_text(hash, ")>");
    hash = hash_text(hash,
        text_property(vhpi_handle(vhpiReturnType, function), vhpiNameP));
    return hash_text(hash, "\n");
}

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

static void start(const vhpiCbDataT* data)
{
    vhpiHandleT libraries = vhpi_iterator(vhpiLibraryDecls, NULL);
    vhpiHandleT library;
    vhpiHandleT standard = NULL;
    vhpiHandleT decls;
    vhpiHandleT item;
    vhpiHandleT uses;
    vhpiHandleT arch;
    uint32_t hash = 2166136261U;
    int types = 0;
    int records = 0;
    int functions = 0;
    int impure = 0;
    int attributes = 0;
    int visible = 0;
    (void)data;
    while (libraries != NULL && (library = vhpi_scan(libraries)) != NULL) {
        if (strcmp(text_property(library, vhpiLibLogicalNameP), "std") == 0) {
            standard = find_unit(library, vhpiPackDeclK, "standard");
        }
    }
    decls = vhpi_iterator(vhpiDecls, standard);
    while (decls != NULL && (item = vhpi_scan(decls)) != NULL) {
        int kind = (int)vhpi_get(vhpiKindP, item);
        if (kind == vhpiFuncDeclK) {
            if (vhpi_get(vhpiIsPureP, item) == 0) {
                ++impure;
            }
            ++functions;
            hash = hash_function(hash, item);
        } else if (kind == vhpiAttrDeclK) {
            ++attributes;
        } else if (kind == vhpiRecordTypeDeclK) {
            ++records;
        } else {
            ++types;
        }
    }
    printf("#c standard types=%d records=%d functions=%d impure=%d "
           "attributes=%d checksum=%08x\n",
        types, records, functions, impure, attributes, (unsigned)hash);
    // The architecture body sees all of them through the implicit use clause.
    arch = vhpi_handle(vhpiDesignUnit, vhpi_handle(vhpiRootInst, NULL));
    uses = vhpi_iterator(vhpiUses, arch);
    while (uses != NULL && (item = vhpi_scan(uses)) != NULL) {
        if (strncmp(text_property(item, vhpiFullNameP), "@std:standard", 13U)
            == 0) {
            ++visible;
        }
    }
    printf("#c uses standard=%d\n", visible);
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
