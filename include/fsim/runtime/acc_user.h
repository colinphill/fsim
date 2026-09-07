// SPDX-License-Identifier: Apache-2.0

#ifndef ACC_USER_H
#define ACC_USER_H

/* Public IEEE ACC declarations for direct v3 plug-ins. */

#include "fsim/runtime/veriuser.h"

#if defined(_WIN32)
#ifndef PLI_DLLISPEC
#if defined(FSIM_ACC_LINK_SURFACE_BUILD)
#define PLI_DLLISPEC __declspec(dllexport)
#else
#define PLI_DLLISPEC __declspec(dllimport)
#endif
#define ACC_USER_DEFINED_DLLISPEC 1
#endif
#else
#ifndef PLI_DLLISPEC
#define PLI_DLLISPEC __attribute__((visibility("default")))
#define ACC_USER_DEFINED_DLLISPEC 1
#endif
#endif

#ifndef PLI_EXTERN
#define PLI_EXTERN
#define ACC_USER_DEFINED_EXTERN 1
#endif
#ifndef PLI_VEXTERN
#define PLI_VEXTERN extern
#define ACC_USER_DEFINED_VEXTERN 1
#endif

#define ACC_PROTO(params) params
#define ACC_EXTERN PLI_EXTERN PLI_DLLISPEC

typedef PLI_INT32* HANDLE;
#ifndef VPI_USER_CDS_H
typedef PLI_INT32* handle;
#endif

/* Object and subtype identities. */
#define accModule 20
#define accScope 21
#define accNet 25
#define accReg 30
#define accRegister accReg
#define accPort 35
#define accTerminal 45
#define accInputTerminal 46
#define accOutputTerminal 47
#define accInoutTerminal 48
#define accCombPrim 140
#define accSeqPrim 142
#define accAndGate 144
#define accNandGate 146
#define accNorGate 148
#define accOrGate 150
#define accXorGate 152
#define accXnorGate 154
#define accBufGate 156
#define accNotGate 158
#define accBufif0Gate 160
#define accBufif1Gate 162
#define accNotif0Gate 164
#define accNotif1Gate 166
#define accNmosGate 168
#define accPmosGate 170
#define accCmosGate 172
#define accRnmosGate 174
#define accRpmosGate 176
#define accRcmosGate 178
#define accRtranGate 180
#define accRtranif0Gate 182
#define accRtranif1Gate 184
#define accTranGate 186
#define accTranif0Gate 188
#define accTranif1Gate 190
#define accPullupGate 192
#define accPulldownGate 194
#define accIntegerParam 200
#define accIntParam accIntegerParam
#define accRealParam 202
#define accStringParam 204
#define accTchk 208
#define accPrimitive 210
#define accBit 212
#define accPortBit 214
#define accNetBit 216
#define accRegBit 218
#define accParameter 220
#define accSpecparam 222
#define accTopModule 224
#define accModuleInstance 226
#define accCellInstance 228
#define accModPath 230
#define accInterModPath 236
#define accScalarPort 250
#define accBitSelectPort 252
#define accPartSelectPort 254
#define accVectorPort 256
#define accConcatPort 258
#define accWire 260
#define accWand 261
#define accWor 262
#define accTri 263
#define accTriand 264
#define accTrior 265
#define accTri0 266
#define accTri1 267
#define accTrireg 268
#define accSupply0 269
#define accSupply1 270
#define accNamedEvent 280
#define accEventVar accNamedEvent
#define accIntegerVar 281
#define accIntVar accIntegerVar
#define accRealVar 282
#define accTimeVar 283
#define accScalar 300
#define accVector 302
#define accExpandedVector 306
#define accUnExpandedVector 307
#define accProtected 308
#define accSetup 366
#define accHold 367
#define accWidth 368
#define accPeriod 369
#define accRecovery 370
#define accSkew 371
#define accNochange 376
#define accNoChange accNochange
#define accSetuphold 377
#define accInput 402
#define accOutput 404
#define accInout 406
#define accMixedIo 407
#define accPositive 408
#define accNegative 410
#define accUnknown 412
#define accPathTerminal 420
#define accPathInput 422
#define accPathOutput 424
#define accDataPath 426
#define accTchkTerminal 428
#define accBitSelect 500
#define accPartSelect 502
#define accTask 504
#define accFunction 506
#define accStatement 508
#define accTaskCall 510
#define accFunctionCall 512
#define accSystemTask 514
#define accSystemFunction 516
#define accSystemRealFunction 518
#define accUserTask 520
#define accUserFunction 522
#define accUserRealFunction 524
#define accConstant 600
#define accConcat 610
#define accOperator 620
#define accMinTypMax 696

/* Configuration selectors. */
#define accPathDelayCount 1
#define accPathDelimStr 2
#define accDisplayErrors 3
#define accDefaultAttr0 4
#define accToHiZDelay 5
#define accEnableArgs 6
#define accDisplayWarnings 8
#define accDevelopmentVersion 11
#define accMapToMipd 17
#define accMinTypMaxDelays 19

/* Edge, timing, update, and value selectors. */
#define accNoedge 0
#define accNoEdge accNoedge
#define accEdge01 1
#define accEdge10 2
#define accEdge0x 4
#define accEdgex1 8
#define accEdge1x 16
#define accEdgex0 32
#define accPosedge 13
#define accPosEdge accPosedge
#define accNegedge 50
#define accNegEdge accNegedge

#define accDelayModeNone 0
#define accDelayModePath 1
#define accDelayModeDistrib 2
#define accDelayModeUnit 3
#define accDelayModeZero 4
#define accDelayModeVeritime 5

#define accNoDelay 0
#define accInertialDelay 1
#define accTransportDelay 2
#define accPureTransportDelay 3
#define accForceFlag 4
#define accReleaseFlag 5
#define accAssignFlag 6
#define accDeassignFlag 7

#define accBinStrVal 1
#define accOctStrVal 2
#define accDecStrVal 3
#define accHexStrVal 4
#define accScalarVal 5
#define accIntVal 6
#define accRealVal 7
#define accStringVal 8
#define accVectorVal 10

#define acc0 0
#define acc1 1
#define accX 2
#define accZ 3

#define vcl0 acc0
#define vcl1 acc1
#define vclX accX
#define vclZ accZ

#define logic_value_change 1
#define strength_value_change 2
#define real_value_change 3
#define vector_value_change 4
#define event_value_change 5
#define integer_value_change 6
#define time_value_change 7
#define sregister_value_change 8
#define vregister_value_change 9
#define realtime_value_change 10

#define vclSupply 7
#define vclStrong 6
#define vclPull 5
#define vclLarge 4
#define vclWeak 3
#define vclMedium 2
#define vclSmall 1
#define vclHighZ 0

#define vcl_verilog_logic 2
#define VCL_VERILOG_LOGIC vcl_verilog_logic
#define vcl_verilog_strength 3
#define VCL_VERILOG_STRENGTH vcl_verilog_strength
#define vcl_verilog vcl_verilog_logic
#define VCL_VERILOG vcl_verilog

#define accTime 1
#define accSimTime 2
#define accRealTime 3

#define accSimulator 1
#define accTimingAnalyzer 2
#define accFaultSimulator 3
#define accOther 4

typedef PLI_INT32 (*consumer_function)(void);

typedef struct t_acc_time {
  PLI_INT32 type;
  PLI_INT32 low;
  PLI_INT32 high;
  double real;
} s_acc_time, *p_acc_time;

typedef struct t_setval_delay {
  s_acc_time time;
  PLI_INT32 model;
} s_setval_delay, *p_setval_delay;

typedef struct t_acc_vecval {
  PLI_INT32 aval;
  PLI_INT32 bval;
} s_acc_vecval, *p_acc_vecval;

typedef struct t_setval_value {
  PLI_INT32 format;
  union {
    PLI_BYTE8* str;
    PLI_INT32 scalar;
    PLI_INT32 integer;
    double real;
    p_acc_vecval vector;
  } value;
} s_setval_value, *p_setval_value, s_acc_value, *p_acc_value;

typedef struct t_strengths {
  PLI_UBYTE8 logic_value;
  PLI_UBYTE8 strength1;
  PLI_UBYTE8 strength2;
} s_strengths, *p_strengths;

typedef struct t_vc_record {
  PLI_INT32 vc_reason;
  PLI_INT32 vc_hightime;
  PLI_INT32 vc_lowtime;
  PLI_BYTE8* user_data;
  union {
    PLI_UBYTE8 logic_value;
    double real_value;
    handle vector_handle;
    s_strengths strengths_s;
  } out_value;
} s_vc_record, *p_vc_record;

typedef struct t_location {
  PLI_INT32 line_no;
  PLI_BYTE8* filename;
} s_location, *p_location;

typedef struct t_timescale_info {
  PLI_INT16 unit;
  PLI_INT16 precision;
} s_timescale_info, *p_timescale_info;

#ifdef __cplusplus
extern "C" {
#endif

ACC_EXTERN PLI_INT32 acc_append_delays ACC_PROTO((handle object, ...));
ACC_EXTERN PLI_INT32 acc_append_pulsere
    ACC_PROTO((handle object, double val1r, double val1x, ...));
ACC_EXTERN void acc_close ACC_PROTO((void));
ACC_EXTERN handle* acc_collect ACC_PROTO(
    (handle (*next_routine)(void), handle scope, PLI_INT32* count));
ACC_EXTERN PLI_INT32 acc_compare_handles ACC_PROTO((handle lhs, handle rhs));
ACC_EXTERN PLI_INT32 acc_configure
    ACC_PROTO((PLI_INT32 item, PLI_BYTE8* value));
ACC_EXTERN PLI_INT32 acc_count
    ACC_PROTO((handle (*next_routine)(void), handle object));
ACC_EXTERN PLI_INT32 acc_fetch_argc ACC_PROTO((void));
ACC_EXTERN PLI_BYTE8** acc_fetch_argv ACC_PROTO((void));
ACC_EXTERN double acc_fetch_attribute ACC_PROTO((handle object, ...));
ACC_EXTERN PLI_INT32 acc_fetch_attribute_int ACC_PROTO((handle object, ...));
ACC_EXTERN PLI_BYTE8* acc_fetch_attribute_str ACC_PROTO((handle object, ...));
ACC_EXTERN PLI_BYTE8* acc_fetch_defname ACC_PROTO((handle object));
ACC_EXTERN PLI_INT32 acc_fetch_delay_mode ACC_PROTO((handle object));
ACC_EXTERN PLI_INT32 acc_fetch_delays ACC_PROTO((handle object, ...));
ACC_EXTERN PLI_INT32 acc_fetch_direction ACC_PROTO((handle object));
ACC_EXTERN PLI_INT32 acc_fetch_edge ACC_PROTO((handle object));
ACC_EXTERN PLI_BYTE8* acc_fetch_fullname ACC_PROTO((handle object));
ACC_EXTERN PLI_INT32 acc_fetch_fulltype ACC_PROTO((handle object));
ACC_EXTERN PLI_INT32 acc_fetch_index ACC_PROTO((handle object));
ACC_EXTERN double acc_fetch_itfarg ACC_PROTO((PLI_INT32 index, handle instance));
ACC_EXTERN PLI_INT32 acc_fetch_itfarg_int
    ACC_PROTO((PLI_INT32 index, handle instance));
ACC_EXTERN PLI_BYTE8* acc_fetch_itfarg_str
    ACC_PROTO((PLI_INT32 index, handle instance));
ACC_EXTERN PLI_INT32 acc_fetch_location
    ACC_PROTO((p_location location, handle object));
ACC_EXTERN PLI_BYTE8* acc_fetch_name ACC_PROTO((handle object));
ACC_EXTERN PLI_INT32 acc_fetch_paramtype ACC_PROTO((handle parameter));
ACC_EXTERN double acc_fetch_paramval ACC_PROTO((handle parameter));
ACC_EXTERN PLI_INT32 acc_fetch_polarity ACC_PROTO((handle path));
ACC_EXTERN PLI_INT32 acc_fetch_precision ACC_PROTO((void));
ACC_EXTERN PLI_INT32 acc_fetch_pulsere ACC_PROTO(
    (handle path, double* reject, double* error, ...));
ACC_EXTERN PLI_INT32 acc_fetch_range
    ACC_PROTO((handle object, PLI_INT32* msb, PLI_INT32* lsb));
ACC_EXTERN PLI_INT32 acc_fetch_size ACC_PROTO((handle object));
ACC_EXTERN double acc_fetch_tfarg ACC_PROTO((PLI_INT32 index));
ACC_EXTERN PLI_INT32 acc_fetch_tfarg_int ACC_PROTO((PLI_INT32 index));
ACC_EXTERN PLI_BYTE8* acc_fetch_tfarg_str ACC_PROTO((PLI_INT32 index));
ACC_EXTERN void acc_fetch_timescale_info
    ACC_PROTO((handle object, p_timescale_info info));
ACC_EXTERN PLI_INT32 acc_fetch_type ACC_PROTO((handle object));
ACC_EXTERN PLI_BYTE8* acc_fetch_type_str ACC_PROTO((PLI_INT32 type));
ACC_EXTERN PLI_BYTE8* acc_fetch_value
    ACC_PROTO((handle object, PLI_BYTE8* format, p_acc_value value));
ACC_EXTERN void acc_free ACC_PROTO((handle* objects));
ACC_EXTERN handle acc_handle_by_name
    ACC_PROTO((PLI_BYTE8* name, handle scope));
ACC_EXTERN handle acc_handle_condition ACC_PROTO((handle object));
ACC_EXTERN handle acc_handle_conn ACC_PROTO((handle terminal));
ACC_EXTERN handle acc_handle_datapath ACC_PROTO((handle path));
ACC_EXTERN handle acc_handle_hiconn ACC_PROTO((handle port));
ACC_EXTERN handle acc_handle_interactive_scope ACC_PROTO((void));
ACC_EXTERN handle acc_handle_itfarg
    ACC_PROTO((PLI_INT32 index, void* instance));
ACC_EXTERN handle acc_handle_loconn ACC_PROTO((handle port));
ACC_EXTERN handle acc_handle_modpath ACC_PROTO(
    (handle module, PLI_BYTE8* input, PLI_BYTE8* output, ...));
ACC_EXTERN handle acc_handle_notifier ACC_PROTO((handle timing_check));
ACC_EXTERN handle acc_handle_object ACC_PROTO((PLI_BYTE8* name, ...));
ACC_EXTERN handle acc_handle_parent ACC_PROTO((handle object));
ACC_EXTERN handle acc_handle_path
    ACC_PROTO((handle source, handle destination));
ACC_EXTERN handle acc_handle_pathin ACC_PROTO((handle path));
ACC_EXTERN handle acc_handle_pathout ACC_PROTO((handle path));
ACC_EXTERN handle acc_handle_port
    ACC_PROTO((handle module, PLI_INT32 index, ...));
ACC_EXTERN handle acc_handle_scope ACC_PROTO((handle object));
ACC_EXTERN handle acc_handle_simulated_net ACC_PROTO((handle net));
ACC_EXTERN handle acc_handle_tchk ACC_PROTO(
    (handle module, PLI_INT32 type, PLI_BYTE8* argument,
     PLI_INT32 edge, ...));
ACC_EXTERN handle acc_handle_tchkarg1 ACC_PROTO((handle timing_check));
ACC_EXTERN handle acc_handle_tchkarg2 ACC_PROTO((handle timing_check));
ACC_EXTERN handle acc_handle_terminal
    ACC_PROTO((handle primitive, PLI_INT32 index));
ACC_EXTERN handle acc_handle_tfarg ACC_PROTO((PLI_INT32 index));
ACC_EXTERN handle acc_handle_tfinst ACC_PROTO((void));
ACC_EXTERN PLI_INT32 acc_initialize ACC_PROTO((void));
ACC_EXTERN handle acc_next
    ACC_PROTO((PLI_INT32* types, handle scope, handle object));
ACC_EXTERN handle acc_next_bit ACC_PROTO((handle vector, handle bit));
ACC_EXTERN handle acc_next_cell ACC_PROTO((handle scope, handle cell));
ACC_EXTERN handle acc_next_cell_load ACC_PROTO((handle net, handle load));
ACC_EXTERN handle acc_next_child ACC_PROTO((handle module, handle child));
ACC_EXTERN handle acc_next_driver ACC_PROTO((handle net, handle driver));
ACC_EXTERN handle acc_next_hiconn ACC_PROTO((handle port, handle connection));
ACC_EXTERN handle acc_next_input ACC_PROTO((handle path, handle input));
ACC_EXTERN handle acc_next_load ACC_PROTO((handle net, handle load));
ACC_EXTERN handle acc_next_loconn ACC_PROTO((handle port, handle connection));
ACC_EXTERN handle acc_next_modpath ACC_PROTO((handle module, handle path));
ACC_EXTERN handle acc_next_net ACC_PROTO((handle module, handle net));
ACC_EXTERN handle acc_next_output ACC_PROTO((handle path, handle output));
ACC_EXTERN handle acc_next_parameter
    ACC_PROTO((handle module, handle parameter));
ACC_EXTERN handle acc_next_port ACC_PROTO((handle object, handle port));
ACC_EXTERN handle acc_next_portout ACC_PROTO((handle module, handle port));
ACC_EXTERN handle acc_next_primitive
    ACC_PROTO((handle module, handle primitive));
ACC_EXTERN handle acc_next_scope ACC_PROTO((handle parent, handle scope));
ACC_EXTERN handle acc_next_specparam
    ACC_PROTO((handle module, handle parameter));
ACC_EXTERN handle acc_next_tchk
    ACC_PROTO((handle module, handle timing_check));
ACC_EXTERN handle acc_next_terminal
    ACC_PROTO((handle primitive, handle terminal));
ACC_EXTERN handle acc_next_topmod ACC_PROTO((handle module));
ACC_EXTERN PLI_INT32 acc_object_of_type
    ACC_PROTO((handle object, PLI_INT32 type));
ACC_EXTERN PLI_INT32 acc_object_in_typelist
    ACC_PROTO((handle object, PLI_INT32* types));
ACC_EXTERN PLI_INT32 acc_product_type ACC_PROTO((void));
ACC_EXTERN PLI_BYTE8* acc_product_version ACC_PROTO((void));
ACC_EXTERN PLI_INT32 acc_release_object ACC_PROTO((handle object));
ACC_EXTERN PLI_INT32 acc_replace_delays ACC_PROTO((handle object, ...));
ACC_EXTERN PLI_INT32 acc_replace_pulsere
    ACC_PROTO((handle object, double reject, double error, ...));
ACC_EXTERN void acc_reset_buffer ACC_PROTO((void));
ACC_EXTERN PLI_INT32 acc_set_interactive_scope
    ACC_PROTO((handle scope, PLI_INT32 callback));
ACC_EXTERN PLI_INT32 acc_set_pulsere
    ACC_PROTO((handle path, double reject, double error));
ACC_EXTERN PLI_BYTE8* acc_set_scope ACC_PROTO((handle object, ...));
ACC_EXTERN PLI_INT32 acc_set_value
    ACC_PROTO((handle object, p_setval_value value, p_setval_delay delay));
ACC_EXTERN void acc_vcl_add ACC_PROTO(
    (handle object, PLI_INT32 (*consumer)(p_vc_record),
     PLI_BYTE8* user_data, PLI_INT32 flags));
ACC_EXTERN void acc_vcl_delete ACC_PROTO(
    (handle object, PLI_INT32 (*consumer)(p_vc_record),
     PLI_BYTE8* user_data, PLI_INT32 flags));
ACC_EXTERN PLI_BYTE8* acc_version ACC_PROTO((void));

PLI_VEXTERN PLI_DLLISPEC PLI_INT32 acc_error_flag;

#ifdef __cplusplus
}
#endif

#define acc_handle_calling_mod_m acc_handle_parent((handle)tf_getinstance())

#undef ACC_PROTO
#undef ACC_EXTERN
#ifdef ACC_USER_DEFINED_EXTERN
#undef ACC_USER_DEFINED_EXTERN
#undef PLI_EXTERN
#endif
#ifdef ACC_USER_DEFINED_VEXTERN
#undef ACC_USER_DEFINED_VEXTERN
#undef PLI_VEXTERN
#endif
#ifdef ACC_USER_DEFINED_DLLISPEC
#undef ACC_USER_DEFINED_DLLISPEC
#undef PLI_DLLISPEC
#endif

#endif
