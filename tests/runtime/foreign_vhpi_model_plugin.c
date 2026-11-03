// SPDX-License-Identifier: Apache-2.0
// VHPI information-model conformance plug-in (IEEE 1076-2019 19, 22.2, 23.9).
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// A string property, lower-cased: the letter case of names is not specified.
static void show(const char* label, const vhpiCharT* text)
{
    char buffer[128];
    size_t index;
    if (text == NULL) {
        printf("#c %s=(null)\n", label);
        return;
    }
    for (index = 0U; text[index] != 0 && index + 1U < sizeof buffer; ++index) {
        buffer[index] = (char)tolower(text[index]);
    }
    buffer[index] = 0;
    printf("#c %s=%s\n", label, buffer);
}

// A string property lower-cased into one of two buffers, or "-".
static const char* text_property(vhpiHandleT object, vhpiStrPropertyT property)
{
    static char buffers[2][128];
    static int next;
    const vhpiCharT* value = vhpi_get_str(property, object);
    char* buffer = buffers[next];
    size_t index;
    next = 1 - next;
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

static const char* text(vhpiHandleT object, vhpiFormatT format)
{
    static char buffer[80];
    vhpiValueT value;
    memset(&value, 0, sizeof value);
    value.format = format;
    value.bufSize = sizeof buffer;
    value.value.str = (vhpiCharT*)buffer;
    if (vhpi_get_value(object, &value) != 0) {
        return "?";
    }
    return buffer;
}

// vhpi_format_value from `in` to a string format.
static const char* reformat(vhpiValueT* in, vhpiFormatT format)
{
    static char buffer[80];
    vhpiValueT out;
    memset(&out, 0, sizeof out);
    out.format = format;
    out.bufSize = sizeof buffer;
    out.value.str = (vhpiCharT*)buffer;
    if (vhpi_format_value(in, &out) != 0) {
        return "?";
    }
    return buffer;
}

static void start(const vhpiCbDataT* data)
{
    char hex[16] = "ff";
    vhpiHandleT root;
    vhpiHandleT unit;
    vhpiHandleT v;
    vhpiHandleT c;
    vhpiHandleT r;
    vhpiHandleT k;
    vhpiHandleT type;
    vhpiHandleT iterator;
    vhpiHandleT range;
    vhpiHandleT slice;
    vhpiHandleT alias;
    vhpiValueT value;
    unsigned char raw[64];
    (void)data;
    root = vhpi_handle(vhpiRootInst, NULL);
    v = vhpi_handle_by_name("top.v", NULL);
    c = vhpi_handle_by_name("top.c", NULL);
    r = vhpi_handle_by_name("top.r", NULL);
    k = vhpi_handle_by_name("top.k", NULL);

    // Strings and relations (19.4.5, 19.4.6, 19.4.2).
    show("root defname", vhpi_get_str(vhpiDefNameP, root));
    unit = vhpi_handle(vhpiDesignUnit, root);
    show("unit name", vhpi_get_str(vhpiUnitNameP, unit));
    show("unit lib", vhpi_get_str(vhpiLibLogicalNameP, unit));
    show("entity name",
        vhpi_get_str(vhpiNameP, vhpi_handle(vhpiEntityDecl, root)));
    printf("#c curregion is root=%d\n",
        vhpi_compare_handles(vhpi_handle(vhpiCurRegion, v), root));

    // Ranges (19.4.3): the range of each dimension has its own bounds.
    type = vhpi_handle(vhpiType, v);
    iterator = vhpi_iterator(vhpiConstraints, type);
    range = iterator == NULL ? NULL : vhpi_scan(iterator);
    if (range != NULL) {
        printf("#c v range left=%d right=%d up=%d intrange=%d\n",
            (int)vhpi_get(vhpiLeftBoundP, range),
            (int)vhpi_get(vhpiRightBoundP, range),
            (int)vhpi_get(vhpiIsUpP, range),
            vhpi_get(vhpiKindP, range) == vhpiIntRangeK);
    }
    printf("#c v numdims=%d\n", (int)vhpi_get(vhpiNumDimensionsP, v));
    printf("#c color literals=%d\n",
        (int)vhpi_get(vhpiNumLiteralsP, vhpi_handle(vhpiType, c)));
    printf("#c std_logic literals=%d\n",
        (int)vhpi_get(vhpiNumLiteralsP, vhpi_handle(vhpiElemType, v)));
    printf("#c v sigkind=%d forced=%d\n", (int)vhpi_get(vhpiSigKindP, v),
        (int)vhpi_get(vhpiIsForcedP, v));
    // Staticness belongs to expressions and ranges (9.4, 19.13): a signal
    // name is dynamic, a range locally static, a declaration has none.
    printf("#c static v(3)=%d range=%d decl undefined=%d\n",
        (int)vhpi_get(vhpiStaticnessP, vhpi_handle_by_name("top.v(3)", NULL)),
        range != NULL ? (int)vhpi_get(vhpiStaticnessP, range) : -1,
        vhpi_get(vhpiStaticnessP, k) == vhpiUndefined);

    // Value formats on a vector (22.2.8).
    printf("#c v oct=%s", text(v, vhpiOctStrVal));
    printf(" hex=%s", text(v, vhpiHexStrVal));
    printf(" dec=%s\n", text(v, vhpiDecStrVal));
    memset(&value, 0, sizeof value);
    value.format = vhpiRawDataVal;
    value.bufSize = sizeof raw;
    value.value.ptr = raw;
    printf("#c raw rc=%d", vhpi_get_value(v, &value));
    printf(" elems=%d\n", (int)value.numElems);

    // A slice name (19.4.3, 8.5).
    slice = vhpi_handle_by_name("top.v(5 downto 2)", NULL);
    if (slice != NULL) {
        printf("#c slice kind=%d bin=%s", vhpi_get(vhpiKindP, slice)
                == vhpiSliceNameK,
            text(slice, vhpiBinStrVal));
        printf(" hex=%s size=%d left=%d\n", text(slice, vhpiHexStrVal),
            (int)vhpi_get(vhpiSizeP, slice),
            (int)vhpi_get(vhpiLeftBoundP, slice));
    } else {
        printf("#c slice missing\n");
    }

    // An alias declaration reads as its target (19.4.3).
    iterator = vhpi_iterator(vhpiAliasDecls, root);
    alias = iterator == NULL ? NULL : vhpi_scan(iterator);
    if (alias != NULL) {
        show("alias name", vhpi_get_str(vhpiNameP, alias));
        printf("#c alias bin=%s", text(alias, vhpiBinStrVal));
        printf(" target is slice=%d\n",
            vhpi_compare_handles(vhpi_handle(vhpiAliasedName, alias),
                vhpi_handle_by_name("top.v(7 downto 4)", NULL)));
        printf("#c hi(3)=%s\n",
            text(vhpi_handle_by_name("top.hi(3)", NULL), vhpiBinStrVal));
    } else {
        printf("#c alias missing\n");
    }

    // vhpi_format_value (23.9).
    memset(&value, 0, sizeof value);
    value.format = vhpiIntVal;
    value.value.intg = 178;
    printf("#c format int->hex=%s", reformat(&value, vhpiHexStrVal));
    printf(" int->bin=%s", reformat(&value, vhpiBinStrVal));
    memset(&value, 0, sizeof value);
    value.format = vhpiBinStrVal;
    value.value.str = (vhpiCharT*)"1100";
    printf(" bin->hex=%s", reformat(&value, vhpiHexStrVal));
    memset(&value, 0, sizeof value);
    value.format = vhpiHexStrVal;
    value.value.str = (vhpiCharT*)hex;
    memset(&raw, 0, sizeof raw);
    {
        vhpiValueT out;
        memset(&out, 0, sizeof out);
        out.format = vhpiIntVal;
        printf(" hex->int=%d\n",
            vhpi_format_value(&value, &out) == 0 ? (int)out.value.intg : -1);
    }

    // Real properties (19.4.3, 23.17).
    type = vhpi_handle(vhpiType, r);
    printf("#c real left<0=%d right>0=%d", vhpi_get_real(vhpiFloatLeftBoundP,
                                                type) < 0.0,
        vhpi_get_real(vhpiFloatRightBoundP, type) > 0.0);
    printf(" val=%g\n", vhpi_get_real(vhpiRealValP, r));

    // Declaration classes and where objects are (19.4, Figure 14).
    iterator = vhpi_iterator(vhpiDecls, root);
    while (iterator != NULL && (k = vhpi_scan(iterator)) != NULL) {
        const vhpiCharT* file = vhpi_get_str(vhpiFileNameP, k);
        const char* base = file != NULL ? strrchr((const char*)file, '/') : NULL;
        printf("#c decl %s %s line=%d file=%s\n",
            text_property(k, vhpiKindStrP), text_property(k, vhpiNameP),
            (int)vhpi_get(vhpiLineNoP, k),
            base != NULL ? base + 1 : (file != NULL ? (const char*)file : "-"));
    }
    printf("#c root kind=%s line=%d unit line=%d\n",
        text_property(root, vhpiKindStrP), (int)vhpi_get(vhpiLineNoP, root),
        (int)vhpi_get(vhpiLineNoP, unit));
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
