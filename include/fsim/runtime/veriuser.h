// SPDX-License-Identifier: Apache-2.0

#ifndef VERIUSER_H
#define VERIUSER_H

/* Public IEEE TF declarations. This header is usable from C and C++. */

#ifndef PLI_TYPES
#define PLI_TYPES
typedef int PLI_INT32;
typedef unsigned int PLI_UINT32;
typedef short PLI_INT16;
typedef unsigned short PLI_UINT16;
typedef char PLI_BYTE8;
typedef unsigned char PLI_UBYTE8;
#endif

#if defined(_WIN32)
#ifndef PLI_DLLISPEC
#if defined(FSIM_TF_LINK_SURFACE_BUILD)
#define PLI_DLLISPEC __declspec(dllexport)
#else
#define PLI_DLLISPEC __declspec(dllimport)
#endif
#define VERIUSER_DEFINED_DLLISPEC 1
#endif
#ifndef PLI_DLLESPEC
#define PLI_DLLESPEC __declspec(dllexport)
#define VERIUSER_DEFINED_DLLESPEC 1
#endif
#else
#ifndef PLI_DLLISPEC
#define PLI_DLLISPEC __attribute__((visibility("default")))
#define VERIUSER_DEFINED_DLLISPEC 1
#endif
#ifndef PLI_DLLESPEC
#define PLI_DLLESPEC __attribute__((visibility("default")))
#define VERIUSER_DEFINED_DLLESPEC 1
#endif
#endif

#ifndef PLI_EXTERN
#define PLI_EXTERN
#define VERIUSER_DEFINED_EXTERN 1
#endif
#ifndef PLI_VEXTERN
#define PLI_VEXTERN extern
#define VERIUSER_DEFINED_VEXTERN 1
#endif

#define PROTO_PARAMS(params) params
#define XXTERN PLI_EXTERN PLI_DLLISPEC
#define EETERN PLI_EXTERN PLI_DLLESPEC

/* Historical source aliases are retained for C without hiding C++ keywords. */
#ifndef PLI_EXTRAS
#define PLI_EXTRAS
#if !defined(__cplusplus)
#define bool int
#define true 1
#define false 0
#endif
#define TRUE 1
#define FALSE 0
#define null 0L
#endif

#define ERR_MESSAGE 1
#define ERR_WARNING 2
#define ERR_ERROR 3
#define ERR_INTERNAL 4
#define ERR_SYSTEM 5

#define reason_checktf 1
#define REASON_CHECKTF reason_checktf
#define reason_sizetf 2
#define REASON_SIZETF reason_sizetf
#define reason_calltf 3
#define REASON_CALLTF reason_calltf
#define reason_save 4
#define REASON_SAVE reason_save
#define reason_restart 5
#define REASON_RESTART reason_restart
#define reason_disable 6
#define REASON_DISABLE reason_disable
#define reason_paramvc 7
#define REASON_PARAMVC reason_paramvc
#define reason_synch 8
#define REASON_SYNCH reason_synch
#define reason_finish 9
#define REASON_FINISH reason_finish
#define reason_reactivate 10
#define REASON_REACTIVATE reason_reactivate
#define reason_rosynch 11
#define REASON_ROSYNCH reason_rosynch
#define reason_paramdrc 15
#define REASON_PARAMDRC reason_paramdrc
#define reason_endofcompile 16
#define REASON_ENDOFCOMPILE reason_endofcompile
#define reason_scope 17
#define REASON_SCOPE reason_scope
#define reason_interactive 18
#define REASON_INTERACTIVE reason_interactive
#define reason_reset 19
#define REASON_RESET reason_reset
#define reason_endofreset 20
#define REASON_ENDOFRESET reason_endofreset
#define reason_force 21
#define REASON_FORCE reason_force
#define reason_release 22
#define REASON_RELEASE reason_release
#define reason_startofsave 27
#define reason_startofrestart 28
#define REASON_MAX 28

#define tf_nullparam 0
#define TF_NULLPARAM tf_nullparam
#define tf_string 1
#define TF_STRING tf_string
#define tf_specialparam 2
#define TF_SPECIALPARAM tf_specialparam
#define tf_readonly 10
#define TF_READONLY tf_readonly
#define tf_readwrite 11
#define TF_READWRITE tf_readwrite
#define tf_rwbitselect 12
#define TF_RWBITSELECT tf_rwbitselect
#define tf_rwpartselect 13
#define TF_RWPARTSELECT tf_rwpartselect
#define tf_rwmemselect 14
#define TF_RWMEMSELECT tf_rwmemselect
#define tf_readonlyreal 15
#define TF_READONLYREAL tf_readonlyreal
#define tf_readwritereal 16
#define TF_READWRITEREAL tf_readwritereal

#define tf_null_node 100
#define TF_NULL_NODE tf_null_node
#define tf_reg_node 101
#define TF_REG_NODE tf_reg_node
#define tf_integer_node 102
#define TF_INTEGER_NODE tf_integer_node
#define tf_time_node 103
#define TF_TIME_NODE tf_time_node
#define tf_netvector_node 104
#define TF_NETVECTOR_NODE tf_netvector_node
#define tf_netscalar_node 105
#define TF_NETSCALAR_NODE tf_netscalar_node
#define tf_memory_node 106
#define TF_MEMORY_NODE tf_memory_node
#define tf_real_node 107
#define TF_REAL_NODE tf_real_node

typedef struct t_vecval {
  PLI_INT32 avalbits;
  PLI_INT32 bvalbits;
} s_vecval, *p_vecval;

typedef struct t_strengthval {
  PLI_INT32 strength0;
  PLI_INT32 strength1;
} s_strengthval, *p_strengthval;

typedef struct t_tfexprinfo {
  PLI_INT16 expr_type;
  PLI_INT16 padding;
  struct t_vecval* expr_value_p;
  double real_value;
  PLI_BYTE8* expr_string;
  PLI_INT32 expr_ngroups;
  PLI_INT32 expr_vec_size;
  PLI_INT32 expr_sign;
  PLI_INT32 expr_lhs_select;
  PLI_INT32 expr_rhs_select;
} s_tfexprinfo, *p_tfexprinfo;

typedef struct t_tfnodeinfo {
  PLI_INT16 node_type;
  PLI_INT16 padding;
  union {
    struct t_vecval* vecval_p;
    struct t_strengthval* strengthval_p;
    PLI_BYTE8* memoryval_p;
    double* real_val_p;
  } node_value;
  PLI_BYTE8* node_symbol;
  PLI_INT32 node_ngroups;
  PLI_INT32 node_vec_size;
  PLI_INT32 node_sign;
  PLI_INT32 node_ms_index;
  PLI_INT32 node_ls_index;
  PLI_INT32 node_mem_size;
  PLI_INT32 node_lhs_element;
  PLI_INT32 node_rhs_element;
  PLI_INT32* node_handle;
} s_tfnodeinfo, *p_tfnodeinfo;

#ifdef __cplusplus
extern "C" {
#endif

XXTERN void io_mcdprintf PROTO_PARAMS((PLI_INT32 mcd, PLI_BYTE8* format, ...));
XXTERN void io_printf PROTO_PARAMS((PLI_BYTE8* format, ...));
XXTERN PLI_BYTE8* mc_scan_plusargs PROTO_PARAMS((PLI_BYTE8* plusarg));
XXTERN PLI_INT32 tf_add_long PROTO_PARAMS((PLI_INT32* low1, PLI_INT32* high1,
                                           PLI_INT32 low2, PLI_INT32 high2));
XXTERN PLI_INT32 tf_asynchoff PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_asynchon PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_clearalldelays PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_compare_long PROTO_PARAMS((PLI_UINT32 low1,
                                               PLI_UINT32 high1,
                                               PLI_UINT32 low2,
                                               PLI_UINT32 high2));
XXTERN PLI_INT32 tf_copypvc_flag PROTO_PARAMS((PLI_INT32 nparam));
XXTERN void tf_divide_long PROTO_PARAMS((PLI_INT32* low1, PLI_INT32* high1,
                                         PLI_INT32 low2, PLI_INT32 high2));
XXTERN PLI_INT32 tf_dofinish PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_dostop PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_error PROTO_PARAMS((PLI_BYTE8* format, ...));
XXTERN PLI_INT32 tf_evaluatep PROTO_PARAMS((PLI_INT32 pnum));
XXTERN p_tfexprinfo tf_exprinfo
    PROTO_PARAMS((PLI_INT32 pnum, p_tfexprinfo info));
XXTERN PLI_BYTE8* tf_getcstringp PROTO_PARAMS((PLI_INT32 nparam));
XXTERN PLI_BYTE8* tf_getinstance PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_getlongp
    PROTO_PARAMS((PLI_INT32* high_value, PLI_INT32 pnum));
XXTERN PLI_INT32 tf_getlongtime PROTO_PARAMS((PLI_INT32* high_time));
XXTERN PLI_INT32 tf_getnextlongtime
    PROTO_PARAMS((PLI_INT32* low_time, PLI_INT32* high_time));
XXTERN PLI_INT32 tf_getp PROTO_PARAMS((PLI_INT32 pnum));
XXTERN PLI_INT32 tf_getpchange PROTO_PARAMS((PLI_INT32 nparam));
XXTERN double tf_getrealp PROTO_PARAMS((PLI_INT32 pnum));
XXTERN double tf_getrealtime PROTO_PARAMS((void));
XXTERN PLI_BYTE8* tf_gettflist PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_gettime PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_gettimeprecision PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_gettimeunit PROTO_PARAMS((void));
XXTERN PLI_BYTE8* tf_getworkarea PROTO_PARAMS((void));

XXTERN PLI_INT32 tf_iasynchoff PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_iasynchon PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_iclearalldelays PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_icopypvc_flag
    PROTO_PARAMS((PLI_INT32 nparam, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_ievaluatep
    PROTO_PARAMS((PLI_INT32 pnum, PLI_BYTE8* instance));
XXTERN p_tfexprinfo tf_iexprinfo
    PROTO_PARAMS((PLI_INT32 pnum, p_tfexprinfo info, PLI_BYTE8* instance));
XXTERN PLI_BYTE8* tf_igetcstringp
    PROTO_PARAMS((PLI_INT32 nparam, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_igetlongp PROTO_PARAMS(
    (PLI_INT32* high_value, PLI_INT32 pnum, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_igetlongtime
    PROTO_PARAMS((PLI_INT32* high_time, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_igetp
    PROTO_PARAMS((PLI_INT32 pnum, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_igetpchange
    PROTO_PARAMS((PLI_INT32 nparam, PLI_BYTE8* instance));
XXTERN double tf_igetrealp
    PROTO_PARAMS((PLI_INT32 pnum, PLI_BYTE8* instance));
XXTERN double tf_igetrealtime PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_igettime PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_igettimeprecision PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_igettimeunit PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_BYTE8* tf_igetworkarea PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_BYTE8* tf_imipname PROTO_PARAMS((PLI_BYTE8* cell));
XXTERN PLI_INT32 tf_imovepvc_flag
    PROTO_PARAMS((PLI_INT32 nparam, PLI_BYTE8* instance));
XXTERN p_tfnodeinfo tf_inodeinfo
    PROTO_PARAMS((PLI_INT32 pnum, p_tfnodeinfo info, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_inump PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_ipropagatep
    PROTO_PARAMS((PLI_INT32 pnum, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_iputlongp PROTO_PARAMS((PLI_INT32 pnum,
                                           PLI_INT32 low_value,
                                           PLI_INT32 high_value,
                                           PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_iputp PROTO_PARAMS(
    (PLI_INT32 pnum, PLI_INT32 value, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_iputrealp
    PROTO_PARAMS((PLI_INT32 pnum, double value, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_irosynchronize PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_isetdelay
    PROTO_PARAMS((PLI_INT32 delay, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_isetlongdelay PROTO_PARAMS(
    (PLI_INT32 low_delay, PLI_INT32 high_delay, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_isetrealdelay
    PROTO_PARAMS((double delay, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_isetworkarea
    PROTO_PARAMS((PLI_BYTE8* work_area, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_isizep
    PROTO_PARAMS((PLI_INT32 pnum, PLI_BYTE8* instance));
XXTERN PLI_BYTE8* tf_ispname PROTO_PARAMS((PLI_BYTE8* cell));
XXTERN PLI_INT32 tf_istrdelputp PROTO_PARAMS(
    (PLI_INT32 nparam, PLI_INT32 bit_length, PLI_INT32 format,
     PLI_BYTE8* value, PLI_INT32 delay, PLI_INT32 delay_type,
     PLI_BYTE8* instance));
XXTERN PLI_BYTE8* tf_istrgetp PROTO_PARAMS(
    (PLI_INT32 pnum, PLI_INT32 format, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_istrlongdelputp PROTO_PARAMS(
    (PLI_INT32 nparam, PLI_INT32 bit_length, PLI_INT32 format,
     PLI_BYTE8* value, PLI_INT32 low_delay, PLI_INT32 high_delay,
     PLI_INT32 delay_type, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_istrrealdelputp PROTO_PARAMS(
    (PLI_INT32 nparam, PLI_INT32 bit_length, PLI_INT32 format,
     PLI_BYTE8* value, double delay, PLI_INT32 delay_type,
     PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_isynchronize PROTO_PARAMS((PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_itestpvc_flag
    PROTO_PARAMS((PLI_INT32 nparam, PLI_BYTE8* instance));
XXTERN PLI_INT32 tf_itypep
    PROTO_PARAMS((PLI_INT32 pnum, PLI_BYTE8* instance));

XXTERN void tf_long_to_real
    PROTO_PARAMS((PLI_INT32 low_value, PLI_INT32 high_value, double* result));
XXTERN PLI_BYTE8* tf_longtime_tostr
    PROTO_PARAMS((PLI_INT32 low_time, PLI_INT32 high_time));
XXTERN PLI_INT32 tf_message PROTO_PARAMS((PLI_INT32 level,
                                          PLI_BYTE8* facility,
                                          PLI_BYTE8* message_number,
                                          PLI_BYTE8* message, ...));
XXTERN PLI_BYTE8* tf_mipname PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_movepvc_flag PROTO_PARAMS((PLI_INT32 nparam));
XXTERN void tf_multiply_long PROTO_PARAMS((PLI_INT32* low1, PLI_INT32* high1,
                                           PLI_INT32 low2, PLI_INT32 high2));
XXTERN p_tfnodeinfo tf_nodeinfo
    PROTO_PARAMS((PLI_INT32 pnum, p_tfnodeinfo info));
XXTERN PLI_INT32 tf_nump PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_propagatep PROTO_PARAMS((PLI_INT32 pnum));
XXTERN PLI_INT32 tf_putlongp PROTO_PARAMS(
    (PLI_INT32 pnum, PLI_INT32 low_value, PLI_INT32 high_value));
XXTERN PLI_INT32 tf_putp
    PROTO_PARAMS((PLI_INT32 pnum, PLI_INT32 value));
XXTERN PLI_INT32 tf_putrealp
    PROTO_PARAMS((PLI_INT32 pnum, double value));
XXTERN PLI_INT32 tf_read_restart
    PROTO_PARAMS((PLI_BYTE8* block, PLI_INT32 block_length));
XXTERN void tf_real_to_long PROTO_PARAMS(
    (double value, PLI_INT32* low_value, PLI_INT32* high_value));
XXTERN PLI_INT32 tf_rosynchronize PROTO_PARAMS((void));
XXTERN void tf_scale_longdelay PROTO_PARAMS(
    (PLI_BYTE8* cell, PLI_INT32 low_delay, PLI_INT32 high_delay,
     PLI_INT32* scaled_low_delay, PLI_INT32* scaled_high_delay));
XXTERN void tf_scale_realdelay
    PROTO_PARAMS((PLI_BYTE8* cell, double delay, double* scaled_delay));
XXTERN PLI_INT32 tf_setdelay PROTO_PARAMS((PLI_INT32 delay));
XXTERN PLI_INT32 tf_setlongdelay
    PROTO_PARAMS((PLI_INT32 low_delay, PLI_INT32 high_delay));
XXTERN PLI_INT32 tf_setrealdelay PROTO_PARAMS((double delay));
XXTERN PLI_INT32 tf_setworkarea PROTO_PARAMS((PLI_BYTE8* work_area));
XXTERN PLI_INT32 tf_sizep PROTO_PARAMS((PLI_INT32 pnum));
XXTERN PLI_BYTE8* tf_spname PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_strdelputp PROTO_PARAMS(
    (PLI_INT32 nparam, PLI_INT32 bit_length, PLI_INT32 format,
     PLI_BYTE8* value, PLI_INT32 delay, PLI_INT32 delay_type));
XXTERN PLI_BYTE8* tf_strgetp
    PROTO_PARAMS((PLI_INT32 pnum, PLI_INT32 format));
XXTERN PLI_BYTE8* tf_strgettime PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_strlongdelputp PROTO_PARAMS(
    (PLI_INT32 nparam, PLI_INT32 bit_length, PLI_INT32 format,
     PLI_BYTE8* value, PLI_INT32 low_delay, PLI_INT32 high_delay,
     PLI_INT32 delay_type));
XXTERN PLI_INT32 tf_strrealdelputp PROTO_PARAMS(
    (PLI_INT32 nparam, PLI_INT32 bit_length, PLI_INT32 format,
     PLI_BYTE8* value, double delay, PLI_INT32 delay_type));
XXTERN PLI_INT32 tf_subtract_long
    PROTO_PARAMS((PLI_INT32* low1, PLI_INT32* high1, PLI_INT32 low2,
                  PLI_INT32 high2));
XXTERN PLI_INT32 tf_synchronize PROTO_PARAMS((void));
XXTERN PLI_INT32 tf_testpvc_flag PROTO_PARAMS((PLI_INT32 nparam));
XXTERN PLI_INT32 tf_text PROTO_PARAMS((PLI_BYTE8* format, ...));
XXTERN PLI_INT32 tf_typep PROTO_PARAMS((PLI_INT32 pnum));
XXTERN void tf_unscale_longdelay PROTO_PARAMS(
    (PLI_BYTE8* cell, PLI_INT32 low_delay, PLI_INT32 high_delay,
     PLI_INT32* unscaled_low_delay, PLI_INT32* unscaled_high_delay));
XXTERN void tf_unscale_realdelay
    PROTO_PARAMS((PLI_BYTE8* cell, double delay, double* unscaled_delay));
XXTERN PLI_INT32 tf_warning PROTO_PARAMS((PLI_BYTE8* format, ...));
XXTERN PLI_INT32 tf_write_save
    PROTO_PARAMS((PLI_BYTE8* block, PLI_INT32 block_length));
XXTERN PLI_BYTE8* tf_getroutine PROTO_PARAMS((void));
XXTERN PLI_BYTE8* tf_igetroutine PROTO_PARAMS((PLI_BYTE8* instance));
EETERN PLI_INT32 err_intercept
    PROTO_PARAMS((PLI_INT32 level, PLI_BYTE8* facility, PLI_BYTE8* code));

PLI_VEXTERN PLI_DLLESPEC PLI_BYTE8* veriuser_version_str;
PLI_VEXTERN PLI_DLLESPEC PLI_INT32 (*endofcompile_routines[])(void);

#ifdef __cplusplus
}
#endif

#undef PROTO_PARAMS
#undef XXTERN
#undef EETERN
#ifdef VERIUSER_DEFINED_EXTERN
#undef VERIUSER_DEFINED_EXTERN
#undef PLI_EXTERN
#endif
#ifdef VERIUSER_DEFINED_VEXTERN
#undef VERIUSER_DEFINED_VEXTERN
#undef PLI_VEXTERN
#endif
#ifdef VERIUSER_DEFINED_DLLISPEC
#undef VERIUSER_DEFINED_DLLISPEC
#undef PLI_DLLISPEC
#endif
#ifdef VERIUSER_DEFINED_DLLESPEC
#undef VERIUSER_DEFINED_DLLESPEC
#undef PLI_DLLESPEC
#endif

#endif
