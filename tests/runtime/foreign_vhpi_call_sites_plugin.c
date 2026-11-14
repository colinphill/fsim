// SPDX-License-Identifier: Apache-2.0
// The variables of the frames of two call sites of a recursive procedure
// and of its recursive call (IEEE 1076-2019 14.6 b): at the first statement of
// each activation every call object that has a frame lists the formal and
// the variable of it. Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
static vhpiHandleT calls[4];
static int probes;
static int val(vhpiHandleT o){ vhpiValueT x; memset(&x,0,sizeof x); x.format=vhpiIntVal; return vhpi_get_value(o,&x)?-999:(int)x.value.intg; }
static void dump(int c){ vhpiHandleT f=vhpi_handle(vhpiCurStackFrame,calls[c]); if(!f) return; printf("#c   call%d:",c); vhpiHandleT it=vhpi_iterator(vhpiParamDecls,calls[c]); vhpiHandleT d; while(it&&(d=vhpi_scan(it))) printf(" %s=%d",(const char*)vhpi_get_str(vhpiNameP,d),val(d)); it=vhpi_iterator(vhpiVarDecls,calls[c]); while(it&&(d=vhpi_scan(it))) printf(" %s=%d",(const char*)vhpi_get_str(vhpiNameP,d),val(d)); printf("\n"); }
// Deposits into the variable `depth` of the frame of call object `c`.
static void deposit(int c, int number)
{
    vhpiHandleT iterator = vhpi_iterator(vhpiVarDecls, calls[c]);
    vhpiHandleT declaration;
    while (iterator && (declaration = vhpi_scan(iterator))) {
        vhpiValueT value;
        memset(&value, 0, sizeof value);
        value.format = vhpiIntVal;
        value.value.intg = number;
        printf("#c   deposit %d into call%d %s\n", number, c,
            vhpi_put_value(declaration, &value, vhpiDeposit) == 0 ? "accepted" : "refused");
    }
}
static void probe(const vhpiCbDataT* d)
{
    (void)d;
    printf("#c probe %d\n", ++probes);
    for (int c = 0; c < 4; c++) {
        dump(c);
    }
    // The variable of the outermost activation, while the third runs: the
    // recursion unwinds into the deposited value (IEEE 1076-2019 22.5.2).
    if (probes == 3) {
        deposit(0, 500);
    }
    // The same, three activations deep in a recursion that shares its
    // frame with the call: the call's own activation is the outermost.
    if (probes == 8) {
        deposit(3, 700);
    }
    fflush(stdout);
}
static void show(const vhpiCbDataT* d)
{
    vhpiTimeT now;
    (void)d;
    vhpi_get_time(&now, NULL);
    printf("#c total=%d at %u fs\n", val(vhpi_handle_by_name("top.main.total", 0)), (unsigned)now.low);
    fflush(stdout);
}

static void ended(const vhpiCbDataT* d){ (void)d; }
static vhpiHandleT nth_of(vhpiHandleT owner, int32_t kind, int skip){ vhpiHandleT it=vhpi_iterator(vhpiSeqStmts,owner),s; while(it&&(s=vhpi_scan(it))){ if(vhpi_get(vhpiKindP,s)==kind&&skip--==0) return s;} return NULL; }
static void start(const vhpiCbDataT* d){ (void)d;
  vhpiHandleT body=vhpi_handle(vhpiSubpBody,vhpi_handle_by_name("top.main.down",0));
  vhpiHandleT br=vhpi_iterator(vhpiBranchs,nth_of(body,vhpiIfStmtK,0)); vhpiHandleT th=vhpi_scan(br);
  vhpiHandleT m=vhpi_handle_by_name("top.main",0);
  calls[0]=nth_of(m,vhpiSeqProcCallStmtK,0); calls[1]=nth_of(th,vhpiSeqProcCallStmtK,0); calls[2]=nth_of(m,vhpiSeqProcCallStmtK,1); calls[3]=nth_of(m,vhpiSeqProcCallStmtK,2);
  vhpiCbDataT r; memset(&r,0,sizeof r); r.reason=vhpiCbStmt; r.cb_rtn=probe; r.obj=nth_of(body,vhpiVarAssignStmtK,0); if(!vhpi_register_cb(&r,vhpiReturnCb)) printf("#c refused\n");
  for(int i=0;i<4;i++){ memset(&r,0,sizeof r); r.reason=vhpiCbEndOfSubpCall; r.cb_rtn=ended; r.obj=calls[i]; if(!vhpi_register_cb(&r,vhpiReturnCb)) printf("#c refused call %d\n",i);} }
static void startup(void){ vhpiCbDataT d; { vhpiCbDataT r; vhpiTimeT t; unsigned i; for (i = 0; i < 3U; i++) { memset(&r,0,sizeof r); t.high=0; t.low=i*1000000u+500000u; r.reason=vhpiCbAfterDelay; r.cb_rtn=show; r.time=&t; vhpi_register_cb(&r,0);} } memset(&d,0,sizeof d); d.reason=vhpiCbStartOfInitialization; d.cb_rtn=start; vhpi_register_cb(&d,0);} 
FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };
