// SPDX-License-Identifier: Apache-2.0
// The frame of a call of a procedure whose body is declared apart from its
// specification (IEEE 1076-2019 4.8, 14.6 b): the formal and the variables of
// the activation, read at the first statement of the body. Lines starting
// with "#c " form the checked trace.
#include "fsim/runtime/vhpi_abi.h"
#include "vhpi_user.h"
#include <stdio.h>
#include <string.h>
static vhpiHandleT call_;
static int val(vhpiHandleT o){ vhpiValueT x; memset(&x,0,sizeof x); x.format=vhpiIntVal; return vhpi_get_value(o,&x)?-999:(int)x.value.intg; }
static void probe(const vhpiCbDataT* d){ (void)d; vhpiHandleT f=vhpi_handle(vhpiCurStackFrame,call_); printf("#c probe frame=%d\n", f!=0); if(!f) return; vhpiHandleT it,dd; int n=0; it=vhpi_iterator(vhpiParamDecls,call_); while(it&&(dd=vhpi_scan(it))){ printf("#c   param %s=%d\n",(const char*)vhpi_get_str(vhpiNameP,dd),val(dd)); n++;} it=vhpi_iterator(vhpiVarDecls,call_); while(it&&(dd=vhpi_scan(it))){ printf("#c   var %s=%d\n",(const char*)vhpi_get_str(vhpiNameP,dd),val(dd)); n++;} printf("#c   decls=%d\n",n); fflush(stdout);} 
static void ended(const vhpiCbDataT* d){ (void)d; }
static vhpiHandleT nth_of(vhpiHandleT owner, int32_t kind, int skip){ vhpiHandleT it=vhpi_iterator(vhpiSeqStmts,owner),s; while(it&&(s=vhpi_scan(it))){ if(vhpi_get(vhpiKindP,s)==kind&&skip--==0) return s;} return NULL; }
static void start(const vhpiCbDataT* d){ (void)d;
  vhpiHandleT m=vhpi_handle_by_name("top.main",0); call_=nth_of(m,vhpiSeqProcCallStmtK,0);
  vhpiHandleT decl=vhpi_handle_by_name("pk.down",0); printf("#c decl=%d\n", decl!=0);
  vhpiHandleT body = decl? vhpi_handle(vhpiSubpBody,decl):0; printf("#c body=%d\n", body!=0);
  vhpiCbDataT r; memset(&r,0,sizeof r); r.reason=vhpiCbStmt; r.cb_rtn=probe; r.obj= body? nth_of(body,vhpiVarAssignStmtK,0):0; if(!r.obj || !vhpi_register_cb(&r,vhpiReturnCb)) printf("#c refused probe\n");
  memset(&r,0,sizeof r); r.reason=vhpiCbEndOfSubpCall; r.cb_rtn=ended; r.obj=call_; if(!vhpi_register_cb(&r,vhpiReturnCb)) printf("#c refused call\n"); }
static void startup(void){ vhpiCbDataT d; memset(&d,0,sizeof d); d.reason=vhpiCbStartOfInitialization; d.cb_rtn=start; vhpi_register_cb(&d,0);} 
FSIM_VHPI_EXPORT void (*vhpi_startup_routines[])(void) = { startup, 0 };
