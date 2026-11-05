// SPDX-License-Identifier: Apache-2.0

// PLI 1.0 task/function tables (IEEE 1364-2001 21.2 and Annex F, veriusertfs)
// registered over the VPI. Each table cell becomes a VPI system task or
// function whose routines run the cell's checktf, sizetf, calltf and
// misctf inside a TF call context, so the tf_* routines of tf_link.cpp
// see the call's arguments, time and instance. An ACC handle context over
// the VPI gives the access routines (IEEE 1364-2001 Clauses 22 and 23) the
// same call.

#include "vpi_user.h"
#include "fsim/runtime/acc_handle_bridge.h"
#include "acc_internal.hpp"
#include "fsim/runtime/veriuser.h"
#include "fsim/runtime/tf_call_bridge.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Instance;

// A misctf call waiting for the routine in progress to return: an argument
// whose change calls misctf(reason_paramvc) once tf_asynchon enabled it, or
// a force, release or disable statement (IEEE 1364-2001 21.3.2, Table 88).
struct Watch {
  Instance* instance{};
  PLI_INT32 parameter{};
  PLI_INT32 reason{reason_paramvc};
};

// A value change of a link added by acc_vcl_add, awaiting its consumer.
struct VclDelivery {
  Instance* instance{};
  uint64_t link{};
  uint64_t sequence{};
  PLI_INT32 reason{};
  PLI_INT32 time_high{};
  PLI_INT32 time_low{};
  uint32_t logic_value{};
  uint32_t strength1{};
  uint32_t strength2{};
  uint32_t vector_width{};
  std::vector<fsim_acc_logic_word_v3> words;
  double real{};
};

// The ACC handle context of a call site. It outlives each routine call so a
// value change link (acc_vcl_add) keeps the context it was added under.
struct AccState {
  std::vector<uint64_t> arguments;
  fsim_acc_tf_context_v3 tf{};
  fsim_acc_handle_context_v3 context{};
  bool prepared{};
};

struct Instance {
  p_tfcell cell{};
  vpiHandle call{};
  vpiHandle module{};
  fsim_tf_instance_bridge_v3 bridge{};
  PLI_BYTE8* work_area{};
  std::string module_name;
  std::string scope_name;
  std::string routine_name;
  std::vector<vpiHandle> handles;
  std::vector<fsim_tf_argument_bridge_v3> arguments;
  std::deque<std::string> expressions;
  uint32_t result_width{};
  uint32_t phase{};
  bool abort_requested{};
  // calltf has run: misctf hears of most reasons only from then on (21.1.4).
  bool executed{};
  std::array<bool, 2> synchronization_pending{};
  bool asynchronous{};
  std::deque<Watch> watches;
  // The current and saved pvc flag of each argument (IEEE 1364-2001 25.9),
  // sized when tf_asynchon first enables them.
  std::vector<uint8_t> pvc_current;
  std::vector<uint8_t> pvc_saved;
  // Design time ticks per tick of the module's precision.
  uint64_t precision_factor{1};
  // The node facts of each argument that tf_nodeinfo reports, and the bytes
  // of the memory argument it last read (IEEE 1364-2001 25.35).
  std::vector<fsim_tf_host_node_v3> nodes;
  std::vector<PLI_BYTE8> memory_bytes;
  AccState acc;
};

// Routines run one at a time: the TF and ACC call contexts do not nest, so
// a parameter change a routine causes calls misctf after it returns.
bool routine_active{};
std::deque<Watch> pending_changes;
std::deque<VclDelivery> pending_vcl;

std::unordered_map<vpiHandle, std::unique_ptr<Instance>>& instances() {
  static std::unordered_map<vpiHandle, std::unique_ptr<Instance>> table;
  return table;
}

PLI_BYTE8* text(const char* const value) {
  return const_cast<PLI_BYTE8*>(value);
}

bool runtime_phase(const uint32_t phase) {
  return phase != FSIM_TF_CALL_PHASE_CHECK &&
         phase != FSIM_TF_CALL_PHASE_SIZE;
}

bool writable_phase(const uint32_t phase) {
  return phase == FSIM_TF_CALL_PHASE_CALL ||
         phase == FSIM_TF_CALL_PHASE_SYNCHRONIZE ||
         phase == FSIM_TF_CALL_PHASE_REACTIVATE;
}

bool writable_kind(const PLI_INT32 kind) {
  return kind == tf_readwrite || kind == tf_rwbitselect ||
         kind == tf_rwpartselect || kind == tf_rwmemselect ||
         kind == tf_readwritereal;
}

std::string full_name(const vpiHandle object, const char* const fallback) {
  const char* const name =
      object == nullptr ? nullptr : vpi_get_str(vpiFullName, object);
  return name == nullptr || *name == '\0' ? std::string{fallback}
                                          : std::string{name};
}

std::string text_of(const PLI_INT32 property, const vpiHandle object) {
  const auto* const value = object == nullptr ? nullptr
                                               : vpi_get_str(property, object);
  return value == nullptr ? std::string{} : std::string{value};
}

// $f(a,,b): the VPI presents the omitted argument as a null operation
// (IEEE 1364-2001 25.36).
bool is_null_argument(const vpiHandle handle) {
  return vpi_get(vpiType, handle) == vpiOperation &&
         vpi_get(vpiOpType, handle) == vpiNullOp;
}

// The index of an array word: tf_exprinfo and tf_nodeinfo give it as the
// select of a memory word (IEEE 1364-2001 25.15, 25.35).
void word_index(const vpiHandle word, fsim_tf_argument_bridge_v3& argument) {
  // The index of a one-dimensional array's word, else the first of the
  // indices of a multidimensional one (IEEE 1800-2023 37.17).
  if (const auto index = vpi_handle(vpiIndex, word)) {
    s_vpi_value bound{vpiIntVal, {}};
    vpi_get_value(index, &bound);
    argument.lhs_select = argument.rhs_select = bound.value.integer;
    return;
  }
  if (const auto iterator = vpi_iterate(vpiIndex, word)) {
    if (const auto index = vpi_scan(iterator)) {
      s_vpi_value bound{vpiIntVal, {}};
      vpi_get_value(index, &bound);
      argument.lhs_select = argument.rhs_select = bound.value.integer;
      (void)vpi_free_object(iterator);
    }
  }
}

// The tf_typep class of an argument (IEEE 1364-2001 25.61, Tables 187 and 202).
fsim_tf_argument_bridge_v3 classify(const vpiHandle handle) {
  fsim_tf_argument_bridge_v3 argument{};
  // No select: the node spans its whole width (tf_exprinfo, tf_nodeinfo).
  argument.lhs_select = -1;
  argument.rhs_select = -1;
  if (handle == nullptr || is_null_argument(handle)) {
    argument.kind = tf_nullparam;
    return argument;
  }
  const auto size = vpi_get(vpiSize, handle);
  argument.width = size > 0 ? static_cast<uint32_t>(size) : 32u;
  argument.is_signed = vpi_get(vpiSigned, handle) == 1 ? 1u : 0u;
  const auto type = vpi_get(vpiType, handle);
  const auto rw_select = [&](const PLI_INT32 kind) {
    const auto parent = vpi_handle(vpiParent, handle);
    const auto parent_type = parent == nullptr ? 0 : vpi_get(vpiType, parent);
    return parent_type == vpiNet || parent_type == vpiNetArray ? tf_readonly
                                                               : kind;
  };
  switch (type) {
  case vpiConstant:
  case vpiParameter:
    switch (vpi_get(vpiConstType, handle)) {
    case vpiStringConst:
      argument.kind = tf_string;
      argument.width = std::max(argument.width, 8u);
      break;
    case vpiRealConst:
      argument.kind = tf_readonlyreal;
      argument.width = 64u;
      break;
    default:
      argument.kind = tf_readonly;
      break;
    }
    break;
  case vpiRealVar:
    argument.kind = tf_readwritereal;
    argument.width = 64u;
    break;
  case vpiReg:
  case vpiIntegerVar:
  case vpiTimeVar: {
    // A word of an array is a memory select (IEEE 1800-2023 37.17).
    const auto parent = vpi_handle(vpiParent, handle);
    argument.kind = parent != nullptr && vpi_get(vpiType, parent) == vpiRegArray
        ? tf_rwmemselect
        : tf_readwrite;
    if (argument.kind == tf_rwmemselect) {
      word_index(handle, argument);
    }
    break;
  }
  case vpiMemoryWord:
    argument.kind = tf_rwmemselect;
    word_index(handle, argument);
    break;
  case vpiBitSelect:
  case vpiNetBit:
  case vpiRegBit:
    argument.kind = rw_select(tf_rwbitselect);
    if (const auto index = vpi_handle(vpiIndex, handle)) {
      s_vpi_value bound{vpiIntVal, {}};
      vpi_get_value(index, &bound);
      argument.lhs_select = argument.rhs_select = bound.value.integer;
    }
    break;
  case vpiPartSelect:
    argument.kind = rw_select(tf_rwpartselect);
    if (const auto left = vpi_handle(vpiLeftRange, handle)) {
      if (const auto right = vpi_handle(vpiRightRange, handle)) {
        s_vpi_value bound{vpiIntVal, {}};
        vpi_get_value(left, &bound);
        argument.lhs_select = bound.value.integer;
        bound = {vpiIntVal, {}};
        vpi_get_value(right, &bound);
        argument.rhs_select = bound.value.integer;
      }
    }
    break;
  // A whole memory is a memory-word select without an index (25.35).
  case vpiRegArray:
  case vpiMemory:
    argument.kind = tf_rwmemselect;
    // The element's width: tf_nodeinfo reads each word of the memory.
    if (const auto iterator = vpi_iterate(vpiReg, handle)) {
      if (const auto word = vpi_scan(iterator)) {
        const auto element = vpi_get(vpiSize, word);
        argument.width = element > 0 ? static_cast<uint32_t>(element) : 32u;
        (void)vpi_free_object(iterator);
      }
    }
    break;
  default: {
    // An expression is real when its value is (IEEE 1364-2001 25.61, Tables 187 and 202).
    s_vpi_value probe{vpiObjTypeVal, {}};
    vpi_get_value(handle, &probe);
    if (probe.format == vpiRealVal) {
      argument.kind = tf_readonlyreal;
      argument.width = 64u;
    } else {
      argument.kind = tf_readonly;
    }
    break;
  }
  }
  return argument;
}

// The msb and lsb a vector declares; its width less one and 0 where the VPI
// names no range.
void declared_bounds(const vpiHandle vector, fsim_tf_host_node_v3& node) {
  const auto size = vpi_get(vpiSize, vector);
  node.ms_index = size > 0 ? size - 1 : 0;
  node.ls_index = 0;
  const auto left = vpi_handle(vpiLeftRange, vector);
  const auto right = vpi_handle(vpiRightRange, vector);
  if (left != nullptr && right != nullptr) {
    s_vpi_value bound{vpiIntVal, {}};
    vpi_get_value(left, &bound);
    node.ms_index = bound.value.integer;
    bound = {vpiIntVal, {}};
    vpi_get_value(right, &bound);
    node.ls_index = bound.value.integer;
  }
}

// What tf_nodeinfo reports of an argument beyond the argument bridge: the
// node class (Table 192), the declared bounds of a vector and the size of a
// memory (IEEE 1364-2001 25.35); all zero for an argument that is no node.
fsim_tf_host_node_v3 node_facts(const vpiHandle handle,
                                const fsim_tf_argument_bridge_v3& argument) {
  fsim_tf_host_node_v3 node{0, 0, 0, 0};
  if (handle == nullptr || argument.kind == tf_nullparam ||
      argument.kind == tf_string || argument.kind == tf_readonlyreal ||
      argument.kind == tf_readwritereal) {
    return node;
  }
  auto type = vpi_get(vpiType, handle);
  auto vector = handle;
  if (type == vpiBitSelect || type == vpiNetBit || type == vpiRegBit ||
      type == vpiPartSelect) {
    // A select takes the class and bounds of the vector it selects from.
    const auto parent = vpi_handle(vpiParent, handle);
    if (parent == nullptr) {
      return node;
    }
    vector = parent;
    type = vpi_get(vpiType, parent);
    if (type == vpiNet) {
      node.node_type = vpi_get(vpiType, handle) == vpiPartSelect
          ? tf_netvector_node
          : tf_netscalar_node;
      declared_bounds(vector, node);
      return node;
    }
  }
  switch (type) {
  case vpiIntegerVar: node.node_type = tf_integer_node; break;
  case vpiTimeVar: node.node_type = tf_time_node; break;
  case vpiReg: case vpiMemoryWord: node.node_type = tf_reg_node; break;
  case vpiRegArray:
  case vpiMemory:
    node.node_type = tf_memory_node;
    node.mem_size = std::max(vpi_get(vpiSize, handle), 0);
    return node;
  case vpiNet:
    node.node_type = argument.width == 1u ? tf_netscalar_node
                                          : tf_netvector_node;
    break;
  default: return node;
  }
  declared_bounds(vector, node);
  return node;
}

// A function's result width; sizetf is called once per cell (21.1.1).
uint32_t function_width(const p_tfcell cell) {
  static std::unordered_map<p_tfcell, uint32_t> widths;
  if (const auto found = widths.find(cell); found != widths.end()) {
    return found->second;
  }
  PLI_INT32 width = 32;
  if (cell->type == userfunction && cell->sizetf != nullptr) {
    width = cell->sizetf(cell->data, reason_sizetf);
  }
  return widths.emplace(cell, width > 0 ? static_cast<uint32_t>(width) : 32u)
      .first->second;
}

Instance& instance_for(const vpiHandle call, const p_tfcell cell) {
  auto& table = instances();
  if (const auto found = table.find(call); found != table.end()) {
    return *found->second;
  }
  auto created = std::make_unique<Instance>();
  created->cell = cell;
  created->call = call;
  // The hierarchy identity is the call's VPI handle: acc_handle_tfinst
  // returns it as the task or function instance (IEEE 1364-2001 23.61).
  created->bridge = {FSIM_TF_INSTANCE_ABI_VERSION,
                     sizeof(fsim_tf_instance_bridge_v3), 1u,
                     static_cast<uint64_t>(
                         reinterpret_cast<std::uintptr_t>(call)),
                     1u, 0u};
  created->module = vpi_handle(vpiModule, call);
  created->module_name = full_name(created->module, "$root");
  created->scope_name = full_name(vpi_handle(vpiScope, call), "$root");
  created->routine_name =
      cell->tfname == nullptr || *cell->tfname == '\0' ? "$tf" : cell->tfname;
  if (const auto iterator = vpi_iterate(vpiArgument, call)) {
    while (const auto argument = vpi_scan(iterator)) {
      const bool omitted = is_null_argument(argument);
      created->handles.push_back(omitted ? nullptr : argument);
      auto classified = classify(argument);
      // The source text names the node for tf_exprinfo and tf_nodeinfo.
      if (!omitted) {
        auto text_of_argument = text_of(vpiDecompile, argument);
        if (text_of_argument.empty()) {
          text_of_argument = text_of(vpiName, argument);
        }
        auto& stored = created->expressions.emplace_back(text_of_argument);
        classified.expression = stored.data();
        classified.expression_size =
            static_cast<uint32_t>(stored.size() + 1u);
      }
      created->arguments.push_back(classified);
      created->nodes.push_back(node_facts(omitted ? nullptr : argument,
                                          created->arguments.back()));
    }
  }
  created->result_width = function_width(cell);
  return *table.emplace(call, std::move(created)).first->second;
}

vpiHandle vpi_object(const uint64_t object) {
  return reinterpret_cast<vpiHandle>(static_cast<std::uintptr_t>(object));
}

uint64_t acc_object(const vpiHandle object) {
  return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(object));
}

// An ACC direction constant of a vpiDirection value; 0 when there is none.
PLI_INT32 acc_direction(const PLI_INT32 direction) {
  switch (direction) {
  case vpiInput: return accInput;
  case vpiOutput: return accOutput;
  case vpiInout: return accInout;
  case vpiMixedIO: return accMixedIo;
  default: return 0;
  }
}

// The fulltype of a primitive (IEEE 1364-2001 Table 113).
PLI_INT32 primitive_full_type(const vpiHandle object) {
  switch (vpi_get(vpiPrimType, object)) {
  case vpiAndPrim: return accAndGate;
  case vpiNandPrim: return accNandGate;
  case vpiNorPrim: return accNorGate;
  case vpiOrPrim: return accOrGate;
  case vpiXorPrim: return accXorGate;
  case vpiXnorPrim: return accXnorGate;
  case vpiBufPrim: return accBufGate;
  case vpiNotPrim: return accNotGate;
  case vpiBufif0Prim: return accBufif0Gate;
  case vpiBufif1Prim: return accBufif1Gate;
  case vpiNotif0Prim: return accNotif0Gate;
  case vpiNotif1Prim: return accNotif1Gate;
  case vpiNmosPrim: return accNmosGate;
  case vpiPmosPrim: return accPmosGate;
  case vpiCmosPrim: return accCmosGate;
  case vpiRnmosPrim: return accRnmosGate;
  case vpiRpmosPrim: return accRpmosGate;
  case vpiRcmosPrim: return accRcmosGate;
  case vpiRtranPrim: return accRtranGate;
  case vpiRtranif0Prim: return accRtranif0Gate;
  case vpiRtranif1Prim: return accRtranif1Gate;
  case vpiTranPrim: return accTranGate;
  case vpiTranif0Prim: return accTranif0Gate;
  case vpiTranif1Prim: return accTranif1Gate;
  case vpiPullupPrim: return accPullupGate;
  case vpiPulldownPrim: return accPulldownGate;
  case vpiSeqPrim: return accSeqPrim;
  case vpiCombPrim: return accCombPrim;
  default: return 0;
  }
}

// The fulltype of a timing check; 0 for the 1364-2001 checks ACC predates.
PLI_INT32 timing_check_full_type(const vpiHandle object) {
  switch (vpi_get(vpiTchkType, object)) {
  case vpiSetup: return accSetup;
  case vpiHold: return accHold;
  case vpiPeriod: return accPeriod;
  case vpiWidth: return accWidth;
  case vpiSkew: return accSkew;
  case vpiRecovery: return accRecovery;
  case vpiNoChange: return accNochange;
  case vpiSetupHold: return accSetuphold;
  default: return 0;
  }
}

// The value type of a parameter or specparam (Table 113 fulltypes).
PLI_INT32 parameter_full_type(const vpiHandle object) {
  switch (vpi_get(vpiConstType, object)) {
  case vpiRealConst: return accRealParam;
  case vpiStringConst: return accStringParam;
  default: return accIntegerParam;
  }
}

PLI_INT32 port_full_type(const vpiHandle port) {
  if (const auto connection = vpi_handle(vpiHighConn, port)) {
    switch (vpi_get(vpiType, connection)) {
    case vpiPartSelect: return accPartSelectPort;
    case vpiBitSelect: return accBitSelectPort;
    case vpiOperation:
      if (vpi_get(vpiOpType, connection) == vpiConcatOp) {
        return accConcatPort;
      }
      break;
    default: break;
    }
  }
  return vpi_get(vpiSize, port) > 1 ? accVectorPort : accScalarPort;
}

bool statement_type(const PLI_INT32 type) {
  switch (type) {
  case vpiAssignStmt: case vpiAssignment: case vpiBegin: case vpiCase:
  case vpiDeassign: case vpiDelayControl: case vpiDisable:
  case vpiEventControl: case vpiFor: case vpiForce: case vpiForever:
  case vpiFork: case vpiIf: case vpiIfElse: case vpiNullStmt:
  case vpiRelease: case vpiRepeat: case vpiWait: case vpiWhile:
    return true;
  default: return false;
  }
}

// The ACC type and full type of a VPI object; 0 when ACC has no such object
// (IEEE 1364-2001 Table 113).
std::pair<PLI_INT32, PLI_INT32> acc_types(const vpiHandle object) {
  const auto same = [](const PLI_INT32 type) { return std::pair{type, type}; };
  const auto vpi_type = vpi_get(vpiType, object);
  switch (vpi_type) {
  case vpiModule:
    return {accModule,
            vpi_get(vpiTopModule, object) == 1        ? accTopModule
            : vpi_get(vpiCellInstance, object) == 1 ? accCellInstance
                                                      : accModuleInstance};
  case vpiReg: return same(accReg);
  case vpiIntegerVar: return same(accIntegerVar);
  case vpiTimeVar: return same(accTimeVar);
  case vpiRealVar: return same(accRealVar);
  case vpiNamedEvent: return same(accNamedEvent);
  case vpiNet:
    switch (vpi_get(vpiNetType, object)) {
    case vpiWand: return {accNet, accWand};
    case vpiWor: return {accNet, accWor};
    case vpiTri: return {accNet, accTri};
    case vpiTriAnd: return {accNet, accTriand};
    case vpiTriOr: return {accNet, accTrior};
    case vpiTri0: return {accNet, accTri0};
    case vpiTri1: return {accNet, accTri1};
    case vpiTriReg: return {accNet, accTrireg};
    case vpiSupply0: return {accNet, accSupply0};
    case vpiSupply1: return {accNet, accSupply1};
    default: return {accNet, accWire};
    }
  case vpiNetBit: return same(accNetBit);
  case vpiRegBit: return same(accRegBit);
  case vpiBitSelect: {
    const auto parent = vpi_handle(vpiParent, object);
    return parent != nullptr && vpi_get(vpiType, parent) == vpiNet
               ? same(accNetBit)
               : same(accRegBit);
  }
  case vpiPartSelect: return same(accPartSelect);
  case vpiParameter: return {accParameter, parameter_full_type(object)};
  // A specparam is not a parameter (Table 113).
  case vpiSpecParam: return {accSpecparam, parameter_full_type(object)};
  case vpiPort: return {accPort, port_full_type(object)};
  case vpiPortBit: return same(accPortBit);
  case vpiPrimTerm:
    switch (vpi_get(vpiDirection, object)) {
    case vpiInput: return {accTerminal, accInputTerminal};
    case vpiOutput: return {accTerminal, accOutputTerminal};
    case vpiInout: return {accTerminal, accInoutTerminal};
    default: return {0, 0};
    }
  case vpiGate:
  case vpiSwitch:
  case vpiUdp: {
    const auto full = primitive_full_type(object);
    return full == 0 ? std::pair<PLI_INT32, PLI_INT32>{0, 0}
                     : std::pair{accPrimitive, full};
  }
  case vpiModPath: return same(accModPath);
  case vpiInterModPath: return {accWirePath, accInterModPath};
  case vpiPathTerm:
    return {accPathTerminal, vpi_get(vpiDirection, object) == vpiInput
                                 ? accPathInput
                                 : accPathOutput};
  case vpiTchk: {
    const auto full = timing_check_full_type(object);
    return full == 0 ? std::pair<PLI_INT32, PLI_INT32>{0, 0}
                     : std::pair{accTchk, full};
  }
  case vpiTchkTerm: return same(accTchkTerminal);
  case vpiTask: return same(accTask);
  case vpiFunction: return same(accFunction);
  case vpiNamedBegin: return {accStatement, accNamedBeginStat};
  case vpiNamedFork: return {accStatement, accNamedForkStat};
  case vpiTaskCall: return same(accTaskCall);
  case vpiFuncCall: return same(accFunctionCall);
  // A call object is a vpiSysTaskCall or vpiSysFuncCall (IEEE 1364-2001
  // 26.6.19); vpiSysTfCall names their class.
  case vpiSysTaskCall:
  case vpiSysFuncCall:
  case vpiSysTfCall: {
    const bool user = vpi_get(vpiUserDefn, object) == 1;
    const auto function_type = vpi_type == vpiSysTaskCall
                                   ? 0
                                   : vpi_get(vpiFuncType, object);
    if (function_type == vpiRealFunc) {
      return same(user ? accUserRealFunction : accSystemRealFunction);
    }
    if (function_type > 0) {
      return same(user ? accUserFunction : accSystemFunction);
    }
    return same(user ? accUserTask : accSystemTask);
  }
  case vpiConstant: return same(accConstant);
  case vpiOperation:
    return same(vpi_get(vpiOpType, object) == vpiConcatOp ? accConcat
                                                           : accOperator);
  default:
    return statement_type(vpi_type) ? std::pair{accStatement, accStatement}
                                    : std::pair<PLI_INT32, PLI_INT32>{0, 0};
  }
}

// A scope for the ACC routines (IEEE 1364-2001 23.54).
bool acc_scope(const vpiHandle object) {
  switch (vpi_get(vpiType, object)) {
  case vpiModule:
  case vpiTask:
  case vpiFunction:
  case vpiNamedBegin:
  case vpiNamedFork: return true;
  default: return false;
  }
}

// The nearest enclosing object that is `accepted`, walking vpiScope past
// generate scopes and other levels ACC does not name.
template <typename Accept>
vpiHandle enclosing(const vpiHandle object, const Accept accepted) {
  auto current = vpi_handle(vpiScope, object);
  for (int depth = 0; current != nullptr && depth < 256; ++depth) {
    if (accepted(current)) {
      return current;
    }
    current = vpi_handle(vpiScope, current);
  }
  return nullptr;
}

// acc_handle_parent (IEEE 1364-2001 23.49): the primitive of a terminal, the
// port of a port bit, else the module instance; none for a top module.
vpiHandle acc_parent(const vpiHandle object) {
  const auto type = vpi_get(vpiType, object);
  if (type == vpiPrimTerm || type == vpiPortBit) {
    return vpi_handle(vpiParent, object);
  }
  if (type == vpiModule && vpi_get(vpiTopModule, object) == 1) {
    return nullptr;
  }
  return enclosing(object, [](const vpiHandle candidate) {
    return vpi_get(vpiType, candidate) == vpiModule;
  });
}

// The members of one VPI relation, in iteration order.
std::vector<vpiHandle> members(const PLI_INT32 relation,
                               const vpiHandle reference) {
  std::vector<vpiHandle> found;
  if (const auto iterator = vpi_iterate(relation, reference)) {
    while (const auto member = vpi_scan(iterator)) {
      found.push_back(member);
    }
  }
  return found;
}

vpiHandle first_member(const PLI_INT32 relation, const vpiHandle reference) {
  const auto iterator = vpi_iterate(relation, reference);
  if (iterator == nullptr) {
    return nullptr;
  }
  const auto first = vpi_scan(iterator);
  if (first != nullptr) {
    (void)vpi_free_object(iterator);
  }
  return first;
}

// The member `index` places after the first of a one-to-many relation.
vpiHandle nth_member(const PLI_INT32 relation, const vpiHandle reference,
                     const PLI_INT32 index) {
  if (index < 0) {
    return nullptr;
  }
  const auto iterator = vpi_iterate(relation, reference);
  if (iterator == nullptr) {
    return nullptr;
  }
  vpiHandle member = nullptr;
  for (PLI_INT32 position = 0; (member = vpi_scan(iterator)) != nullptr;
       ++position) {
    if (position == index) {
      (void)vpi_free_object(iterator);
      break;
    }
  }
  return member;
}

// The name of the net or expression a path or timing-check terminal names.
std::string terminal_name(const vpiHandle terminal) {
  return text_of(vpiName, terminal == nullptr ? nullptr
                                              : vpi_handle(vpiExpr, terminal));
}

// A module path is named source, accPathDelimStr, destination
// (IEEE 1364-2001 Table 133).
std::string path_name(const vpiHandle path) {
  return terminal_name(first_member(vpiModPathIn, path)) +
         fsim_acc_path_delimiter_v3() +
         terminal_name(first_member(vpiModPathOut, path));
}

std::string acc_local_name(const vpiHandle object) {
  const auto type = vpi_get(vpiType, object);
  return type == vpiModPath || type == vpiInterModPath
             ? path_name(object)
             : text_of(vpiName, object);
}

std::string acc_full_name(const vpiHandle object) {
  const auto type = vpi_get(vpiType, object);
  if (type == vpiModPath || type == vpiInterModPath) {
    const auto owner = text_of(vpiFullName, acc_parent(object));
    return owner.empty() ? path_name(object) : owner + "." + path_name(object);
  }
  // A module port is named in its module (Table 141); its VPI object has no
  // full name of its own.
  if (type == vpiPort) {
    const auto owner = text_of(vpiFullName, vpi_handle(vpiModule, object));
    const auto name = text_of(vpiName, object);
    return owner.empty() || name.empty() ? std::string{} : owner + "." + name;
  }
  return text_of(vpiFullName, object);
}

// Storage the last read result points into, valid until the next read.
struct AccReadStorage {
  std::vector<fsim_acc_logic_word_v3> words;
  std::array<std::string, 11> text;
};

AccReadStorage& acc_read_storage() {
  static AccReadStorage storage;
  return storage;
}

// The two-letter strength of a scalar, as %v prints it (IEEE 1364-2001 17.1).
const char* strength_name(const PLI_INT32 strength) {
  switch (strength) {
  case vpiSupplyDrive: return "Su";
  case vpiStrongDrive: return "St";
  case vpiPullDrive: return "Pu";
  case vpiLargeCharge: return "La";
  case vpiWeakDrive: return "We";
  case vpiMediumCharge: return "Me";
  case vpiSmallCharge: return "Sm";
  default: return "Hi";
  }
}

// A scalar's value with its strength, such as St1 or HiZ.
std::string strength_text(const vpiHandle object) {
  s_vpi_value value{vpiStrengthVal, {}};
  vpi_get_value(object, &value);
  if (value.format != vpiStrengthVal || value.value.strength == nullptr) {
    return {};
  }
  const auto& scalar = *value.value.strength;
  switch (scalar.logic) {
  case vpi0: return std::string{strength_name(scalar.s0)} + "0";
  case vpi1: return std::string{strength_name(scalar.s1)} + "1";
  case vpiZ: return "HiZ";
  default:
    return std::string{strength_name(std::max(scalar.s0, scalar.s1))} + "X";
  }
}

void read_text(const vpiHandle object, const PLI_INT32 format,
               std::string& text, const PLI_BYTE8*& pointer,
               uint32_t& size) {
  s_vpi_value value{format, {}};
  vpi_get_value(object, &value);
  text = value.format == format && value.value.str != nullptr
             ? value.value.str
             : "";
  pointer = text.c_str();
  size = static_cast<uint32_t>(text.size());
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_resolve(void* /*user_data*/,
                                             const uint64_t object,
                                             PLI_INT32* const type) {
  if (object == 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto full_type = acc_types(vpi_object(object)).second;
  if (full_type == 0) {
    return FSIM_ACC_VPI_OBJECT_WRONG_TYPE;
  }
  *type = full_type;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

// Name lookup by the hierarchical-name rules (IEEE 1364-2001 12.4): the name
// is tried in the scope, then in each enclosing scope, then from the tops.
vpiHandle find_by_upward_name(const std::string& path, vpiHandle scope) {
  auto* const name = const_cast<PLI_BYTE8*>(path.c_str());
  for (int depth = 0; scope != nullptr && depth < 256; ++depth) {
    if (const auto found = vpi_handle_by_name(name, scope)) {
      return found;
    }
    scope = vpi_handle(vpiScope, scope);
  }
  return vpi_handle_by_name(name, nullptr);
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_lookup(
    void* /*user_data*/, const uint32_t mode, const uint64_t scope,
    const PLI_BYTE8* const name, const uint32_t name_size,
    uint64_t* const object) {
  const std::string path{name, name_size};
  vpiHandle found{};
  switch (mode) {
  case FSIM_ACC_LOOKUP_ABSOLUTE:
    found = vpi_handle_by_name(const_cast<PLI_BYTE8*>(path.c_str()), nullptr);
    break;
  case FSIM_ACC_LOOKUP_RELATIVE:
    found = vpi_handle_by_name(const_cast<PLI_BYTE8*>(path.c_str()),
                               vpi_object(scope));
    break;
  case FSIM_ACC_LOOKUP_PLI_SCOPE:
  case FSIM_ACC_LOOKUP_CALLING_SCOPE:
    found = find_by_upward_name(path, vpi_object(scope));
    break;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (found == nullptr) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  *object = acc_object(found);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_relation(void* /*user_data*/,
                                              const uint32_t relation,
                                              const uint64_t object,
                                              uint64_t* const related) {
  vpiHandle found{};
  switch (relation) {
  case FSIM_ACC_RELATION_PARENT:
    found = acc_parent(vpi_object(object));
    break;
  case FSIM_ACC_RELATION_SCOPE:
    // A top module has no containing scope (IEEE 1364-2001 23.54).
    found = enclosing(vpi_object(object), &acc_scope);
    break;
  case FSIM_ACC_RELATION_SIMULATED_NET:
    // Nets are never collapsed, so a net is its own simulated net (23.55).
    found = vpi_object(object);
    break;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (found == nullptr) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  *related = acc_object(found);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

// The full hierarchical name, for acc_set_scope's previous-scope result.
uint32_t FSIM_NATIVE_PLUGIN_CALL acc_name(void* /*user_data*/,
                                          const uint64_t object,
                                          PLI_BYTE8* const buffer,
                                          const uint32_t capacity,
                                          uint32_t* const size) {
  const auto name = acc_full_name(vpi_object(object));
  if (name.empty() || name.size() >= capacity) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  std::memcpy(buffer, name.c_str(), name.size() + 1u);
  *size = static_cast<uint32_t>(name.size());
  return FSIM_ACC_VPI_OBJECT_VALID;
}

// The members one acc_next_* traversal visits, gathered when it begins.
struct Cursor {
  std::vector<vpiHandle> items;
  std::size_t next{};
};

// Keeps the members ACC has a type for.
void keep_acc_objects(std::vector<vpiHandle>& items) {
  std::erase_if(items, [](const vpiHandle item) {
    return acc_types(item).second == 0;
  });
}

// The cell instances in a module and below, not those inside other cells
// (IEEE 1364-2001 23.65).
void collect_cells(const vpiHandle scope, std::vector<vpiHandle>& cells) {
  for (const auto child : members(vpiModule, scope)) {
    if (vpi_get(vpiCellInstance, child) == 1) {
      cells.push_back(child);
    } else {
      collect_cells(child, cells);
    }
  }
}

std::vector<vpiHandle> traversal_members(const uint32_t family,
                                         const vpiHandle scope) {
  std::vector<vpiHandle> items;
  switch (family) {
  case FSIM_ACC_TRAVERSE_CELL: collect_cells(scope, items); break;
  // A null reference scans the top modules (Table 172).
  case FSIM_ACC_TRAVERSE_CHILD: items = members(vpiModule, scope); break;
  case FSIM_ACC_TRAVERSE_TOP_MODULE:
    items = members(vpiModule, nullptr);
    break;
  case FSIM_ACC_TRAVERSE_NET: items = members(vpiNet, scope); break;
  case FSIM_ACC_TRAVERSE_PARAMETER:
    items = members(vpiParameter, scope);
    break;
  case FSIM_ACC_TRAVERSE_SPECPARAM:
    items = members(vpiSpecParam, scope);
    break;
  // The ports of a module, or those connected to a net or variable
  // (Table 173).
  case FSIM_ACC_TRAVERSE_PORT:
    items = members(vpi_get(vpiType, scope) == vpiModule ? vpiPort
                                                          : vpiPortInst,
                    scope);
    break;
  case FSIM_ACC_TRAVERSE_PRIMITIVE:
    items = members(vpiPrimitive, scope);
    break;
  // The immediate scopes of a scope: its blocks, tasks, functions and
  // module instances (23.80).
  case FSIM_ACC_TRAVERSE_SCOPE:
    items = members(vpiInternalScope, scope);
    std::erase_if(items, [](const vpiHandle item) {
      return !acc_scope(item);
    });
    for (const auto child : members(vpiModule, scope)) {
      items.push_back(child);
    }
    break;
  default: break;
  }
  keep_acc_objects(items);
  return items;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_traverse(
    void* /*user_data*/, const uint32_t operation, const uint32_t family,
    const uint64_t scope, uint64_t* const cursor, uint64_t* const object) {
  switch (operation) {
  case FSIM_ACC_TRAVERSE_BEGIN: {
    auto* const created = new Cursor{};
    created->items = traversal_members(family, vpi_object(scope));
    *cursor = static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(created));
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  case FSIM_ACC_TRAVERSE_NEXT: {
    auto* const active =
        reinterpret_cast<Cursor*>(static_cast<std::uintptr_t>(*cursor));
    if (active->next >= active->items.size()) {
      return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    }
    *object = acc_object(active->items[active->next++]);
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  case FSIM_ACC_TRAVERSE_END:
    delete reinterpret_cast<Cursor*>(static_cast<std::uintptr_t>(*cursor));
    *cursor = 0;
    return FSIM_ACC_VPI_OBJECT_VALID;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
}

// An object_query result is one VPI handle or none.
uint32_t object_result(const vpiHandle result, uint64_t* const handle) {
  if (result == nullptr) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  *handle = acc_object(result);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

// The terminal of a timing check that acc_handle_tchkarg1 or 2 names: the
// arguments are in source order, and $setup puts the data event first
// (IEEE 1364-2001 15.3).
vpiHandle timing_check_argument(const vpiHandle check, const bool first) {
  const bool data_first = vpi_get(vpiTchkType, check) == vpiSetup;
  const bool data = first == data_first;
  return vpi_handle(data ? vpiTchkDataTerm : vpiTchkRefTerm, check);
}

// A path or check terminal matches an acc_handle_modpath or acc_handle_tchk
// argument given by name, or by the net's handle, with an edge when given.
bool terminal_matches(const vpiHandle terminal, const PLI_BYTE8* const name,
                      const uint32_t name_size, const uint64_t net) {
  if (terminal == nullptr) {
    return name == nullptr && net == 0;
  }
  const auto connected = vpi_handle(vpiExpr, terminal);
  if (name != nullptr) {
    const std::string wanted{name, name_size};
    return terminal_name(terminal) == wanted ||
           text_of(vpiFullName, connected) == wanted;
  }
  return net == 0 || (connected != nullptr &&
                      vpi_compare_objects(connected, vpi_object(net)) != 0);
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_object_query(
    void* /*user_data*/, const fsim_acc_object_query_v3* const query,
    uint64_t* const handle) {
  const auto owner = vpi_object(query->owner);
  const auto first = vpi_object(query->first);
  switch (query->operation) {
  case FSIM_ACC_OBJECT_CONDITION:
    return object_result(vpi_handle(vpiCondition, first), handle);
  case FSIM_ACC_OBJECT_CONNECTION:
    return object_result(vpi_handle(vpiExpr, first), handle);
  case FSIM_ACC_OBJECT_HICONN:
    return object_result(vpi_handle(vpiHighConn, first), handle);
  case FSIM_ACC_OBJECT_LOCONN:
    return object_result(vpi_handle(vpiLowConn, first), handle);
  case FSIM_ACC_OBJECT_NOTIFIER:
    return object_result(vpi_handle(vpiTchkNotifier, first), handle);
  case FSIM_ACC_OBJECT_PATH_INPUT:
    return object_result(vpi_handle(vpiExpr, first_member(vpiModPathIn, first)),
                 handle);
  case FSIM_ACC_OBJECT_PATH_OUTPUT:
    return object_result(vpi_handle(vpiExpr, first_member(vpiModPathOut, first)),
                 handle);
  case FSIM_ACC_OBJECT_PORT_INDEX:
    return object_result(nth_member(vpiPort, owner, query->index), handle);
  case FSIM_ACC_OBJECT_TERMINAL_INDEX:
    return object_result(nth_member(vpiPrimTerm, owner, query->index),
                 handle);
  case FSIM_ACC_OBJECT_TIMING_CHECK_ARG1:
    return object_result(timing_check_argument(first, true), handle);
  case FSIM_ACC_OBJECT_TIMING_CHECK_ARG2:
    return object_result(timing_check_argument(first, false), handle);
  case FSIM_ACC_OBJECT_MODULE_PATH:
    for (const auto path : members(vpiModPath, owner)) {
      if (terminal_matches(first_member(vpiModPathIn, path), query->first_name,
                           query->first_name_size, query->first) &&
          terminal_matches(first_member(vpiModPathOut, path),
                           query->second_name, query->second_name_size,
                           query->second)) {
        return object_result(path, handle);
      }
    }
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  case FSIM_ACC_OBJECT_TIMING_CHECK:
    for (const auto check : members(vpiTchk, owner)) {
      const auto first_terminal = timing_check_argument(check, true);
      const auto second_terminal = timing_check_argument(check, false);
      const bool edge_first = first_terminal == nullptr ||
                              vpi_get(vpiEdge, first_terminal) ==
                                  query->first_edge;
      const bool edge_second = second_terminal == nullptr ||
                               vpi_get(vpiEdge, second_terminal) ==
                                   query->second_edge;
      if (timing_check_full_type(check) == query->object_type &&
          terminal_matches(first_terminal, query->first_name,
                           query->first_name_size, query->first) &&
          terminal_matches(second_terminal, query->second_name,
                           query->second_name_size, query->second) &&
          edge_first && edge_second) {
        return object_result(check, handle);
      }
    }
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  // An intermodule path joins an output port to an input port (23.55).
  case FSIM_ACC_OBJECT_INTERMODULE_PATH:
    return object_result(
        vpi_handle_multi(vpiInterModPath, first, vpi_object(query->second)),
        handle);
  // A data path has no VPI object to start from.
  case FSIM_ACC_OBJECT_DATAPATH:
  default: return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
}

// Whether the object has a value acc_fetch_value reads.
bool has_value(const PLI_INT32 type) {
  switch (type) {
  case accNet: case accReg: case accIntegerVar: case accRealVar:
  case accTimeVar: case accNetBit: case accRegBit: case accPortBit:
  case accBitSelect: case accPartSelect: case accParameter:
  case accSpecparam: case accConstant: case accTerminal:
    return true;
  default: return false;
  }
}

bool writes_values(const PLI_INT32 type) {
  switch (type) {
  case accNet: case accReg: case accIntegerVar: case accRealVar:
  case accTimeVar: case accNetBit: case accRegBit: case accBitSelect:
  case accPartSelect: case accPrimitive:
    return true;
  default: return false;
  }
}

bool variable_type(const PLI_INT32 type) {
  return type == accReg || type == accIntegerVar || type == accRealVar ||
         type == accTimeVar || type == accRegBit;
}

// An object's attribute by name (IEEE 1364-2001 23.10-23.12): a parameter or
// specparam of the module the object is in, named as the attribute string
// followed by the object's name (a specific attribute), else as the attribute
// string alone (a general attribute). A module is also searched for its own.
// The value is returned as each ACC routine reads it, or NOT_FOUND.
uint32_t read_attribute(const vpiHandle object, const PLI_BYTE8* const name,
                        const uint32_t name_size,
                        fsim_acc_read_result_v3* const result) {
  const std::string wanted{name, name_size};
  const auto object_name = acc_local_name(object);
  std::vector<vpiHandle> scopes;
  if (const auto parent = acc_parent(object)) {
    scopes.push_back(parent);
  }
  if (vpi_get(vpiType, object) == vpiModule) {
    scopes.push_back(object);
  }
  for (const auto scope : scopes) {
    auto declared = members(vpiParameter, scope);
    for (const auto specparam : members(vpiSpecParam, scope)) {
      declared.push_back(specparam);
    }
    for (const auto& candidate :
         {object_name.empty() ? std::string{} : wanted + object_name,
          wanted}) {
      if (candidate.empty()) {
        continue;
      }
      for (const auto attribute : declared) {
        if (text_of(vpiName, attribute) != candidate) {
          continue;
        }
        auto& storage = acc_read_storage();
        s_vpi_value as_integer{vpiIntVal, {}};
        vpi_get_value(attribute, &as_integer);
        s_vpi_value as_real{vpiRealVal, {}};
        vpi_get_value(attribute, &as_real);
        s_vpi_value as_string{vpiStringVal, {}};
        vpi_get_value(attribute, &as_string);
        result->attribute_integer = as_integer.value.integer;
        result->attribute_real = as_real.format == vpiRealVal
                                     ? as_real.value.real
                                     : static_cast<double>(
                                           as_integer.value.integer);
        storage.text[10] = as_string.format == vpiStringVal &&
                                   as_string.value.str != nullptr
                               ? as_string.value.str
                               : "";
        result->attribute_string = storage.text[10].c_str();
        result->attribute_string_size =
            static_cast<uint32_t>(storage.text[10].size());
        return FSIM_ACC_VPI_OBJECT_VALID;
      }
    }
  }
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

// The design's time precision and unit exponents (acc_fetch_precision,
// acc_fetch_timescale_info with a null object).
uint32_t read_design(fsim_acc_read_result_v3* const result) {
  const auto precision = std::clamp(vpi_get(vpiTimePrecision, nullptr), -15, 2);
  result->design_precision = precision;
  result->timescale_precision = static_cast<PLI_INT16>(precision);
  result->timescale_unit = static_cast<PLI_INT16>(
      std::clamp(vpi_get(vpiTimeUnit, nullptr), -15, 2));
  return FSIM_ACC_VPI_OBJECT_VALID;
}

bool three_state_primitive(vpiHandle object);

// An object's type, range, value and names (FSIM_ACC_READ_OBJECT).
uint32_t read_object(const vpiHandle object,
                     fsim_acc_read_result_v3* const result) {
  const auto [type, full_type] = acc_types(object);
  if (full_type == 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  auto& storage = acc_read_storage();
  result->type = type;
  result->full_type = full_type;
  result->index = -1;
  result->width = 1;
  storage.text[0] = acc_local_name(object);
  result->name = storage.text[0].c_str();
  result->name_size = static_cast<uint32_t>(storage.text[0].size());
  storage.text[1] = acc_full_name(object);
  result->full_name = storage.text[1].c_str();
  result->full_name_size = static_cast<uint32_t>(storage.text[1].size());
  storage.text[7] = text_of(vpiDefName, object);
  result->definition_name = storage.text[7].c_str();
  result->definition_name_size = static_cast<uint32_t>(storage.text[7].size());
  storage.text[8] = text_of(vpiFile, object);
  result->source_file = storage.text[8].c_str();
  result->source_file_size = static_cast<uint32_t>(storage.text[8].size());
  result->source_line = std::max(vpi_get(vpiLineNo, object), 0);

  // Direction, index, edge and time scale (acc_fetch_direction, _index,
  // _edge and _timescale_info).
  if (type == accPort || type == accPortBit || type == accTerminal) {
    result->direction = acc_direction(vpi_get(vpiDirection, object));
  }
  if (type == accPort) {
    result->index = std::max(vpi_get(vpiPortIndex, object), 0);
  } else if (type == accTerminal) {
    result->index = std::max(vpi_get(vpiTermIndex, object), 0);
  }
  if (type == accPathTerminal || type == accTchkTerminal) {
    result->edge = std::max(vpi_get(vpiEdge, object), 0);
  }
  const auto unit = vpi_get(vpiTimeUnit, object);
  const auto precision = vpi_get(vpiTimePrecision, object);
  if (unit >= -15 && unit <= 2 && precision >= -15 && precision <= 2) {
    result->timescale_unit = static_cast<PLI_INT16>(unit);
    result->timescale_precision = static_cast<PLI_INT16>(precision);
  }
  // The delay, pulse and polarity services (IEEE 1364-2001 22.8, 23.15,
  // 23.27). A 3-state primitive has three delays, other primitives two, a
  // path up to twelve and a timing check one limit; $setuphold and
  // $nochange have two limits, which 22.8.1 gives no routine for.
  if (type == accModPath) {
    result->timing_capabilities = FSIM_ACC_TIMING_CAP_DELAYS |
                                  FSIM_ACC_TIMING_CAP_PULSE |
                                  FSIM_ACC_TIMING_CAP_POLARITY;
    result->timing_delay_count = 12;
  } else if (type == accWirePath) {
    result->timing_capabilities = FSIM_ACC_TIMING_CAP_POLARITY;
  } else if (type == accPrimitive && full_type != accPullupGate &&
             full_type != accPulldownGate) {
    result->timing_capabilities = FSIM_ACC_TIMING_CAP_DELAYS;
    result->timing_delay_count = three_state_primitive(object) ? 3 : 2;
  } else if (type == accTchk && full_type != accSetuphold &&
             full_type != accNochange) {
    result->timing_capabilities = FSIM_ACC_TIMING_CAP_DELAYS;
    result->timing_delay_count = 1;
  }
  if (type == accParameter || type == accSpecparam) {
    result->parameter_type = full_type;
  }
  if (!has_value(type) && type != accPort) {
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  const auto size = vpi_get(vpiSize, object);
  result->width = size > 0 ? size : 1;
  if (const auto left = vpi_handle(vpiLeftRange, object)) {
    if (const auto right = vpi_handle(vpiRightRange, object)) {
      s_vpi_value bound{vpiIntVal, {}};
      vpi_get_value(left, &bound);
      result->msb = bound.value.integer;
      bound = {vpiIntVal, {}};
      vpi_get_value(right, &bound);
      result->lsb = bound.value.integer;
      result->has_range = 1;
    }
  }
  if (writes_values(type)) {
    result->write_capabilities = FSIM_ACC_WRITE_CAP_DEPOSIT |
                                 FSIM_ACC_WRITE_CAP_FORCE |
                                 FSIM_ACC_WRITE_CAP_RELEASE;
    // Procedural assign and deassign act on registers and variables only
    // (IEEE 1364-2001 23.96).
    if (variable_type(type)) {
      result->write_capabilities |=
          FSIM_ACC_WRITE_CAP_ASSIGN | FSIM_ACC_WRITE_CAP_DEASSIGN;
    }
  }
  if (type == accPort || type == accTerminal) {
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (full_type == accRealVar || full_type == accRealParam) {
    s_vpi_value value{vpiRealVal, {}};
    vpi_get_value(object, &value);
    result->value_kind = FSIM_ACC_READ_VALUE_REAL;
    result->real_value = value.value.real;
    result->parameter_value = value.value.real;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  const auto words = static_cast<uint32_t>((result->width + 31) / 32);
  s_vpi_value value{vpiVectorVal, {}};
  vpi_get_value(object, &value);
  storage.words.assign(words, fsim_acc_logic_word_v3{});
  for (uint32_t word = 0; word < words && value.value.vector != nullptr;
       ++word) {
    storage.words[word] = {value.value.vector[word].aval,
                           value.value.vector[word].bval};
  }
  result->value_kind = FSIM_ACC_READ_VALUE_LOGIC4;
  result->logic_word_count = words;
  result->logic_words = storage.words.data();
  result->parameter_value = static_cast<double>(storage.words[0].aval);
  read_text(object, vpiBinStrVal, storage.text[2], result->binary_value,
            result->binary_value_size);
  read_text(object, vpiOctStrVal, storage.text[3], result->octal_value,
            result->octal_value_size);
  read_text(object, vpiDecStrVal, storage.text[4], result->decimal_value,
            result->decimal_value_size);
  read_text(object, vpiHexStrVal, storage.text[5], result->hexadecimal_value,
            result->hexadecimal_value_size);
  read_text(object, vpiStringVal, storage.text[6], result->string_value,
            result->string_value_size);
  if (result->width == 1 && (type == accNet || type == accNetBit)) {
    storage.text[9] = strength_text(object);
    result->strength_value = storage.text[9].c_str();
    result->strength_value_size = static_cast<uint32_t>(storage.text[9].size());
  }
  return FSIM_ACC_VPI_OBJECT_VALID;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_read(
    void* /*user_data*/, const fsim_acc_read_query_v3* const query,
    fsim_acc_read_result_v3* const result) {
  switch (query->operation) {
  case FSIM_ACC_READ_OBJECT:
    return read_object(vpi_object(query->object), result);
  case FSIM_ACC_READ_ATTRIBUTE:
    return read_attribute(vpi_object(query->object), query->attribute_name,
                          query->attribute_name_size, result);
  case FSIM_ACC_READ_DESIGN: return read_design(result);
  default: return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
}

// acc_set_value: a deposit, delayed assignment, force or release, and
// procedural assign or deassign.
uint32_t FSIM_NATIVE_PLUGIN_CALL acc_write(
    void* user_data, const fsim_acc_write_query_v3* const query,
    fsim_acc_write_result_v3* const result) {
  if (query->operation != FSIM_ACC_WRITE_SET_VALUE) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto object = vpi_object(query->object);
  PLI_INT32 flags{};
  switch (query->model) {
  case accNoDelay: flags = vpiNoDelay; break;
  case accInertialDelay: flags = vpiInertialDelay; break;
  case accTransportDelay: flags = vpiTransportDelay; break;
  case accPureTransportDelay: flags = vpiPureTransportDelay; break;
  case accForceFlag: flags = vpiForceFlag; break;
  case accReleaseFlag: flags = vpiReleaseFlag; break;
  // VPI has only force and release: procedural assign and deassign take
  // their overriding-until-released effect through them (23.96).
  case accAssignFlag: flags = vpiForceFlag; break;
  case accDeassignFlag: flags = vpiReleaseFlag; break;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  // accTime and accRealTime count the object's time units, accSimTime ticks.
  s_vpi_time time{vpiSimTime, 0, 0, 0.0};
  if (query->time_type == accSimTime) {
    time.high = static_cast<PLI_UINT32>(query->time_ticks >> 32u);
    time.low = static_cast<PLI_UINT32>(query->time_ticks);
  } else if (query->time_type == accTime) {
    time.type = vpiScaledRealTime;
    time.real = static_cast<double>(query->time_ticks);
  } else if (query->time_type == accRealTime) {
    time.type = vpiScaledRealTime;
    time.real = query->time_real;
  }
  s_vpi_value value{vpiVectorVal, {}};
  std::vector<s_vpi_vecval> words;
  std::string text;
  const auto& input = query->value;
  const bool release = flags == vpiReleaseFlag;
  switch (release ? accVectorVal : input.format) {
  case accScalarVal:
  case accIntVal:
  case accVectorVal:
    words.resize(std::max<std::size_t>(
        input.logic_word_count,
        static_cast<std::size_t>((std::max(input.width, 1) + 31) / 32)));
    for (uint32_t word = 0; word < input.logic_word_count; ++word) {
      words[word] = {input.logic_words[word].aval,
                     input.logic_words[word].bval};
    }
    value.value.vector = words.data();
    break;
  case accRealVal:
    value.format = vpiRealVal;
    value.value.real = input.real_value;
    break;
  case accStringVal:
  case accBinStrVal:
  case accOctStrVal:
  case accDecStrVal:
  case accHexStrVal:
    value.format = input.format == accStringVal   ? vpiStringVal
                   : input.format == accBinStrVal ? vpiBinStrVal
                   : input.format == accOctStrVal ? vpiOctStrVal
                   : input.format == accDecStrVal ? vpiDecStrVal
                                                  : vpiHexStrVal;
    text.assign(input.string_value, input.string_value_size);
    value.value.str = text.data();
    break;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const bool delayed = flags == vpiInertialDelay ||
                       flags == vpiTransportDelay ||
                       flags == vpiPureTransportDelay;
  (void)vpi_put_value(object, &value, delayed ? &time : nullptr, flags);
  fsim_acc_read_query_v3 read{};
  read.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  read.struct_size = sizeof(read);
  read.operation = FSIM_ACC_READ_OBJECT;
  read.object = query->object;
  return acc_read(user_data, &read, &result->current_value);
}

// The members acc_next and the relation routines visit, gathered when the
// iteration begins (the Cursor of the acc_next_* traversals).
bool bit_object(const vpiHandle object) {
  const auto [type, full_type] = acc_types(object);
  (void)full_type;
  return type == accNetBit || type == accRegBit || type == accPortBit ||
         type == accBitSelect;
}

// What acc_next_driver, _load and _cell_load return (IEEE 1364-2001
// 23.68, 23.71): terminals, ports, primitives, nets and registers.
bool connection_object(const vpiHandle object) {
  const auto type = acc_types(object).first;
  return type == accTerminal || type == accPort || type == accPortBit ||
         type == accPrimitive || type == accNet || type == accNetBit ||
         type == accReg || type == accRegBit || type == accIntegerVar ||
         type == accRealVar || type == accTimeVar;
}

std::vector<vpiHandle> iteration_members(const fsim_acc_iterator_query_v3& query) {
  const auto owner = vpi_object(query.owner);
  std::vector<vpiHandle> items;
  switch (query.family) {
  // Every object kind Table 171 lists; the requested types filter them.
  case FSIM_ACC_ITERATOR_GENERIC:
    for (const auto relation :
         {vpiNet, vpiReg, vpiIntegerVar, vpiRealVar, vpiTimeVar,
          vpiNamedEvent, vpiParameter, vpiPrimitive, vpiModule}) {
      for (const auto member : members(relation, owner)) {
        items.push_back(member);
      }
    }
    break;
  case FSIM_ACC_ITERATOR_BIT:
    items = members(vpiBit, owner);
    std::erase_if(items, [](const vpiHandle item) {
      return !bit_object(item);
    });
    break;
  case FSIM_ACC_ITERATOR_DRIVER:
    items = members(vpiDriver, owner);
    std::erase_if(items, [](const vpiHandle item) {
      return !connection_object(item);
    });
    break;
  case FSIM_ACC_ITERATOR_LOAD:
    items = members(vpiLoad, owner);
    std::erase_if(items, [](const vpiHandle item) {
      return !connection_object(item);
    });
    break;
  // The loads inside cell instances.
  case FSIM_ACC_ITERATOR_CELL_LOAD:
    items = members(vpiLoad, owner);
    std::erase_if(items, [](const vpiHandle item) {
      const auto module = vpi_handle(vpiModule, item);
      return !connection_object(item) || module == nullptr ||
             vpi_get(vpiCellInstance, module) != 1;
    });
    break;
  case FSIM_ACC_ITERATOR_HICONN:
    if (const auto connection = vpi_handle(vpiHighConn, owner)) {
      items.push_back(connection);
    }
    std::erase_if(items, [](const vpiHandle item) {
      const auto type = acc_types(item).first;
      return type != accNet && type != accNetBit && type != accReg &&
             type != accRegBit && type != accPort;
    });
    break;
  case FSIM_ACC_ITERATOR_LOCONN:
    if (const auto connection = vpi_handle(vpiLowConn, owner)) {
      items.push_back(connection);
    }
    std::erase_if(items, [](const vpiHandle item) {
      const auto type = acc_types(item).first;
      return type != accNet && type != accNetBit && type != accReg &&
             type != accRegBit && type != accPort;
    });
    break;
  case FSIM_ACC_ITERATOR_INPUT: items = members(vpiModPathIn, owner); break;
  case FSIM_ACC_ITERATOR_OUTPUT: items = members(vpiModPathOut, owner); break;
  case FSIM_ACC_ITERATOR_MODPATH: items = members(vpiModPath, owner); break;
  // The output and inout ports of a module.
  case FSIM_ACC_ITERATOR_PORTOUT:
    items = members(vpiPort, owner);
    std::erase_if(items, [](const vpiHandle item) {
      const auto direction = vpi_get(vpiDirection, item);
      return direction != vpiOutput && direction != vpiInout;
    });
    break;
  case FSIM_ACC_ITERATOR_TCHK: items = members(vpiTchk, owner); break;
  case FSIM_ACC_ITERATOR_TERMINAL:
    items = members(vpiPrimTerm, owner);
    break;
  default: break;
  }
  keep_acc_objects(items);
  return items;
}

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_iterate(
    void* /*user_data*/, const fsim_acc_iterator_query_v3* const query,
    fsim_acc_iterator_result_v3* const result) {
  switch (query->operation) {
  case FSIM_ACC_ITERATOR_BEGIN: {
    auto* const created = new Cursor{};
    created->items = iteration_members(*query);
    result->cursor =
        static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(created));
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  case FSIM_ACC_ITERATOR_NEXT: {
    auto* const active =
        reinterpret_cast<Cursor*>(static_cast<std::uintptr_t>(query->cursor));
    if (active->next >= active->items.size()) {
      return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    }
    result->cursor = query->cursor;
    result->object = acc_object(active->items[active->next++]);
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  case FSIM_ACC_ITERATOR_END:
    delete reinterpret_cast<Cursor*>(static_cast<std::uintptr_t>(query->cursor));
    return FSIM_ACC_VPI_OBJECT_VALID;
  default: return FSIM_ACC_VPI_OBJECT_INVALID;
  }
}

// A gate or switch whose output can be turned off: its delays are rise, fall
// and turn-off (IEEE 1364-2001 Table 115).
bool three_state_primitive(const vpiHandle object) {
  switch (vpi_get(vpiPrimType, object)) {
  case vpiBufif0Prim: case vpiBufif1Prim: case vpiNotif0Prim:
  case vpiNotif1Prim: case vpiNmosPrim: case vpiPmosPrim: case vpiCmosPrim:
  case vpiRnmosPrim: case vpiRpmosPrim: case vpiRcmosPrim:
    return true;
  default: return false;
  }
}

// The values the last timing query returned, valid until the next one.
std::vector<double>& acc_timing_values() {
  static std::vector<double> values;
  return values;
}

// vpi_get_delays and vpi_put_delays of `count` delays, each as one value
// (or three with `mtm`), followed by its reject and error limits with
// `pulses`, in the time unit of the object's module (IEEE 1364-2001 23.15,
// IEEE 1800-2023 38.10 Table 38-2). False when the VPI reports an error.
bool vpi_delay_rows(const vpiHandle object, const PLI_INT32 count,
                    const bool mtm, const bool pulses, const bool append,
                    const bool put, std::vector<double>& values) {
  const auto per_delay = (mtm ? 3U : 1U) * (pulses ? 3U : 1U);
  std::vector<s_vpi_time> times(static_cast<std::size_t>(count) * per_delay);
  for (std::size_t index = 0; put && index < times.size(); ++index) {
    times[index].type = vpiScaledRealTime;
    times[index].real = values[index];
  }
  s_vpi_delay delays{};
  delays.da = times.data();
  delays.no_of_delays = count;
  delays.time_type = vpiScaledRealTime;
  delays.mtm_flag = mtm ? 1 : 0;
  delays.append_flag = append ? 1 : 0;
  delays.pulsere_flag = pulses ? 1 : 0;
  if (put) {
    vpi_put_delays(object, &delays);
  } else {
    vpi_get_delays(object, &delays);
  }
  if (vpi_chk_error(nullptr) != 0) {
    return false;
  }
  if (!put) {
    values.resize(times.size());
    for (std::size_t index = 0; index < times.size(); ++index) {
      values[index] = times[index].real;
    }
  }
  return true;
}

// The toZ delay of rise and fall under accToHiZDelay (IEEE 1364-2001 Table
// 119).
double to_hiz_delay(const PLI_INT32 policy, const double rise,
                    const double fall) {
  if (policy == static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_AVERAGE)) {
    return (rise + fall) / 2.0;
  }
  if (policy == static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_MAXIMUM)) {
    return std::max(rise, fall);
  }
  return std::min(rise, fall);
}

// A pulse limit never exceeds the delay, and the reject limit never the
// error limit; limits that do are truncated (IEEE 1364-2001 23.2).
void truncate_pulse_limits(std::vector<double>& rows) {
  for (std::size_t index = 0; index + 2 < rows.size(); index += 3) {
    rows[index + 2] = std::min(rows[index + 2], rows[index]);
    rows[index + 1] = std::min(rows[index + 1], rows[index + 2]);
  }
}

// acc_append_delays and acc_replace_delays on a primitive, module path or
// timing check (IEEE 1364-2001 22.8, 23.1, 23.90). A path or 3-state
// primitive written with two delays and accToHiZDelay other than from_user
// gets its toZ delay calculated (Table 119) from the resulting rise and fall.
// The pulse limits of a path then stay unless they exceed the new delays
// (23.1).
bool write_delays(const vpiHandle object,
                  const fsim_acc_timing_query_v3& query, const bool append) {
  const bool mtm = query.min_typ_max != 0;
  const auto type = vpi_get(vpiType, object);
  const bool path = type == vpiModPath;
  const bool primitive = type == vpiGate || type == vpiSwitch || type == vpiUdp;
  auto count = static_cast<PLI_INT32>(query.delay_count);
  std::vector<double> rows(query.values, query.values + query.value_count);
  bool append_rows = append;
  const bool computed =
      !mtm &&
      query.to_hiz_policy !=
          static_cast<PLI_INT32>(FSIM_ACC_TIMING_TO_HIZ_FROM_USER) &&
      ((primitive && three_state_primitive(object)) ||
       (path && count == 2));
  if (computed) {
    if (append) {
      std::vector<double> current;
      if (!vpi_delay_rows(object, 2, false, false, false, false, current)) {
        return false;
      }
      rows[0] += current[0];
      rows[1] += current[1];
    }
    rows.push_back(to_hiz_delay(query.to_hiz_policy, rows[0], rows[1]));
    count = 3;
    append_rows = false;
  }
  if (!vpi_delay_rows(object, count, mtm, false, append_rows, true, rows)) {
    return false;
  }
  if (!path) {
    return true;
  }
  std::vector<double> limits;
  if (!vpi_delay_rows(object, count, false, true, false, false, limits)) {
    return false;
  }
  auto truncated = limits;
  truncate_pulse_limits(truncated);
  return truncated == limits ||
         vpi_delay_rows(object, count, false, true, false, true, truncated);
}

// The pulse routines of IEEE 1364-2001 23.2, 23.27, 23.91 and 23.94 on a
// module path: reject and error limits for each of `delay_count` delays, in
// the time unit of the path's module.
bool pulse_operation(const vpiHandle object,
                     const fsim_acc_timing_query_v3& query) {
  const auto count = static_cast<PLI_INT32>(query.delay_count);
  std::vector<double> rows;
  if (!vpi_delay_rows(object, count, false, true, false, false, rows)) {
    return false;
  }
  if (query.operation == FSIM_ACC_TIMING_FETCH_PULSE) {
    auto& values = acc_timing_values();
    values.clear();
    for (std::size_t index = 0; index < rows.size(); index += 3) {
      values.push_back(rows[index + 1]);
      values.push_back(rows[index + 2]);
    }
    return true;
  }
  for (std::size_t index = 0, pair = 0; index < rows.size();
       index += 3, pair += 2) {
    switch (query.operation) {
    case FSIM_ACC_TIMING_REPLACE_PULSE:
      rows[index + 1] = query.values[pair];
      rows[index + 2] = query.values[pair + 1];
      break;
    case FSIM_ACC_TIMING_APPEND_PULSE:
      rows[index + 1] += query.values[pair];
      rows[index + 2] += query.values[pair + 1];
      break;
    default:
      // acc_set_pulsere: percentages of each delay (23.94).
      rows[index + 1] = rows[index] * query.values[0] / 100.0;
      rows[index + 2] = rows[index] * query.values[1] / 100.0;
      break;
    }
  }
  truncate_pulse_limits(rows);
  return vpi_delay_rows(object, count, false, true, false, true, rows);
}

// The delay and pulse routines, the delay mode and the polarity of paths
// (IEEE 1364-2001 clause 23) on the VPI services.
uint32_t FSIM_NATIVE_PLUGIN_CALL acc_timing(
    void*, const fsim_acc_timing_query_v3* const query,
    fsim_acc_timing_result_v3* const result) {
  const auto object = vpi_object(query->object);
  if (object == nullptr) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  auto& values = acc_timing_values();
  bool done = false;
  switch (query->operation) {
  case FSIM_ACC_TIMING_FETCH_POLARITY:
    switch (vpi_get(vpiPolarity, object)) {
    case vpiPositive: result->polarity = accPositive; break;
    case vpiNegative: result->polarity = accNegative; break;
    default: result->polarity = accUnknown; break;
    }
    return FSIM_ACC_VPI_OBJECT_VALID;
  case FSIM_ACC_TIMING_FETCH_DELAY_MODE:
    switch (vpi_get(vpiDefDelayMode, object)) {
    case vpiDelayModePath: result->delay_mode = accDelayModePath; break;
    case vpiDelayModeDistrib: result->delay_mode = accDelayModeDistrib; break;
    case vpiDelayModeUnit: result->delay_mode = accDelayModeUnit; break;
    case vpiDelayModeZero: result->delay_mode = accDelayModeZero; break;
    case vpiDelayModeMTM: result->delay_mode = accDelayModeMTM; break;
    default: result->delay_mode = accDelayModeNone; break;
    }
    return FSIM_ACC_VPI_OBJECT_VALID;
  case FSIM_ACC_TIMING_FETCH_DELAYS:
    values.clear();
    done = vpi_delay_rows(object, static_cast<PLI_INT32>(query->delay_count),
                          query->min_typ_max != 0, false, false, false,
                          values);
    result->values = values.data();
    result->value_count = static_cast<uint32_t>(values.size());
    break;
  case FSIM_ACC_TIMING_APPEND_DELAYS:
    done = write_delays(object, *query, true);
    break;
  case FSIM_ACC_TIMING_REPLACE_DELAYS:
    done = write_delays(object, *query, false);
    break;
  case FSIM_ACC_TIMING_FETCH_PULSE:
  case FSIM_ACC_TIMING_APPEND_PULSE:
  case FSIM_ACC_TIMING_REPLACE_PULSE:
  case FSIM_ACC_TIMING_SET_PULSE_PERCENT:
    done = pulse_operation(object, *query);
    if (query->operation == FSIM_ACC_TIMING_FETCH_PULSE) {
      result->values = values.data();
      result->value_count = static_cast<uint32_t>(values.size());
    }
    break;
  default: return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  return done ? FSIM_ACC_VPI_OBJECT_VALID : FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

// A value change link (acc_vcl_add) as a cbValueChange callback on its
// object (IEEE 1364-2001 22.10).
struct VclLink {
  Instance* instance{};
  uint64_t identity{};
  vpiHandle object{};
  vpiHandle callback{};
  PLI_INT32 type{};
  PLI_INT32 flags{};
  uint32_t width{};
  uint64_t sequence{};
};

std::unordered_map<uint64_t, std::unique_ptr<VclLink>>& vcl_links() {
  static std::unordered_map<uint64_t, std::unique_ptr<VclLink>> links;
  return links;
}

PLI_INT32 vcl_changed(p_cb_data data);
void run(Instance& instance, uint32_t phase, PLI_INT32 reason,
         PLI_INT32 parameter, const VclDelivery* vcl);

uint32_t FSIM_NATIVE_PLUGIN_CALL acc_vcl(
    void* const user_data, const fsim_acc_vcl_query_v3* const query) {
  auto& links = vcl_links();
  if (query->operation == FSIM_ACC_VCL_UNREGISTER) {
    const auto link = links.find(query->link_identity);
    if (link == links.end()) {
      return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    }
    if (link->second->callback != nullptr) {
      (void)vpi_remove_cb(link->second->callback);
    }
    std::erase_if(pending_vcl, [&](const VclDelivery& delivery) {
      return delivery.link == query->link_identity;
    });
    links.erase(link);
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (query->operation == FSIM_ACC_VCL_FLUSH) {
    const auto link = links.find(query->link_identity);
    if (link == links.end() || link->second->callback == nullptr) {
      return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    }
    // The changes observed before acc_vcl_delete reach the consumer before
    // it returns (23.97, 23.98), in the contexts of the call site that added
    // the link (22.10); the running routine's are set aside meanwhile.
    const auto tf = fsim::runtime::acc_detail::suspend_tf_call();
    const auto acc = fsim::runtime::acc_detail::suspend_acc_call();
    const auto active = std::exchange(routine_active, false);
    std::deque<VclDelivery> queued;
    std::erase_if(pending_vcl, [&](VclDelivery& delivery) {
      if (delivery.link != query->link_identity) {
        return false;
      }
      queued.push_back(std::move(delivery));
      return true;
    });
    for (const auto& delivery : queued) {
      run(*delivery.instance, FSIM_TF_CALL_PHASE_REACTIVATE, reason_reactivate,
          0, &delivery);
    }
    const bool delivered =
        fsim::runtime::acc_detail::deliver_vcl_changes(link->second->callback);
    routine_active = active;
    return delivered ? FSIM_ACC_VPI_OBJECT_VALID : FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (query->operation != FSIM_ACC_VCL_REGISTER) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  auto created = std::make_unique<VclLink>();
  created->instance = static_cast<Instance*>(user_data);
  created->identity = query->link_identity;
  created->object = vpi_object(query->object);
  // A link on a port is a link on its loconn (22.10.1).
  if (vpi_get(vpiType, created->object) == vpiPort) {
    if (const auto loconn = vpi_handle(vpiLowConn, created->object)) {
      created->object = loconn;
    }
  }
  created->type = acc_types(created->object).first;
  created->flags = query->flags;
  const auto size = vpi_get(vpiSize, created->object);
  created->width = size > 0 ? static_cast<uint32_t>(size) : 1u;
  s_vpi_time at{vpiSimTime, 0, 0, 0.0};
  // The callback carries the value of each change in the format the link
  // reports it.
  s_vpi_value captured{
      created->type == accNamedEvent         ? vpiSuppressVal
      : created->flags == vcl_verilog_strength ? vpiStrengthVal
      : created->type == accRealVar          ? vpiRealVal
                                             : vpiVectorVal,
      {}};
  s_cb_data callback{};
  callback.reason = cbValueChange;
  callback.cb_rtn = &vcl_changed;
  callback.obj = created->object;
  callback.time = &at;
  callback.value = &captured;
  callback.user_data = reinterpret_cast<PLI_BYTE8*>(created.get());
  created->callback =
      fsim::runtime::acc_detail::register_vcl_callback(&callback);
  if (created->callback == nullptr) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  links.emplace(created->identity, std::move(created));
  return FSIM_ACC_VPI_OBJECT_VALID;
}

// The simulator's command line for acc_fetch_argc and acc_fetch_argv.
struct AccCommandLine {
  std::vector<PLI_BYTE8*> argv;
  std::vector<uint32_t> sizes;
};

const AccCommandLine& acc_command_line() {
  static const AccCommandLine line = [] {
    AccCommandLine result;
    s_vpi_vlog_info info{};
    if (vpi_get_vlog_info(&info) == 0 || info.argc <= 0 ||
        info.argv == nullptr ||
        static_cast<uint32_t>(info.argc) > FSIM_ACC_TF_MAXIMUM_ARGV) {
      return result;
    }
    for (PLI_INT32 index = 0; index < info.argc; ++index) {
      const auto size = info.argv[index] == nullptr
                            ? 0u
                            : static_cast<uint32_t>(
                                  std::strlen(info.argv[index]) + 1u);
      if (size == 0u || size > FSIM_ACC_TF_MAXIMUM_ARGV_BYTES) {
        return AccCommandLine{};
      }
      result.argv.push_back(info.argv[index]);
      result.sizes.push_back(size);
    }
    return result;
  }();
  return line;
}

// Enters the call site's ACC handle context for one routine call.
struct AccCall {
  AccState* state{};
  bool entered{};

  AccCall(Instance& instance, const fsim_tf_call_context_v3& call) {
    if (instance.module == nullptr) {
      return;
    }
    state = &instance.acc;
    auto& tf = state->tf;
    auto& context = state->context;
    if (!state->prepared) {
      state->prepared = true;
      // A null argument names the call itself, which no ACC handle denotes.
      for (const auto handle : instance.handles) {
        state->arguments.push_back(acc_object(handle == nullptr ? instance.call
                                                                 : handle));
      }
      const auto& line = acc_command_line();
      tf.abi_version = FSIM_ACC_TF_CONTEXT_ABI_VERSION;
      tf.struct_size = sizeof(tf);
      tf.argument_objects =
          state->arguments.empty() ? nullptr : state->arguments.data();
      tf.argument_count = static_cast<uint32_t>(state->arguments.size());
      tf.argc = static_cast<uint32_t>(line.argv.size());
      tf.argv = line.argv.empty() ? nullptr : line.argv.data();
      tf.argv_sizes = line.sizes.empty() ? nullptr : line.sizes.data();
      const auto scope = acc_object(instance.module);
      context.abi_version = FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION;
      context.struct_size = sizeof(context);
      context.simulation_identity = instance.bridge.design_id;
      context.hierarchy_generation = instance.bridge.generation;
      context.maximum_handles = FSIM_ACC_HANDLE_MAX_OBJECTS;
      context.user_data = &instance;
      context.resolve = &acc_resolve;
      context.calling_scope = scope;
      context.default_scope = scope;
      context.interactive_scope = scope;
      context.lookup = &acc_lookup;
      context.relation = &acc_relation;
      context.name = &acc_name;
      context.traverse = &acc_traverse;
      context.object_query = &acc_object_query;
      context.read = &acc_read;
      context.write = &acc_write;
      context.iterate = &acc_iterate;
      context.timing = &acc_timing;
      context.vcl = &acc_vcl;
      context.tf = &tf;
    }
    tf.call_context = &call;
    entered = fsim_acc_handle_context_enter_v3(&context) != 0;
  }
  AccCall(const AccCall&) = delete;
  AccCall& operator=(const AccCall&) = delete;
  ~AccCall() {
    if (state != nullptr) {
      if (entered) {
        fsim_acc_handle_context_leave_v3(&state->context);
      }
      state->tf.call_context = nullptr;
    }
  }
};

const char* message_label(const PLI_INT32 level, const uint32_t kind) {
  switch (level) {
  case ERR_MESSAGE: return "MESSAGE";
  case ERR_WARNING: return "WARNING";
  case ERR_ERROR: return "ERROR";
  case ERR_INTERNAL: return "INTERNAL ERROR";
  case ERR_SYSTEM: return "SYSTEM ERROR";
  default:
    return kind == FSIM_TF_CONTROL_ERROR     ? "ERROR"
           : kind == FSIM_TF_CONTROL_WARNING ? "WARNING"
                                             : "MESSAGE";
  }
}

// A warning or error as tf_error, tf_warning and tf_message report it: the
// severity, the text, [facility-code] and the call's source location
// (IEEE 1364-2001 25.13, 25.31). The facility and code are under 10
// characters.
void report_message(Instance& instance, const uint32_t kind,
                    const PLI_INT32 level, const PLI_BYTE8* const facility,
                    const uint32_t facility_size,
                    const PLI_BYTE8* const message_number,
                    const uint32_t message_number_size,
                    const PLI_BYTE8* const message, const uint32_t size) {
  std::string body{message, size};
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
    body.pop_back();
  }
  std::string line = std::string{message_label(level, kind)} + "! " + body;
  if (facility_size != 0 || message_number_size != 0) {
    const auto bounded = [](const PLI_BYTE8* const part, const uint32_t length) {
      return part == nullptr ? std::string{}
                             : std::string(part, std::min(length, 9u));
    };
    line += " [" + bounded(facility, facility_size) + "-" +
            bounded(message_number, message_number_size) + "]";
  }
  const auto file = text_of(vpiFile, instance.call);
  if (!file.empty()) {
    line += " (" + file + ":" + std::to_string(vpi_get(vpiLineNo, instance.call)) +
            ")";
  }
  line += "\n";
  (void)vpi_printf(text("%s"), line.c_str());
  // An error from checktf while the source compiles aborts compilation.
  if (instance.phase == FSIM_TF_CALL_PHASE_CHECK &&
      (level == ERR_ERROR || level == ERR_SYSTEM || level == ERR_INTERNAL)) {
    instance.abort_requested = true;
  }
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL emit(
    void* const user_data, const uint32_t kind, const PLI_INT32 channel,
    const PLI_INT32 level, const PLI_BYTE8* const facility,
    const uint32_t facility_size, const PLI_BYTE8* const message_number,
    const uint32_t message_number_size, const PLI_BYTE8* const message,
    const uint32_t size) {
  const auto length = static_cast<int>(size);
  switch (kind) {
  case FSIM_TF_CONTROL_OUTPUT:
    if (channel == 1) {
      (void)vpi_printf(text("%.*s"), length, message);
    } else {
      (void)vpi_mcd_printf(static_cast<PLI_UINT32>(channel), text("%.*s"),
                           length, message);
    }
    return 0;
  case FSIM_TF_CONTROL_WARNING:
  case FSIM_TF_CONTROL_ERROR:
  case FSIM_TF_CONTROL_MESSAGE:
    report_message(*static_cast<Instance*>(user_data), kind, level, facility,
                   facility_size, message_number, message_number_size, message,
                   size);
    return 0;
  case FSIM_TF_CONTROL_FINISH:
    (void)vpi_control(vpiFinish, 1);
    return 0;
  case FSIM_TF_CONTROL_STOP:
    (void)vpi_control(vpiStop, 1);
    return 0;
  default:
    return 1;
  }
}

// A delayed string put of an argument (tf_strdelputp and its siblings,
// IEEE 1364-2001 25.52): the text is a b, o, d or h string, the delay type
// is Table 196.
PLI_INT32 FSIM_NATIVE_PLUGIN_CALL put_string_argument(
    void* const user_data, const PLI_INT32 narg, const PLI_INT32 format,
    const PLI_BYTE8* const text, const uint64_t delay_ticks,
    const PLI_INT32 delaytype) {
  auto& instance = *static_cast<Instance*>(user_data);
  if (narg < 1 ||
      static_cast<std::size_t>(narg) > instance.handles.size() ||
      instance.handles[static_cast<std::size_t>(narg - 1)] == nullptr) {
    return 0;
  }
  s_vpi_value value{format == 'b'   ? vpiBinStrVal
                    : format == 'o' ? vpiOctStrVal
                    : format == 'd' ? vpiDecStrVal
                                    : vpiHexStrVal,
                    {}};
  value.value.str = const_cast<PLI_BYTE8*>(text);
  const auto scheduled = delay_ticks * instance.precision_factor;
  s_vpi_time after{vpiSimTime, static_cast<PLI_UINT32>(scheduled >> 32u),
                   static_cast<PLI_UINT32>(scheduled), 0.0};
  (void)vpi_put_value(instance.handles[static_cast<std::size_t>(narg - 1)], &value,
                      &after,
                      delaytype == 0   ? vpiInertialDelay
                      : delaytype == 1 ? vpiTransportDelay
                                       : vpiPureTransportDelay);
  return vpi_chk_error(nullptr) == 0 ? 1 : 0;
}

// The strength bit patterns of a scalar net argument (tf_nodeinfo, IEEE
// 1364-2001 25.35, 7.10): bit n of each pattern is strength level n of its
// scale, the encoding of the VPI strength constants. A known value has the
// strength of its side only, a z is high impedance on both sides, and an
// unknown value spans its level and every weaker one.
PLI_INT32 FSIM_NATIVE_PLUGIN_CALL read_strength_argument(
    void* const user_data, const PLI_INT32 narg, PLI_INT32* const zero,
    PLI_INT32* const one) {
  auto& instance = *static_cast<Instance*>(user_data);
  if (narg < 1 ||
      static_cast<std::size_t>(narg) > instance.handles.size() ||
      instance.handles[static_cast<std::size_t>(narg - 1)] == nullptr) {
    return 1;
  }
  s_vpi_value value{vpiStrengthVal, {}};
  vpi_get_value(instance.handles[static_cast<std::size_t>(narg - 1)], &value);
  if (value.format != vpiStrengthVal || value.value.strength == nullptr) {
    return 1;
  }
  const auto& scalar = *value.value.strength;
  constexpr PLI_INT32 high_impedance = 0x01;
  const auto level = [](const PLI_INT32 strength) {
    return strength & 0xff;
  };
  const auto span = [&](const PLI_INT32 strength) {
    const auto bits = level(strength);
    return bits == 0 ? high_impedance : (bits << 1) - 1;
  };
  switch (scalar.logic) {
  case vpi0:
    *zero = level(scalar.s0) == 0 ? high_impedance : level(scalar.s0);
    *one = 0;
    break;
  case vpi1:
    *zero = 0;
    *one = level(scalar.s1) == 0 ? high_impedance : level(scalar.s1);
    break;
  case vpiZ:
    *zero = *one = high_impedance;
    break;
  case vpiH:
    *zero = high_impedance;
    *one = span(scalar.s1);
    break;
  case vpiL:
    *zero = span(scalar.s0);
    *one = high_impedance;
    break;
  default:
    *zero = span(scalar.s0);
    *one = span(scalar.s1);
    break;
  }
  return 0;
}

// The words of a memory argument, each as its aval bytes then its bval bytes
// (tf_nodeinfo, Figure 169): the byte count of one group, the element count
// and the data. The lowest address comes first.
PLI_INT32 FSIM_NATIVE_PLUGIN_CALL read_memory_argument(
    void* const user_data, const PLI_INT32 narg, uint32_t* const group_bytes,
    uint32_t* const count, const PLI_BYTE8** const data) {
  auto& instance = *static_cast<Instance*>(user_data);
  if (narg < 1 ||
      static_cast<std::size_t>(narg) > instance.handles.size() ||
      instance.handles[static_cast<std::size_t>(narg - 1)] == nullptr) {
    return 1;
  }
  const auto memory = instance.handles[static_cast<std::size_t>(narg - 1)];
  auto words = members(vpiReg, memory);
  if (words.empty()) {
    words = members(vpiMemoryWord, memory);
  }
  const auto width = instance.arguments[static_cast<std::size_t>(narg - 1)].width;
  const auto bytes = (width + 7u) / 8u;
  instance.memory_bytes.assign(
      static_cast<std::size_t>(words.size()) * bytes * 2u, PLI_BYTE8{0});
  for (std::size_t element = 0; element < words.size(); ++element) {
    s_vpi_value read{vpiVectorVal, {}};
    vpi_get_value(words[element], &read);
    if (read.value.vector == nullptr) {
      continue;
    }
    for (uint32_t byte = 0; byte < bytes; ++byte) {
      const auto group = read.value.vector[byte / 4u];
      const auto shift = (byte % 4u) * 8u;
      const auto base = element * bytes * 2u;
      instance.memory_bytes[base + byte] =
          static_cast<PLI_BYTE8>((group.aval >> shift) & 0xffu);
      instance.memory_bytes[base + bytes + byte] =
          static_cast<PLI_BYTE8>((group.bval >> shift) & 0xffu);
    }
  }
  *group_bytes = bytes;
  *count = static_cast<uint32_t>(words.size());
  *data = instance.memory_bytes.data();
  return 0;
}

PLI_INT32 synchronized(p_cb_data data);
PLI_INT32 reactivated(p_cb_data data);
PLI_INT32 parameter_changed(p_cb_data data);
void drain_changes();

// Runs one routine of a cell in a TF call context, then applies what it
// assigned and scheduled.
void run(Instance& instance, const uint32_t phase, const PLI_INT32 reason,
         const PLI_INT32 parameter = 0, const VclDelivery* const vcl = nullptr) {
  const auto* const cell = instance.cell;
  const auto count = static_cast<uint32_t>(instance.arguments.size());
  instance.phase = phase;
  fsim_tf_call_context_v3 context{};
  context.phase = phase;
  context.argument_count = count;
  context.arguments = count == 0 ? nullptr : instance.arguments.data();
  context.instance = &instance.bridge;
  context.user_data = cell->data;
  context.module_instance_name = instance.module_name.data();
  context.scope_name = instance.scope_name.data();
  context.routine_name = instance.routine_name.data();
  context.work_area = instance.work_area;
  context.control_user_data = &instance;
  context.control_emit = &emit;
  std::array<uint32_t, 8> synchronizations{};
  context.synchronization_capacity =
      static_cast<uint32_t>(synchronizations.size());
  context.synchronization_kinds = synchronizations.data();

  std::vector<PLI_UINT32> aval;
  std::vector<PLI_UINT32> bval;
  if (phase == FSIM_TF_CALL_PHASE_CALL && cell->type == userfunction) {
    context.kind = FSIM_TF_CALL_RESULT_INTEGRAL;
    context.width = instance.result_width;
    context.word_count = (context.width + 31u) / 32u;
    aval.assign(context.word_count, 0u);
    bval.assign(context.word_count, 0u);
    context.aval_words = aval.data();
    context.bval_words = bval.data();
  } else if (phase == FSIM_TF_CALL_PHASE_CALL &&
             cell->type == userrealfunction) {
    context.kind = FSIM_TF_CALL_RESULT_REAL;
    context.width = 64u;
  }

  std::vector<fsim_tf_value_bridge_v3> values(count);
  std::vector<std::vector<t_vecval>> vectors(count);
  std::vector<std::string> strings(count);
  fsim_tf_time_bridge_v3 time{};
  std::array<uint64_t, 16> delays{};
  uint64_t precision_factor{1};
  int design_exponent{};
  uint64_t design_ticks{};
  if (runtime_phase(phase)) {
    const bool writable = writable_phase(phase);
    for (uint32_t index = 0; index < count; ++index) {
      const auto& argument = instance.arguments[index];
      const auto handle = instance.handles[index];
      auto& value = values[index];
      value.width = argument.width;
      value.writable =
          static_cast<uint32_t>(writable && writable_kind(argument.kind));
      if (argument.kind == tf_nullparam) {
        continue;
      }
      if (argument.kind == tf_string) {
        s_vpi_value read{vpiStringVal, {}};
        vpi_get_value(handle, &read);
        strings[index] = read.value.str == nullptr ? "" : read.value.str;
        strings[index].resize(
            std::min<std::size_t>(strings[index].size(), argument.width / 8u));
        value.kind = FSIM_TF_VALUE_STRING;
        value.string_size = static_cast<uint32_t>(strings[index].size());
        value.string_value = strings[index].data();
      } else if (argument.kind == tf_readonlyreal ||
                 argument.kind == tf_readwritereal) {
        s_vpi_value read{vpiRealVal, {}};
        vpi_get_value(handle, &read);
        value.kind = FSIM_TF_VALUE_REAL;
        value.real = read.value.real;
      } else {
        const auto words = (argument.width + 31u) / 32u;
        s_vpi_value read{vpiVectorVal, {}};
        // A whole memory has no vector value; tf_nodeinfo reads its words.
        if (instance.nodes[index].node_type != tf_memory_node) {
          vpi_get_value(handle, &read);
        }
        vectors[index].assign(words, t_vecval{});
        for (uint32_t word = 0; word < words && read.value.vector != nullptr;
             ++word) {
          vectors[index][word].avalbits =
              static_cast<PLI_INT32>(read.value.vector[word].aval);
          vectors[index][word].bvalbits =
              static_cast<PLI_INT32>(read.value.vector[word].bval);
        }
        value.kind = FSIM_TF_VALUE_INTEGRAL;
        value.word_count = words;
        value.vector_words = vectors[index].data();
      }
    }
    context.values = count == 0 ? nullptr : values.data();

    s_vpi_time now{vpiSimTime, 0, 0, 0.0};
    vpi_get_time(nullptr, &now);
    // The scheduler counts the design's precision; the routines see the
    // calling module's unit and precision (IEEE 1364-2001 25.26, 25.27).
    const auto design_precision =
        std::clamp(vpi_get(vpiTimePrecision, nullptr), -15, 0);
    const auto precision = std::clamp(
        vpi_get(vpiTimePrecision, instance.module), design_precision, 0);
    const auto unit = std::clamp(
        vpi_get(vpiTimeUnit, instance.module), precision, 0);
    design_exponent = design_precision;
    design_ticks = (static_cast<uint64_t>(now.high) << 32u) | now.low;
    for (auto exponent = design_precision; exponent < precision; ++exponent) {
      precision_factor *= 10u;
    }
    instance.precision_factor = precision_factor;
    time.unit_exponent = static_cast<int8_t>(unit);
    time.precision_exponent = static_cast<int8_t>(precision);
    time.tick_multiplier = 1u;
    time.scheduler_ticks =
        ((static_cast<uint64_t>(now.high) << 32u) | now.low) /
        precision_factor;
    time.delay_capacity = static_cast<uint32_t>(delays.size());
    time.delay_ticks = delays.data();
    context.time = &time;
  }

  if (fsim_tf_call_context_enter_v3(&context) != 0) {
    vpi_printf(text("ERROR: %s: PLI 1.0 call context rejected\n"),
               instance.routine_name.c_str());
    return;
  }
  fsim_tf_host_bridge_v3 host{};
  host.pvc_count = static_cast<uint32_t>(instance.pvc_current.size());
  host.pvc_current = instance.pvc_current.data();
  host.pvc_saved = instance.pvc_saved.data();
  host.put_user_data = &instance;
  host.put = &put_string_argument;
  host.node_count = static_cast<uint32_t>(instance.nodes.size());
  host.nodes = instance.nodes.data();
  host.strength = &read_strength_argument;
  host.memory = &read_memory_argument;
  if (runtime_phase(phase)) {
    // The routines see the module's precision; the simulation's is the
    // design's (IEEE 1364-2001 25.26, 25.27, 25.54).
    host.simulation_precision_exponent = static_cast<int16_t>(design_exponent);
    host.has_simulation_precision = 1u;
    host.simulation_ticks = design_ticks;
  }
  (void)fsim_tf_call_host_bind_v3(&context, &host);
  routine_active = true;
  auto acc = std::make_unique<AccCall>(instance, context);
  PLI_INT32 result{};
  if (vcl != nullptr) {
    // A value change consumer runs in the call site's contexts (22.10).
    fsim_acc_vcl_event_v3 event{};
    event.abi_version = FSIM_ACC_VCL_QUERY_ABI_VERSION;
    event.struct_size = sizeof(event);
    event.reason = vcl->reason;
    event.link_identity = vcl->link;
    event.sequence_identity = vcl->sequence;
    event.time_high = vcl->time_high;
    event.time_low = vcl->time_low;
    event.logic_value = vcl->logic_value;
    event.strength1 = vcl->strength1;
    event.strength2 = vcl->strength2;
    event.vector_width = vcl->vector_width;
    event.logic_word_count = static_cast<uint32_t>(vcl->words.size());
    event.logic_words = vcl->words.empty() ? nullptr : vcl->words.data();
    event.real_value = vcl->real;
    (void)fsim_acc_vcl_dispatch_v3(&instance.acc.context, &event);
  } else switch (phase) {
  case FSIM_TF_CALL_PHASE_CHECK:
    result = reason == reason_checktf
                 ? (cell->checktf == nullptr
                        ? 0
                        : cell->checktf(cell->data, reason))
                 : (cell->misctf == nullptr
                        ? 0
                        : cell->misctf(cell->data, reason, 0));
    break;
  case FSIM_TF_CALL_PHASE_CALL:
    result = cell->calltf == nullptr ? 0 : cell->calltf(cell->data, reason);
    break;
  default:
    result = cell->misctf == nullptr
                 ? 0
                 : cell->misctf(cell->data, reason, parameter);
    break;
  }
  (void)result;
  instance.work_area = context.work_area;
  acc.reset();
  fsim_tf_call_context_leave_v3(&context);
  routine_active = false;

  // The function result (tf_putp(0, ...)) and assigned arguments.
  if (context.assigned != 0 &&
      context.kind == FSIM_TF_CALL_RESULT_INTEGRAL) {
    std::vector<s_vpi_vecval> words(context.word_count);
    for (uint32_t word = 0; word < context.word_count; ++word) {
      words[word] = {aval[word], bval[word]};
    }
    s_vpi_value write{vpiVectorVal, {}};
    write.value.vector = words.data();
    (void)vpi_put_value(instance.call, &write, nullptr, vpiNoDelay);
  } else if (context.assigned != 0 &&
             context.kind == FSIM_TF_CALL_RESULT_REAL) {
    s_vpi_value write{vpiRealVal, {}};
    write.value.real = context.real;
    (void)vpi_put_value(instance.call, &write, nullptr, vpiNoDelay);
  }
  for (uint32_t index = 0; index < count && runtime_phase(phase); ++index) {
    const auto& value = values[index];
    if (value.assigned == 0) {
      continue;
    }
    if (value.kind == FSIM_TF_VALUE_REAL) {
      s_vpi_value write{vpiRealVal, {}};
      write.value.real = value.real;
      (void)vpi_put_value(instance.handles[index], &write, nullptr,
                          vpiNoDelay);
    } else if (value.kind == FSIM_TF_VALUE_INTEGRAL) {
      std::vector<s_vpi_vecval> words(value.word_count);
      for (uint32_t word = 0; word < value.word_count; ++word) {
        words[word] = {static_cast<PLI_UINT32>(vectors[index][word].avalbits),
                       static_cast<PLI_UINT32>(vectors[index][word].bvalbits)};
      }
      s_vpi_value write{vpiVectorVal, {}};
      write.value.vector = words.data();
      (void)vpi_put_value(instance.handles[index], &write, nullptr,
                          vpiNoDelay);
    }
  }

  // tf_synchronize and tf_rosynchronize: one misctf per time step.
  for (uint32_t index = 0; index < context.synchronization_count; ++index) {
    if (synchronizations[index] == FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_OFF) {
      instance.asynchronous = false;
      continue;
    }
    if (synchronizations[index] == FSIM_TF_SYNCHRONIZATION_ASYNCHRONOUS_ON) {
      instance.asynchronous = true;
      if (instance.pvc_current.empty()) {
        instance.pvc_current.assign(count, 0u);
        instance.pvc_saved.assign(count, 0u);
      }
      const bool watched = !instance.watches.empty();
      for (uint32_t argument = 0; !watched && argument < count; ++argument) {
        const auto handle = instance.handles[argument];
        if (handle == nullptr || vpi_get(vpiType, handle) == vpiConstant) {
          continue;
        }
        auto& watch = instance.watches.emplace_back(
            Watch{&instance, static_cast<PLI_INT32>(argument + 1u)});
        s_vpi_time at{vpiSuppressTime, 0, 0, 0.0};
        s_vpi_value value{vpiSuppressVal, {}};
        s_cb_data callback{};
        callback.reason = cbValueChange;
        callback.cb_rtn = &parameter_changed;
        callback.obj = handle;
        callback.time = &at;
        callback.value = &value;
        callback.user_data = reinterpret_cast<PLI_BYTE8*>(&watch);
        (void)vpi_register_cb(&callback);
        // A change on a driver of a net argument calls misctf with
        // reason_paramdrc (IEEE 1364-2001 21.3.2, 21.3.3, Table 88). A driver
        // is watched through the signal it drives; a driver the VPI cannot
        // watch (a continuous assignment) is not.
        const auto type = vpi_get(vpiType, handle);
        if (type != vpiNet && type != vpiNetBit) {
          continue;
        }
        for (const auto driver : members(vpiDriver, handle)) {
          auto& driver_watch = instance.watches.emplace_back(Watch{
              &instance, static_cast<PLI_INT32>(argument + 1u),
              reason_paramdrc});
          s_cb_data driver_callback{};
          driver_callback.reason = cbValueChange;
          driver_callback.cb_rtn = &parameter_changed;
          driver_callback.obj = driver;
          driver_callback.time = &at;
          driver_callback.value = &value;
          driver_callback.user_data =
              reinterpret_cast<PLI_BYTE8*>(&driver_watch);
          (void)vpi_register_cb(&driver_callback);
        }
      }
      continue;
    }
    const bool read_only =
        synchronizations[index] == FSIM_TF_SYNCHRONIZATION_READ_ONLY;
    auto& pending = instance.synchronization_pending[read_only ? 1u : 0u];
    if (pending) {
      continue;
    }
    s_vpi_time at{vpiSimTime, 0, 0, 0.0};
    s_cb_data callback{};
    callback.reason = read_only ? cbReadOnlySynch : cbReadWriteSynch;
    callback.cb_rtn = &synchronized;
    callback.time = &at;
    callback.user_data = reinterpret_cast<PLI_BYTE8*>(&instance);
    pending = vpi_register_cb(&callback) != nullptr;
  }
  // tf_setdelay and friends: misctf(reason_reactivate) after each delay.
  for (uint32_t index = 0; index < time.delay_count; ++index) {
    const auto scheduled = delays[index] * precision_factor;
    s_vpi_time after{vpiSimTime, static_cast<PLI_UINT32>(scheduled >> 32u),
                     static_cast<PLI_UINT32>(scheduled), 0.0};
    s_cb_data callback{};
    callback.reason = cbAfterDelay;
    callback.cb_rtn = &reactivated;
    callback.time = &after;
    callback.user_data = reinterpret_cast<PLI_BYTE8*>(&instance);
    (void)vpi_register_cb(&callback);
  }
  drain_changes();
}

PLI_INT32 synchronized(const p_cb_data data) {
  auto& instance = *reinterpret_cast<Instance*>(data->user_data);
  const bool read_only = data->reason == cbReadOnlySynch;
  instance.synchronization_pending[read_only ? 1u : 0u] = false;
  run(instance,
      read_only ? FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE
                : FSIM_TF_CALL_PHASE_SYNCHRONIZE,
      read_only ? reason_rosynch : reason_synch);
  return 0;
}

// misctf(reason_paramvc) for a changed argument, or reason_paramdrc for a
// changed driver of one (tf_asynchon), after any routine in progress returns.
PLI_INT32 parameter_changed(const p_cb_data data) {
  const auto watch = *reinterpret_cast<Watch*>(data->user_data);
  if (!watch.instance->asynchronous) {
    return 0;
  }
  // The software product sets the current flag once tf_asynchon enabled it
  // (IEEE 1364-2001 25.33); a driver change is no change of the parameter.
  if (watch.reason == reason_paramvc) {
    watch.instance
        ->pvc_current[static_cast<std::size_t>(watch.parameter - 1)] = 1u;
  }
  pending_changes.push_back(watch);
  if (!routine_active) {
    drain_changes();
  }
  return 0;
}

void drain_changes() {
  while (!pending_changes.empty() || !pending_vcl.empty()) {
    if (!pending_changes.empty()) {
      const auto next = pending_changes.front();
      pending_changes.pop_front();
      if ((next.reason != reason_paramvc && next.reason != reason_paramdrc) ||
          next.instance->asynchronous) {
        run(*next.instance, FSIM_TF_CALL_PHASE_SYNCHRONIZE, next.reason,
            next.parameter);
      }
    } else {
      const auto next = std::move(pending_vcl.front());
      pending_vcl.pop_front();
      run(*next.instance, FSIM_TF_CALL_PHASE_REACTIVATE, reason_reactivate, 0,
          &next);
    }
  }
}

// The acc_next-style scalar of the low bit of a vector word (Table 181).
uint32_t scalar_logic(const fsim_acc_logic_word_v3 word) {
  const auto aval = word.aval & 1u;
  if ((word.bval & 1u) == 0u) {
    return aval == 0u ? acc0 : acc1;
  }
  return aval == 0u ? accZ : accX;
}

// Strength constants of Table 122 from a vpiStrengthVal level.
uint32_t vcl_strength(const PLI_INT32 strength) {
  switch (strength) {
  case vpiSupplyDrive: return vclSupply;
  case vpiStrongDrive: return vclStrong;
  case vpiPullDrive: return vclPull;
  case vpiLargeCharge: return vclLarge;
  case vpiWeakDrive: return vclWeak;
  case vpiMediumCharge: return vclMedium;
  case vpiSmallCharge: return vclSmall;
  default: return vclHighZ;
  }
}

// Reads the changed object into the delivery, choosing the vc_reason of
// Table 120; false when the value cannot be read.
// The value a link reports: the value of the change its callback carries,
// read again only when the callback carries none.
void vcl_value(const VclLink& link, const s_vpi_value* const captured,
               s_vpi_value& value) {
  if (captured != nullptr && captured->format == value.format) {
    value = *captured;
    return;
  }
  vpi_get_value(link.object, &value);
}

bool read_vcl_value(const VclLink& link, const s_vpi_value* const captured,
                    VclDelivery& delivery) {
  if (link.type == accNamedEvent) {
    delivery.reason = event_value_change;
    return true;
  }
  if (link.flags == vcl_verilog_strength) {
    s_vpi_value value{vpiStrengthVal, {}};
    vcl_value(link, captured, value);
    if (value.format != vpiStrengthVal || value.value.strength == nullptr) {
      return false;
    }
    const auto& scalar = *value.value.strength;
    delivery.reason = strength_value_change;
    // Known strengths repeat; an unknown value gives its 1 and 0 parts.
    const auto one = vcl_strength(scalar.s1);
    const auto zero = vcl_strength(scalar.s0);
    switch (scalar.logic) {
    case vpi0:
      delivery.logic_value = acc0;
      delivery.strength1 = delivery.strength2 = zero;
      break;
    case vpi1:
      delivery.logic_value = acc1;
      delivery.strength1 = delivery.strength2 = one;
      break;
    case vpiZ:
      delivery.logic_value = accZ;
      delivery.strength1 = delivery.strength2 = vclHighZ;
      break;
    default:
      delivery.logic_value = accX;
      delivery.strength1 = one;
      delivery.strength2 = zero;
      break;
    }
    return true;
  }
  if (link.type == accRealVar) {
    s_vpi_value value{vpiRealVal, {}};
    vcl_value(link, captured, value);
    delivery.reason = real_value_change;
    delivery.real = value.value.real;
    return true;
  }
  const bool scalar = link.width == 1u && link.type != accIntegerVar &&
                      link.type != accTimeVar;
  const bool variable = link.type == accReg || link.type == accRegBit;
  delivery.reason = link.type == accIntegerVar ? integer_value_change
                    : link.type == accTimeVar  ? time_value_change
                    : variable ? (scalar ? sregister_value_change
                                         : vregister_value_change)
                               : (scalar ? logic_value_change
                                         : vector_value_change);
  s_vpi_value value{vpiVectorVal, {}};
  vcl_value(link, captured, value);
  if (value.value.vector == nullptr) {
    return false;
  }
  const auto words = (link.width + 31u) / 32u;
  std::vector<fsim_acc_logic_word_v3> read(words);
  for (uint32_t word = 0; word < words; ++word) {
    read[word] = {value.value.vector[word].aval,
                  value.value.vector[word].bval};
  }
  if (scalar) {
    delivery.logic_value = scalar_logic(read[0]);
  } else {
    delivery.vector_width = link.width;
    delivery.words = std::move(read);
  }
  return true;
}

PLI_INT32 vcl_changed(const p_cb_data data) {
  auto& link = *reinterpret_cast<VclLink*>(data->user_data);
  VclDelivery delivery;
  delivery.instance = link.instance;
  delivery.link = link.identity;
  delivery.sequence = ++link.sequence;
  if (data->time != nullptr) {
    delivery.time_high = static_cast<PLI_INT32>(data->time->high);
    delivery.time_low = static_cast<PLI_INT32>(data->time->low);
  }
  if (!read_vcl_value(link, data->value, delivery)) {
    return 0;
  }
  // A consumer waits for the routine that changed the value to return.
  if (routine_active) {
    pending_vcl.push_back(std::move(delivery));
  } else {
    run(*delivery.instance, FSIM_TF_CALL_PHASE_REACTIVATE, reason_reactivate,
        0, &delivery);
  }
  return 0;
}

PLI_INT32 reactivated(const p_cb_data data) {
  run(*reinterpret_cast<Instance*>(data->user_data),
      FSIM_TF_CALL_PHASE_REACTIVATE, reason_reactivate);
  return 0;
}

PLI_INT32 end_of_compile(const p_cb_data data) {
  run(*reinterpret_cast<Instance*>(data->user_data),
      FSIM_TF_CALL_PHASE_CHECK, reason_endofcompile);
  return 0;
}

PLI_INT32 finished(const p_cb_data data) {
  auto& instance = *reinterpret_cast<Instance*>(data->user_data);
  if (instance.executed) {
    run(instance, FSIM_TF_CALL_PHASE_READ_ONLY_SYNCHRONIZE, reason_finish);
  }
  return 0;
}

// misctf(reason_force, reason_release or reason_disable) of every executed
// instance, after any routine in progress returns. paramvc is the index of
// the argument the statement forced or released, else 0.
void notify_statement(const PLI_INT32 reason, const vpiHandle target) {
  for (const auto& entry : instances()) {
    auto& instance = *entry.second;
    if (!instance.executed || instance.cell->misctf == nullptr) {
      continue;
    }
    PLI_INT32 parameter = 0;
    for (std::size_t index = 0;
         target != nullptr && index < instance.handles.size(); ++index) {
      if (instance.handles[index] != nullptr &&
          vpi_compare_objects(instance.handles[index], target) != 0) {
        parameter = static_cast<PLI_INT32>(index + 1u);
        break;
      }
    }
    pending_changes.push_back(Watch{&instance, parameter, reason});
  }
  if (!routine_active) {
    drain_changes();
  }
}

// cbForce and cbAssign give reason_force, cbRelease and cbDeassign give
// reason_release (Table 88: a procedural continuous assignment counts as a
// force, a deassign as a release); obj is the statement, whose left-hand
// side is the target (IEEE 1800-2023 38.36.1).
PLI_INT32 forced(const p_cb_data data) {
  const auto reason = data->reason == cbForce || data->reason == cbAssign
                          ? reason_force
                          : reason_release;
  notify_statement(reason, vpi_handle(vpiLhs, data->obj));
  return 0;
}

PLI_INT32 disabled(const p_cb_data) {
  notify_statement(reason_disable, nullptr);
  return 0;
}

// Registers cbDisable on each named block, task and function below scope;
// false once the registration is refused, as it is without the execution
// probes of simulate --foreign-probes (IEEE 1800-2023 38.36.1).
bool watch_disables(const vpiHandle scope) {
  const auto children = vpi_iterate(vpiInternalScope, scope);
  if (children == nullptr) {
    return true;
  }
  while (const vpiHandle child = vpi_scan(children)) {
    const auto type = vpi_get(vpiType, child);
    if (type == vpiNamedBegin || type == vpiNamedFork || type == vpiTask ||
        type == vpiFunction) {
      s_cb_data callback{};
      callback.reason = cbDisable;
      callback.cb_rtn = &disabled;
      callback.obj = child;
      if (vpi_register_cb(&callback) == nullptr) {
        vpi_free_object(children);
        return false;
      }
    }
    if (!watch_disables(child)) {
      vpi_free_object(children);
      return false;
    }
  }
  return true;
}

PLI_INT32 end_of_compile_disables(const p_cb_data) {
  const auto tops = vpi_iterate(vpiModule, nullptr);
  if (tops == nullptr) {
    return 0;
  }
  while (const vpiHandle top = vpi_scan(tops)) {
    if (!watch_disables(top)) {
      vpi_free_object(tops);
      break;
    }
  }
  return 0;
}

// The statement watches of the misctf reasons run at the end of compile: the
// force and assign reasons cannot be registered from a startup routine (IEEE
// 1800-2023 36.10.2), and veriusertfs_register_table is called from one.
PLI_INT32 install_statement_watches(p_cb_data data) {
  for (const auto reason : {cbForce, cbAssign, cbRelease, cbDeassign}) {
    s_cb_data callback{};
    callback.reason = reason;
    callback.cb_rtn = &forced;
    (void)vpi_register_cb(&callback);
  }
  return end_of_compile_disables(data);
}

// Installed once, and only when a cell has a misctf routine, so a design
// without one pays nothing.
void watch_statements() {
  static bool installed{};
  if (installed) {
    return;
  }
  installed = true;
  s_cb_data callback{};
  callback.reason = cbEndOfCompile;
  callback.cb_rtn = &install_statement_watches;
  (void)vpi_register_cb(&callback);
}

PLI_INT32 compiletf(PLI_BYTE8* const user_data) {
  const auto cell = reinterpret_cast<p_tfcell>(user_data);
  const auto call = vpi_handle(vpiSysTfCall, nullptr);
  if (call == nullptr) {
    return 0;
  }
  auto& instance = instance_for(call, cell);
  if (cell->checktf != nullptr) {
    run(instance, FSIM_TF_CALL_PHASE_CHECK, reason_checktf);
    // An error-level message from checktf aborts compilation (25.31); the
    // VPI has no abort, so the run ends.
    if (instance.abort_requested) {
      (void)vpi_control(vpiFinish, 1);
    }
  }
  if (cell->misctf != nullptr) {
    for (const auto reason : {cbEndOfCompile, cbEndOfSimulation}) {
      s_cb_data callback{};
      callback.reason = reason;
      callback.cb_rtn = reason == cbEndOfCompile ? &end_of_compile : &finished;
      callback.user_data = reinterpret_cast<PLI_BYTE8*>(&instance);
      (void)vpi_register_cb(&callback);
    }
  }
  return 0;
}

PLI_INT32 sizetf(PLI_BYTE8* const user_data) {
  const auto cell = reinterpret_cast<p_tfcell>(user_data);
  return static_cast<PLI_INT32>(function_width(cell));
}

PLI_INT32 calltf(PLI_BYTE8* const user_data) {
  const auto cell = reinterpret_cast<p_tfcell>(user_data);
  const auto call = vpi_handle(vpiSysTfCall, nullptr);
  if (call != nullptr) {
    auto& instance = instance_for(call, cell);
    instance.executed = true;
    run(instance, FSIM_TF_CALL_PHASE_CALL, reason_calltf);
  }
  return 0;
}

}  // namespace

void veriusertfs_register_table(const p_tfcell table) {
  for (auto cell = table; cell != nullptr && cell->type != 0; ++cell) {
    if (cell->misctf != nullptr) {
      watch_statements();
    }
    s_vpi_systf_data data{};
    data.type = cell->type == usertask ? vpiSysTask : vpiSysFunc;
    data.sysfunctype =
        cell->type == userrealfunction ? vpiRealFunc : vpiSizedFunc;
    data.tfname = cell->tfname;
    data.calltf = &calltf;
    data.compiletf = &compiletf;
    data.sizetf = cell->type == userfunction ? &sizetf : nullptr;
    data.user_data = reinterpret_cast<PLI_BYTE8*>(cell);
    (void)vpi_register_systf(&data);
  }
}
