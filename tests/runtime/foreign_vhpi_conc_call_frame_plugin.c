// SPDX-License-Identifier: Apache-2.0
// The frame of a concurrent procedure call as a region (IEEE 1076-2019 11.4,
// 14.6 b): at the first statement of each activation the variable of the frame
// is listed, and a deposit at 2 ns is seen by the statements that follow.
// Lines starting with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdio.h>
#include <string.h>
static vhpiHandleT call_;
static int val(vhpiHandleT o){ vhpiValueT x; memset(&x,0,sizeof x); x.format=vhpiIntVal; return vhpi_get_value(o,&x)?-999:(int)x.value.intg; }
static void probe(const vhpiCbDataT* d){ vhpiTimeT t; (void)d; vhpi_get_time(&t,0); vhpiHandleT f=vhpi_handle(vhpiCurStackFrame,call_); printf("#c probe at %u frame=%d\n",(unsigned)t.low, f!=0); if(!f) return; vhpiHandleT it,dd; it=vhpi_iterator(vhpiVarDecls,call_); while(it&&(dd=vhpi_scan(it))){ printf("#c   var %s=%d\n",(const char*)vhpi_get_str(vhpiNameP,dd),val(dd)); if(t.low==2000000){ vhpiValueT x; memset(&x,0,sizeof x); x.format=vhpiIntVal; x.value.intg=10; if(vhpi_put_value(dd,&x,vhpiDeposit)) printf("#c   deposit refused\n"); } } fflush(stdout);} 
static void show(const vhpiCbDataT* d){ (void)d; printf("#c o=%d\n", val(vhpi_handle_by_name("top.o",0))); fflush(stdout);} 
static void ended(const vhpiCbDataT* d){ (void)d; }
static void start(const vhpiCbDataT* d){ (void)d;
  vhpiHandleT root=vhpi_handle(vhpiRootInst,0); vhpiHandleT it=vhpi_iterator(vhpiStmts,root),s; while(it&&(s=vhpi_scan(it))) if(vhpi_get(vhpiKindP,s)==vhpiConcProcCallStmtK) call_=s;
  printf("#c call=%d\n", call_!=0); if(!call_) return;
  vhpiHandleT decl=vhpi_handle_by_name("top.scale",0); vhpiHandleT body= decl?vhpi_handle(vhpiSubpBody,decl):0; printf("#c body=%d\n",body!=0);
  vhpiHandleT st=0; { vhpiHandleT i2=vhpi_iterator(vhpiSeqStmts,body),q; while(i2&&(q=vhpi_scan(i2))){ if(vhpi_get(vhpiKindP,q)==vhpiVarAssignStmtK){st=q;break;} } }
  vhpiCbDataT r; memset(&r,0,sizeof r); r.reason=vhpiCbStmt; r.cb_rtn=probe; r.obj=st; if(!st||!vhpi_register_cb(&r,vhpiReturnCb)) printf("#c refused probe\n");
  memset(&r,0,sizeof r); r.reason=vhpiCbEndOfSubpCall; r.cb_rtn=ended; r.obj=call_; if(!vhpi_register_cb(&r,vhpiReturnCb)) printf("#c refused call\n"); }
static void startup0(void){ vhpiCbDataT d; memset(&d,0,sizeof d); d.reason=vhpiCbStartOfInitialization; d.cb_rtn=start; vhpi_register_cb(&d,0);} 
static void startup(void){ startup0(); vhpiCbDataT r; vhpiTimeT t; memset(&r,0,sizeof r); t.high=0; t.low=3000000u; r.reason=vhpiCbAfterDelay; r.cb_rtn=show; r.time=&t; vhpi_register_cb(&r,0);} 
FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };
