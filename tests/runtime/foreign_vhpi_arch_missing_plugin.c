// SPDX-License-Identifier: Apache-2.0
// A VHPI library that registers a foreign architecture other than the one
// the design names (IEEE 1076-2019 20.2.4.2): the run fails when
// registration completes. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"

#include <stdio.h>
#include <string.h>

static char library_name[] = "archlib";
static char model_name[] = "other";

static void elab(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c elab ran\n");
}

static void exec(const vhpiCbDataT* data)
{
    (void)data;
    printf("#c exec ran\n");
}

static void startup(void)
{
    vhpiForeignDataT model;
    memset(&model, 0, sizeof model);
    model.kind = vhpiArchF;
    model.libraryName = library_name;
    model.modelName = model_name;
    model.elabf = elab;
    model.execf = exec;
    printf("#c registered %s %s %d\n", library_name, model_name,
        vhpi_register_foreignf(&model) != NULL);
    fflush(stdout);
}

FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };
