#include <string>
#include <set>
#include <algorithm>
#include <sstream>

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "datashield"

#define DCDBG(...) DEBUG(dbgs() << "[DataShield] " << __VA_ARGS__ )

STATISTIC(NumLoads, "Total number of loads");
STATISTIC(NumBoundsChecks, "Total number of bounds checks");
STATISTIC(NumBoundsStores, "Total number of bounds stores");
STATISTIC(NumBoundsLoads, "Total number of bounds loads");

static cl::opt<bool>
IntegrityOnlyMode("datashield-integrity-only-mode",
    cl::desc("only enforce unsensitive data integrity, mask only stores"),
    cl::init(false));

static cl::opt<bool>
ConfidentialityOnlyMode("datashield-confidentiality-only-mode",
    cl::desc("only enforce unsensitive data confidentiality, mask only loads"),
    cl::init(false));

static cl::opt<bool>
DebugMode("datashield-debug-mode",
    cl::desc("enable extra debug information"),
    cl::init(false));

static cl::opt<bool>
UseMPX("datashield-use-mpx",
    cl::desc("enable mpx bounds checking for unsafe pointers"),
    cl::init(false));

static cl::opt<bool>
UseMask("datashield-use-mask",
    cl::desc("use a mask for non-sensitive bounds enforcement"),
    cl::init(false));

static cl::opt<bool>
UsePrefix("datashield-use-prefix-check",
    cl::desc("use a prefix for non-sensitive bounds enforcement"),
    cl::init(false));

static cl::opt<bool>
SaveModuleAfter("datashield-save-module-after",
    cl::desc("save the module to disk after transformation"),
    cl::init(false));

static cl::opt<bool>
SaveModuleBefore("datashield-save-module-before",
    cl::desc("save the module to disk before transformation"),
    cl::init(false));

static cl::opt<bool>
LibraryMode("datashield-library-mode",
    cl::desc("run in library mode ie just mask everything and replace malloc/free"),
    cl::init(false));

static cl::opt<bool>
UseSeparationMode("datashield-separation-mode",
    cl::desc("use the separation mode propagation algorith"),
    cl::init(false));


namespace {

class ReplacementMap;

Type *int64Ty, *voidTy, *int32Ty;
StructType *boundsTy;
PointerType  *int8PtrTy, *int8PtrPtrTy, *int32PtrTy, *int16PtrPtrTy, *int64PtrTy, *int32PtrPtrTy;

Function *unsafeMalloc, *unsafeFree, *unsafeCalloc, *unsafeRealloc, *unsafeStrdup;
Function *safeMalloc, *safeFree, *safeCalloc, *safeRealloc, *safeStrdup;

Function *safeMallocDebug, *safeCallocDebug;

Function *metadataCopy = nullptr;
Function *metadataCopyDebug = nullptr;
Function *safeMemCpy = nullptr;
Function *maskDebug = nullptr;
Function *safeDealloc = nullptr;
Function* safeFreeDebug = nullptr;
Function* safeAllocDebug = nullptr;
Function* safeStrError = nullptr;
Function* safeGetTimeOfDay = nullptr;
Function* safeMemAlignDebug = nullptr;

size_t IDCounter = 0;

vector<StringRef> whiteList;

typedef Value Bounds;
typedef map<Function*, vector<GlobalVariable*>> FunctionToGlobalMapTy;
typedef set<Instruction*> InstructionSet;
typedef map<Value*, Bounds*> BoundsMap;
typedef map<Value*, Value*> BasedOnMap;

Function* findFunctionWithSameName(set<Function*> Fs, StringRef name) {
  for (auto& F : Fs) {
      if (F->getName() == name) {
        return F;
      }
  }
  return nullptr;
}

void getRuntimeMemManFunctions(Module& M) {
    auto mallocTy = FunctionType::get(int8PtrTy, {int64Ty}, false);
    unsafeMalloc = dyn_cast<Function>(M.getOrInsertFunction("__ds_unsafe_malloc", mallocTy));
    assert(unsafeMalloc && "should be able to get rt functions");
    safeMalloc = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_malloc", mallocTy));
    assert(safeMalloc && "should be able to get rt functions");

    auto allocDebugTy = FunctionType::get(int8PtrTy, {int64Ty, int64Ty}, false);
    safeAllocDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_debug_safe_alloc", allocDebugTy));
    assert(safeAllocDebug && "should be able to get rt functions");

    auto mallocDebugTy = FunctionType::get(int8PtrTy, {int64Ty, int8PtrTy, int64Ty}, false);
    safeMallocDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_debug_safe_malloc", mallocDebugTy));
    assert(safeMallocDebug && "should be able to get rt functions");

    auto freeTy = FunctionType::get(voidTy, {int8PtrTy}, false);
    unsafeFree = dyn_cast<Function>(M.getOrInsertFunction("__ds_unsafe_free", freeTy));
    safeFree = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_free", freeTy));
    assert(unsafeFree && "should be able to get rt functions");
    assert(safeFree && "should be able to get rt functions");

    auto safeDeallocTy = FunctionType::get(voidTy, {int8PtrTy, int64Ty}, false);
    safeDealloc = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_dealloc", safeDeallocTy));
    assert(safeDealloc && "should be able to get rt functions");

    auto freeDebugTy = FunctionType::get(voidTy, {int8PtrTy, int64Ty}, false);
    safeFreeDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_free_debug", freeDebugTy));
    assert(safeFreeDebug && "should be able to get rt functions");

    auto callocTy = FunctionType::get(int8PtrTy, {int64Ty, int64Ty}, false);
    unsafeCalloc = dyn_cast<Function>(M.getOrInsertFunction("__ds_unsafe_calloc", callocTy));
    safeCalloc = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_calloc", callocTy));
    assert(unsafeCalloc && "should be able to get rt functions");
    assert(safeCalloc && "should be able to get rt functions");

    auto callocDebugTy = FunctionType::get(int8PtrTy, {int64Ty, int64Ty, int8PtrTy}, false);
    safeCallocDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_debug_safe_calloc", callocDebugTy));
    assert(safeCallocDebug && "should be able to get rt functions");

    auto reallocTy = FunctionType::get(int8PtrTy, {int8PtrTy, int64Ty}, false);
    unsafeRealloc = dyn_cast<Function>(M.getOrInsertFunction("__ds_unsafe_realloc", reallocTy));
    safeRealloc = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_realloc", reallocTy));
    assert(unsafeRealloc && "should be able to get rt functions");
    assert(safeRealloc && "should be able to get rt functions");

    auto strdupTy = FunctionType::get(int8PtrTy, {int8PtrTy}, false);
    unsafeStrdup = dyn_cast<Function>(M.getOrInsertFunction("__ds_unsafe_strdup", strdupTy));
    safeStrdup = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_strdup", strdupTy));
    assert(unsafeStrdup && "should be able to get rt functions");
    assert(safeStrdup && "should be able to get rt functions");

    auto metadataCopyTy = FunctionType::get(voidTy, {int8PtrTy, int8PtrTy, int64Ty}, false);
    metadataCopy = dyn_cast<Function>(M.getOrInsertFunction("__ds_metadata_copy", metadataCopyTy));
    assert(metadataCopy && "should be able to get rt functions");

    auto metadataCopyDebugTy = FunctionType::get(voidTy, {int8PtrTy, int8PtrTy, int64Ty, int64Ty}, false);
    metadataCopyDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_metadata_copy_debug", metadataCopyDebugTy));
    assert(metadataCopyDebug && "should be able to get rt functions");

    safeMemCpy = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_memcpy", metadataCopyTy));
    assert(safeMemCpy && "should be able to get rt functions");

    auto maskDebugTy = FunctionType::get(int8PtrTy, {int8PtrTy, int64Ty}, false);
    maskDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_mask_debug", maskDebugTy));
    assert(maskDebug && "should be able to get rt functions");

    if (!LibraryMode) {
      auto strErrorTy = FunctionType::get(int8PtrTy, {int32Ty}, false);
      safeStrError = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_strerror", strErrorTy));
      assert(safeStrError && "should be able to get runtime functions");

      auto timevalTy = M.getTypeByName("struct.timespec");
      if (timevalTy) {
        auto timevalPtrTy = timevalTy->getPointerTo();
        auto gettimeofdayTy = FunctionType::get(int32Ty, {timevalPtrTy, int8PtrTy}, false);
        safeGetTimeOfDay = dyn_cast<Function>(M.getOrInsertFunction("__ds_safe_gettimeofday", gettimeofdayTy));
        assert(safeGetTimeOfDay && "should be able to get rt functions");
      }
    }

    auto memalignDebugTy = FunctionType::get(int8PtrTy, {int64Ty, int64Ty, int8PtrTy, int64Ty}, false);
    safeMemAlignDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_debug_safe_memalign", memalignDebugTy));
    assert(safeMemAlignDebug && "should be able to get rt functions");
}

Constant *getOrCreateNumBoundsChecks(Module &M) {
  // The unsafe stack pointer is stored in a global variable with a magic name.
  const char *numBoundsName = "__ds_num_bounds_checks";

  auto numBoundsChecks =
      dyn_cast_or_null<GlobalVariable>(M.getNamedValue(numBoundsName));

  if (!numBoundsChecks) {
    // The global variable is not defined yet, define it ourselves.
    // We use the initial-exec TLS model because we do not support the variable
    // living anywhere other than in the main executable.
    numBoundsChecks = new GlobalVariable(
        /*Module=*/M, /*Type=*/int64Ty,
        /*isConstant=*/false, /*Linkage=*/GlobalValue::ExternalLinkage,
        /*Initializer=*/0, /*Name=*/numBoundsName,
        /*InsertBefore=*/nullptr//,
        ///*ThreadLocalMode=*/GlobalValue::InitialExecTLSModel
          );
  }

  return numBoundsChecks;
}

void insertStatsDump(Module& M) {
  //auto main = M.getFunction("orig_main");
  auto main = M.getFunction("main");
  if (!main) {
    llvm_unreachable("i was expecting to be about to find orig_main");
  }
  auto statsDumpTy = FunctionType::get(voidTy, {}, false);
  auto statsDump = dyn_cast<Function>(M.getOrInsertFunction("__ds_print_runtime_stats", statsDumpTy));
  assert(statsDump && "__ds_print_runtime_stats should exist");
  vector<ReturnInst*> returns;
  for (inst_iterator It = inst_begin(main), Ie = inst_end(main); It != Ie;) {
    Instruction *I = &*(It++);
    if (auto ret = dyn_cast<ReturnInst>(I)) {
      returns.push_back(ret);
    }
  }
  for (auto& ret : returns) {
    IRBuilder<> IRB(ret);
    IRB.CreateCall(statsDump, {});
  }

}

void replaceAllByNameWith(Module& M, StringRef name, Function& replacement) {
  auto origFn = M.getNamedValue(name);
  if (origFn) {
    origFn->replaceAllUsesWith(&replacement);
  }
}

void replaceWeirdCXXMemManFn(Module& M, Function* weirdFree, bool isFree) {
  for (auto v : weirdFree->users()) {
    if (auto call = dyn_cast<CallInst>(v)) {
      CallInst* repl;
      IRBuilder<> IRB(call);
      if (isFree) {
          repl = IRB.CreateCall(unsafeFree, {call->getOperand(0)});
      } else {
          repl = IRB.CreateCall(unsafeMalloc, {call->getOperand(0)});
      }
      call->replaceAllUsesWith(repl);
      call->eraseFromParent();
    }
  }
}

void replaceAllWeirdCXXMemManFn(Module& M) {
  vector<string> weirdMallocNames {
      "_ZdlPvRKSt9nothrow_t",
      "_ZdaPvRKSt9nothrow_t",
      "_ZnwjRKSt9nothrow_t",
      "_ZnwmRKSt9nothrow_t",
      "_ZnajRKSt9nothrow_t",
      "_ZnamRKSt9nothrow_t"
  };
  for (auto weirdName : weirdMallocNames) {
    auto weirdVal = M.getNamedValue(weirdName);
    if (weirdVal) {
      if (auto weirdFn = dyn_cast<Function>(weirdVal)) {
        replaceWeirdCXXMemManFn(M, weirdFn, weirdName[2] == 'd');
      }
    }
  }
}


bool isCallToNamedFn(CallInst* call, StringRef name) {
  return call->getCalledFunction() && call->getCalledFunction()->getName() == name;
}

bool isAllocationFnDS(CallInst* call, const TargetLibraryInfo* TLI) {
    if (isAllocationFn(call, TLI, true)) { return true; }
    if (isCallToNamedFn(call, "realloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_unsafe_malloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_unsafe_calloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_unsafe_realloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_unsafe_strdup")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_malloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_calloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_realloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_strdup")) { return true; }
    if (isCallToNamedFn(call, "__ds_debug_safe_malloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_debug_safe_calloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_debug_safe_alloc")) { return true; }
    if (isCallToNamedFn(call, "memalign")) { return true; }
    if (isCallToNamedFn(call, "_Znwj")) { return true; }
    if (isCallToNamedFn(call, "_Znwm")) { return true; }
    if (isCallToNamedFn(call, "_Znaj")) { return true; }
    if (isCallToNamedFn(call, "_Znam")) { return true; }
    return false;
}

bool isMallocLikeFnDS(CallInst* call, TargetLibraryInfo* TLI) {
    if(isMallocLikeFn(call, TLI, true)) { return true; }
    if (isCallToNamedFn(call, "__ds_unsafe_malloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_malloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_debug_safe_malloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_debug_safe_alloc")) { return true; }
    if (isCallToNamedFn(call, "_Znwj")) { return true; }
    if (isCallToNamedFn(call, "_Znwm")) { return true; }
    if (isCallToNamedFn(call, "_Znaj")) { return true; }
    if (isCallToNamedFn(call, "_Znam")) { return true; }
    return false;
}

bool isCallocLikeFnDS(CallInst* call, TargetLibraryInfo* TLI) {
    if(isCallocLikeFn(call, TLI, true)) { return true; }
    if (isCallToNamedFn(call, "__ds_unsafe_calloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_calloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_debug_safe_calloc")) { return true; }
    return false;
}

bool isReallocLikeFnDS(CallInst* call, TargetLibraryInfo* TLI) {
    if (isCallToNamedFn(call, "realloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_unsafe_realloc")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_realloc")) { return true; }
    return false;
}

bool isFreeCallDS(CallInst* call, TargetLibraryInfo* TLI) {
    if (isFreeCall(call, TLI)) { return true;}
    if (isCallToNamedFn(call, "__ds_unsafe_free")) { return true; }
    if (isCallToNamedFn(call, "__ds_safe_free")) { return true; }
    return false;
}

bool couldHaveBounds(Value* val) {
  if (val->getType()->isPointerTy() && !isa<ConstantPointerNull>(val)) {
    return true;
  }
  return false;
}

Value* getGlobalString(IRBuilder<>& IRB, StringRef msg) {
  auto debugVal = IRB.CreateGlobalString(msg, "debuginfo");
  auto debugPtr = ConstantExpr::getPointerCast(debugVal, int8PtrTy);
  return debugPtr;
}

Value* getDebugString(IRBuilder<>& IRB, Instruction* I) {
  static map<string, Value*> strMap;
  DebugLoc const& dl = I->getDebugLoc();
  stringstream ss;
  if (dl.get()) {
    // Print source line info.
    auto *Scope = cast<DIScope>(dl.getScope());
    ss << Scope->getFilename().str();
    ss << ':' << dl.getLine();
  } else {
    ss << "no debug symbols available";
  }
  if (strMap.count(ss.str())) {
    return strMap[ss.str()];
  } else {
    auto debugVal = IRB.CreateGlobalString(ss.str(), "debuginfo");
    auto debugPtr = ConstantExpr::getPointerCast(debugVal, int8PtrTy);
    strMap[ss.str()] = debugPtr;
    return debugPtr;
  }
}

void initTypeShortHands(Module& M) {
    int8PtrTy = Type::getInt8PtrTy(M.getContext());
    int64Ty = Type::getInt64Ty(M.getContext());
    int32Ty = Type::getInt32Ty(M.getContext());
    voidTy = Type::getVoidTy(M.getContext());
    int8PtrPtrTy = Type::getInt8Ty(M.getContext())->getPointerTo()->getPointerTo();
    int32PtrTy = int32Ty->getPointerTo();
    int32PtrPtrTy = int32PtrTy->getPointerTo();
    int16PtrPtrTy = Type::getInt16Ty(M.getContext())->getPointerTo()->getPointerTo();
    int64PtrTy = int64Ty->getPointerTo();
    boundsTy = StructType::get(int8PtrTy, int8PtrTy, nullptr);
}

void replaceAllFunctionUsesWith(Module& M, StringRef orig, StringRef repl) {
    auto origFn = M.getFunction(orig);
    auto replFn = M.getFunction(repl);
    if (origFn && replFn) {
        origFn->replaceAllUsesWith(replFn);
    }
}

/// A think wrapper around vector<pair<Instruction*, Instruction*>> that stores
/// pairs of instructions and replacements them
class ReplacementMap {
  public:
  vector<pair<Instruction*, Instruction*>> replMap;
  void push_back(pair<Instruction*, Instruction*> it) {
    replMap.push_back(it);
  }
  void replaceAndErase() {
    doReplacement(true);
  }
  void replace() {
    doReplacement(false);
  }
  void doReplacement(bool eraseFromParent) {
    for (auto item : replMap) {
      auto orig = item.first;
      auto repl = item.second;
      orig->replaceAllUsesWith(repl);
      if (eraseFromParent) {
        orig->eraseFromParent();
      }
    }
  }
};


void replaceOperator(Operator* op, Constant* repl) {
  ReplacementMap replMap;
  if (auto cast = dyn_cast<BitCastOperator>(op)) {
    vector<User*> castUsers(cast->user_begin(), cast->user_end());
    for (auto u : castUsers) {
      // this doesnt work because it doesnt drill down into the operands
      // (through operators) to find the global val
      //u->replaceUsesOfWith(orig, repl);

      if (auto store = dyn_cast<StoreInst>(u)) {
        IRBuilder<> IRB(store);
        Value *valOp, *ptrOp;
        if (op == store->getPointerOperand()) {
          valOp = store->getValueOperand();
          ptrOp = IRB.CreateBitCast(repl, cast->getDestTy(), repl->getName() + "_casted");
        } else if (op == store->getValueOperand()) {
          ptrOp = store->getPointerOperand();
          valOp = IRB.CreateBitCast(repl, cast->getDestTy(), repl->getName() + "_casted");
        } else {
          llvm_unreachable("how could it be neither?");
        }
        auto repl = IRB.CreateStore(valOp, ptrOp);
        replMap.push_back(pair<Instruction*, Instruction*>(store, repl));
      }
      else if (auto load = dyn_cast<LoadInst>(u)) {
        assert(op == load->getPointerOperand());
        IRBuilder<> IRB(load);
        auto ptrOp = IRB.CreateBitCast(repl, cast->getDestTy(), repl->getName() + "_casted");
        auto repl = IRB.CreateLoad(ptrOp);
        replMap.push_back(pair<Instruction*, Instruction*>(load, repl));
      }
      else {
        llvm_unreachable("how could it be some other type of instruction?");
      }
    }
  } else {
    op->dump();
    llvm_unreachable("operators other than bitcast not implemented.");
  }
  replMap.replaceAndErase();
}

Constant* getOrCreateCharPtrPtrGlobal(Module &M, const char* name) {
  auto int8PtrPtrTy = Type::getInt8PtrTy(M.getContext())->getPointerTo();
  auto gbl = (GlobalVariable*) M.getOrInsertGlobal(name, int8PtrPtrTy);
  gbl->setInitializer(ConstantPointerNull::get(int8PtrPtrTy));
  return gbl;
}

bool isWhiteListed(Function& F) {
  for (auto&s : whiteList) {
    if (F.getName().startswith(s)) { return true; }
  }
  return false;
}

Function* duplicateFunction(Module& M, Function& oldF, StringRef newName) {
  ValueToValueMapTy vMap;
  // fix me: change true to false in CloneFunction once they fix my bug
  auto newF = CloneFunction(&oldF, vMap, false);
  newF->setName(newName);
  M.getFunctionList().push_back(newF);
  return newF;
}

template<typename T>
void dumpSet(string header, set<T*> theSet) {
  dbgs() << header;
  for (auto i : theSet) {
    dbgs() << "\t";
    i->dump();
  }
}

void saveModule(Module& M, Twine filename) {
  int fd;
  sys::fs::openFileForWrite(filename, fd, sys::fs::F_RW | sys::fs::F_Text);
  raw_fd_ostream file(fd, true, true);
  M.print(file, nullptr, true);
}

// end static helper functions

class TypeSet {
  private:
  set<Type*> sensTys;
  set<Type*> nonsensTys;
  set<Type*> loopDetector;
  TypeSet() {}
  public:
  static TypeSet getEmpty() {
      return TypeSet();
  }
  TypeSet(Module& M) {
    findSensitiveTypeAnnotations(M);
    for (auto t : M.getIdentifiedStructTypes()) {
      isSensitiveTypeRecursive(t);
    }
  }
  pair<set<Type*>::iterator,bool> insert(Type* ty) {
    return sensTys.insert(ty);
  }
  size_t count(Type* ty) const {
    return sensTys.count(ty);
  }
  void dump() {
    dumpSet("sensitive type set: ", sensTys);
  }
  bool isSensitiveTypeRecursive(Type* type) {
    // consider optimization: memoize this function
    // i.e. record whether or not a type is sensitive rather
    // than re-exploring the entire type hierarchy every time
    // we encounter a type

    if (sensTys.count(type)) {
      return true;
    }
    if (nonsensTys.count(type)) {
      return false;
    }
    if (type->isMetadataTy()) {
      return false;
    }
    if (loopDetector.count(type)) {
      // we've already visited this type before but we
      // didnt memoize it, we're in an infinite loop
      nonsensTys.insert(type);
      return false;
    }

    loopDetector.insert(type);

    if (auto arrayTy = dyn_cast<ArrayType>(type)) {
      auto eleTy = arrayTy->getElementType();
      auto rv = isSensitiveTypeRecursive(eleTy);
      if (rv) {
          sensTys.insert(type);
          sensTys.insert(eleTy);
      } else {
          nonsensTys.insert(type);
          nonsensTys.insert(eleTy);
      }
      return rv;
    }

    if (auto structTy = dyn_cast<StructType>(type)) {
      for (auto e : structTy->elements()) {
        // remember that structs can have pointers to themselves
        // so we need to break the infinite loop
        if (e == structTy->getPointerTo() || e == structTy) {
            continue;
        }
        auto rv = isSensitiveTypeRecursive(e);
        if (rv) {
          sensTys.insert(e);
          sensTys.insert(structTy);
        } else {
          nonsensTys.insert(e);
          nonsensTys.insert(structTy);
        }
        return rv;
      }
    }
    if (auto ptrTy = dyn_cast<PointerType>(type)) {
      auto rv = isSensitiveTypeRecursive(ptrTy->getPointerElementType());
      if (rv) {
        sensTys.insert(type);
      } else {
        nonsensTys.insert(type);
      }
      return rv;
    }
    nonsensTys.insert(type);
    return false;
  }
  void findSensitiveTypeAnnotations(Module& M) {
    if (auto I = M.getNamedGlobal("llvm.global.annotations")) {
      Value *Op0 = I->getOperand(0);
      ConstantArray *arr = cast<ConstantArray>(Op0);
      for (unsigned int i = 0; i < arr->getNumOperands(); ++i) {
        ConstantStruct *annoStruct = cast<ConstantStruct>(arr->getOperand(i));
        Constant* cast = annoStruct->getOperand(0);
        Value* val = cast->getOperand(0);
        Value *ann = annoStruct->getOperand(1);
        Operator *op;
        Constant *c;
        if (isa<Operator>(ann) &&
            (op = dyn_cast<Operator>(ann)) &&
            (op->getOpcode() == Instruction::GetElementPtr) &&
            (c = dyn_cast<Constant>(op->getOperand(0))))
        {
          sensTys.insert(val->getType());
          if (auto ptrTy = dyn_cast<PointerType>(val->getType())) {
              sensTys.insert(ptrTy->getElementType());
          }

        } else {
          dbgs() << "in module: " << M.getModuleIdentifier() << "\n";
          annoStruct->dump();
          llvm_unreachable("unable to parse annotation");
        }
      }
    }
  }
  size_t size() {
    return sensTys.size();
  }
};

class ValueSet {
  set<Value*> vals;
  TypeSet& sensitiveTypes;
  ValueSet(TypeSet& sensitiveTypes) : sensitiveTypes(sensitiveTypes){}
  public:
  static ValueSet getEmpty(TypeSet& sensitiveTypes) {
      return ValueSet(sensitiveTypes);
  }
  const set<Value*> getVals() {
    return vals;
  }
  ValueSet(Module& M, TypeSet& sensitiveTypes): sensitiveTypes(sensitiveTypes) {
    findSensitiveTypedValues(M);
  }
  bool isChanged = false;
  pair<set<Value*>::iterator, bool> insert(Value* v) {
    if (!(isa<ConstantInt>(v) || isa<ConstantFP>(v) || isa<ConstantPointerNull>(v) || isa<UndefValue>(v))) {
      auto rv = vals.insert(v);
      if (rv.second) {
        //v->dump();
        if (auto ce = dyn_cast<ConstantExpr>(v)) {
          insert(ce->getOperand(0));
        } else if (auto vec = dyn_cast<ConstantDataVector>(v)) {
          if (vec->getElementType()->isPointerTy()) {
            for (unsigned i = 0, e = vec->getNumElements(); i != e; ++i) {
              insert(vec->getElementAsConstant(i));
            }
          }
        }
      }
      isChanged |= rv.second;
      return rv;
    }
    return pair<set<Value*>::iterator, bool>(vals.end(), false);
  }
  size_t count(Value* v) const {
    auto rv = vals.count(v);
    if (rv) { return rv; }
    if (auto ce = dyn_cast<ConstantExpr>(v)) {
      return count(ce->getOperand(0));
    } else if (auto vec = dyn_cast<ConstantDataVector>(v)) {
      for (unsigned i = 0, e = vec->getNumElements(); i != e; ++i) {
        if (auto cnt = count(vec->getElementAsConstant(i))) {
            return cnt;
        }
      }
      return 0;
    } else {
      return 0;
    }
  }
  void dump() {
    dumpSet("sensitive values set: ", vals);
  }
  void findSensitiveTypedValues(Function& F) {
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
      Instruction *I = &*(It++);
      for (auto& op : I->operands()) {
        auto val = op.get();
        //if (sensitiveTypes.count(val->getType())) {
        if (sensitiveTypes.isSensitiveTypeRecursive(val->getType())) {
          vals.insert(val);
        }
      }
    }
  }
  void findSensitiveTypedValues(Module& M) {
    // first, let's find every value that is
    // an explicitly sensitive type
    // look through globals
    for (auto& g : M.globals()) {
      auto type = g.getType();
      //if (sensitiveTypes.count(type)) {
      if (sensitiveTypes.isSensitiveTypeRecursive(type)) {
        vals.insert(&g);
      }
    }
    // look through all function arguments
    for (auto& F : M) {
      for (auto& a : F.args()) {
        if (sensitiveTypes.isSensitiveTypeRecursive(a.getType())) {
          vals.insert(&a);
        }
      }
    }
    // look through function bodies
    for (auto& F : M) {
      findSensitiveTypedValues(F);
    }
  }
};

void replaceAndMakeSensitive(Value* From, Value* To, ValueSet& sensitiveSet) {
  while(!From->use_empty()) {
    Use& U = *From->use_begin();
    if (auto I = dyn_cast<Instruction>(U.getUser())) {
      I->replaceUsesOfWith(From, To);
      sensitiveSet.insert(I);
    }
  }
  sensitiveSet.insert(To);
}

void replaceAndEraseAndMakeSensitive(ReplacementMap& replMap, ValueSet& sensitiveSet) {
  for (auto item : replMap.replMap) {
    auto From = item.first;
    auto To = item.second;
    replaceAndMakeSensitive(From, To, sensitiveSet);
    From->eraseFromParent();
  }
}

class CallInfo {
  public:
  CallInst& callSite;
  Function& parent;
  private:
  const ValueSet& sensitiveSet;
  TypeSet& sensitiveTypes; // TypeSet.count is not const because we may discover new sensitive subtypes
  const FunctionToGlobalMapTy& functionToGlobalMap;
  public:
  Function* replacement = nullptr;
  bool isDirectCall() const {
    return callSite.getCalledFunction() != nullptr;
  }
  bool isVarArg() const {
    return callSite.getFunctionType()->isVarArg();
  }
  unsigned getNumArgs() const {
    return callSite.getNumArgOperands();
  }
  Type* getParamType(unsigned i) const {
    return callSite.getFunctionType()->getParamType(i);
  }
  Type* getReturnType() const {
    return callSite.getFunctionType()->getReturnType();
  }
  bool isReturnValSensitive() const {
    return sensitiveSet.count(&callSite);
  }
  bool isArgSensitive(unsigned i) const {
    return sensitiveSet.count(callSite.getArgOperand(i));
  }
  bool isArgSensitiveAtCallee(unsigned i) const {
    if (auto callee = getCallee()) {
      for (auto &arg : callee->args()) {
        if (sensitiveSet.count(&arg)) {
          return true;
        }
      }
    }
    if (!replacement) { return false; } // we don't know
    auto arg = replacement->arg_begin();
    for (unsigned j = 0; j < i; ++j) {
      arg++;
    }
    return sensitiveSet.count(&*arg);
  }
  bool isParamTypeSensitive(unsigned i) const {
    return sensitiveTypes.count(getParamType(i));
  }
  bool isReturnTypeSensitive() const {
    return sensitiveTypes.count(getReturnType());
  }
  bool isCallToExternalFunction() const {
    if (!isDirectCall()) { llvm_unreachable("we cant tell which function a indirect call is to"); }
    return callSite.getCalledFunction()->isDeclaration();
  }
  bool usesSensiviteGlobal() const {
    auto callee = getCallee();
    if (callee == nullptr) {
      llvm_unreachable_internal("if we dont know what the callee is, we dont know if it uses sensitive globals");
    }
    if (functionToGlobalMap.count(callee) == 0) {
      return false;
    }
    for (auto& g : functionToGlobalMap.at(callee)) {
      if (sensitiveSet.count(g)) {
          return true;
      }
    }
    return false;
  }
  void print(raw_ostream& out) const {
    out << "CallInfo::print TODO\n";
    if (isDirectCall()) {
      out << "direct ";
    }
    if (isVarArg()) {
      out << "varArg ";
    }
    if(isDirectCall() && isCallToExternalFunction()) {
      out << "external ";
    }
    out << "parent: " << callSite.getParent()->getParent()->getName() << "\n";
    callSite.print(out);
    printSignature(out);
  }
  void printSignature(raw_ostream& out) const {
    out << "\tvalues: ";
    out << isReturnValSensitive();
    out << "(";
    unsigned N = getNumArgs();
    for (unsigned i = 0; i < N; ++i) {
      out << isArgSensitive(i) << ", ";
    }
    out << ")";

    out << "\ttypes: ";
    out << isReturnTypeSensitive();
    out << "(";
    if (!isVarArg()) {
      for (unsigned i = 0; i < N; ++i) {
        out << isParamTypeSensitive(i) << ", ";
      }
    } else {
      out << "...";
    }
    out << ")";
    out << "\n";
  }
  void dump() const {
    print(dbgs());
  }
  Function* getCallee() const {
    return callSite.getCalledFunction();
  }
  CallInfo(CallInst& cs, Function& p, ValueSet& senSet, TypeSet& sensTy, const FunctionToGlobalMapTy& f2g)
    : callSite(cs), parent(p), sensitiveSet(senSet), sensitiveTypes(sensTy), functionToGlobalMap(f2g)
  { }
  bool operator<(const CallInfo& other) const {
    return getCallee() < other.getCallee();
  }
  bool needsNewFunction() {
    if (isReturnTypeSensitive()) { return true; }
    if (isReturnValSensitive()) { return true; }
    unsigned N = getNumArgs();
    for (unsigned i = 0; i < N; ++i) {
      if (isArgSensitive(i)) { return true; }
    }
    if (!isVarArg()) {
      for (unsigned i = 0; i < N; ++i) {
        if (isParamTypeSensitive(i)) { return true; }
      }
      for (unsigned i = 0; i < N; ++i) {
        if (isArgSensitiveAtCallee(i)) { return true; }
      }
    }
    return false;
  }
  string getSignatureString() {
    stringstream ss;
    ss << "_";
    if(isReturnValSensitive() || isReturnTypeSensitive()) {
      ss << "1";
    } else {
      ss << "0";
    }
    ss << "_";
    for (unsigned i = 0, N = getNumArgs(); i != N; i++) {
      if (isArgSensitive(i) || isParamTypeSensitive(i) || isArgSensitiveAtCallee(i)) {
        ss << "1";
      } else {
        ss << "0";
      }
    }
    return ss.str();
  }
};

class CallInfoContainer {
  void findUsedGlobalVariables(Function& F) {
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
      Instruction *I = &*(It++);
      for (auto& o : I->operands()) {
        auto v = o.get();
        if(auto g = dyn_cast<GlobalVariable>(v)) {
          functionToGlobalMap[&F].push_back(g);
        }
      }
    }
  }
  public:
  vector<CallInfo> data;
  FunctionToGlobalMapTy& functionToGlobalMap;
  CallInfoContainer(Module& M, FunctionToGlobalMapTy& f2g) : functionToGlobalMap(f2g) {
    for (auto& F : M) {
      findUsedGlobalVariables(F);
    }
  }
  void makeCallInfoForEachCallInst(Module& M, ValueSet& sensitiveSet, TypeSet& sensitiveTypes) {
    // precondition: all variables are seperated into sensitive and nonsensitive
    for (auto& F : M) {
      if (F.isDeclaration()) { continue; } // skip decls
      if (isWhiteListed(F)) { continue; }
      makeCallInfoForEachCallInst(F, sensitiveSet, sensitiveTypes);
    }
  }
  void makeCallInfoForEachCallInst(Function& F, ValueSet& sensitiveSet, TypeSet& sensitiveTypes) {
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
      Instruction *I = &*(It++);
      if (auto call = dyn_cast<CallInst>(I)) {
        CallInfo ci(*call, F, sensitiveSet, sensitiveTypes, functionToGlobalMap);
        data.push_back(ci);
      }
    }
  }
  void print(raw_ostream& out) {
    for (auto i : data) {
      i.print(out);
    }
  }
  void dump() {
    print(dbgs());
  }
};

class MemoryRegioner {
  Function *unsafeMmap; // there is no safeMmap currently
  ValueSet& sensitiveSet;
  void getRuntimeFunctions(Module& M) {

    auto mmapTy = FunctionType::get(int8PtrTy, {int8PtrTy, int64Ty, int32Ty, int32Ty, int32Ty, int64Ty}, false);
    unsafeMmap = dyn_cast<Function>(M.getOrInsertFunction("__ds_unsafe_mmap", mmapTy));
    assert(unsafeMmap && "should be able to get rt functions");
  }
  CallInst* getSafeReplacementForAllocationOrFree(CallInst& call, TargetLibraryInfo& TLI) {
    IRBuilder<> IRB(&call);
    StringRef origName = call.getName();
    if (isMallocLikeFnDS(&call, &TLI)) {
      auto sizeArg = call.getArgOperand(0);
      auto dbgStr = getDebugString(IRB, &call);
      if (DebugMode) {
        auto id = ConstantInt::get(int64Ty, IDCounter++);
        return IRB.CreateCall(safeMallocDebug, {sizeArg, dbgStr, id}, origName);
      } else {
        return IRB.CreateCall(safeMalloc, {sizeArg}, origName);
      }
    } else if (isCallocLikeFnDS(&call, &TLI)) {
      auto sizeArg = call.getArgOperand(0);
      auto elemSizeArg = call.getArgOperand(1);
      auto dbgStr = getDebugString(IRB, &call);
      if (DebugMode) {
        return IRB.CreateCall(safeCallocDebug, {sizeArg, elemSizeArg, dbgStr}, origName);
      } else {
        return IRB.CreateCall(safeCalloc, {sizeArg, elemSizeArg}, origName);
      }
    } else if (isCallToNamedFn(&call, "strdup") || isCallToNamedFn(&call, "__strdup")) {
      return IRB.CreateCall(safeStrdup, {call.getArgOperand(0)}, call.getName());
    } else if (isReallocLikeFnDS(&call, &TLI)) {
      return IRB.CreateCall(safeRealloc, {call.getArgOperand(0), call.getArgOperand(1)}, origName);
    } else if (isCallToNamedFn(&call, "memalign")) {
      if (DebugMode) {
        auto dbgStr = getDebugString(IRB, &call);
        auto id = ConstantInt::get(int64Ty, IDCounter++);
        return IRB.CreateCall(safeMemAlignDebug, {call.getArgOperand(0), call.getArgOperand(1), dbgStr, id}, origName);
      } else {
        llvm_unreachable("safe memalign not implemented");
      }


    } else if (isAllocationFnDS(&call, &TLI)) {
        DEBUG(dbgs() << call.getName() << "\n");
        call.dump();
        llvm_unreachable("other allocation functions not implemented\n");
    }
    if (isFreeCallDS(&call, &TLI)) {
      if (DebugMode) {
        auto id = ConstantInt::get(int64Ty, IDCounter++);
        return IRB.CreateCall(safeFreeDebug, {call.getArgOperand(0), id});
      } else {
        return IRB.CreateCall(safeFree, {call.getArgOperand(0)});
      }
    }
    llvm_unreachable("we should have matched one of our allocation fns");
  }
  public:
  void replaceCFuncsThatAlloc(Function& F) {
    vector<pair<Instruction*, Instruction*>> replMap;
    for (auto& BB : F) {
      for (auto& I : BB) {
        if (auto call = dyn_cast<CallInst>(&I)) {
          if (call->getCalledFunction()) {
            if (call->getCalledFunction()->getName() == "mmap") {
              if (sensitiveSet.count(call)) {
                //llvm_unreachable("mmap not implemented in safe region");
              } else {
                IRBuilder<> IRB(call);
                auto replCall = IRB.CreateCall(unsafeMmap, {
                                                 call->getArgOperand(0),
                                                 call->getArgOperand(1),
                                                 call->getArgOperand(2),
                                                 call->getArgOperand(3),
                                                 call->getArgOperand(4),
                                                 call->getArgOperand(5)},
                                               call->getName() + "_unsafe");
                replMap.push_back(pair<Instruction*, Instruction*>(call, replCall));
              }
            }
            if (call->getCalledFunction()->getName() == "strerror") {
              if (sensitiveSet.count(call)) {
                IRBuilder<> IRB(call);
                auto replCall = IRB.CreateCall(safeStrError, {call->getArgOperand(0)}, call->getName() + "_safe");
                replMap.push_back(pair<Instruction*, Instruction*>(call, replCall));
              }
            }
            if (call->getCalledFunction()->getName() == "gettimeofday") {
              if (sensitiveSet.count(call->getArgOperand(0)) || sensitiveSet.count(call->getArgOperand(1))) {
                IRBuilder<> IRB(call);
                auto replCall = IRB.CreateCall(safeGetTimeOfDay, {call->getArgOperand(0), call->getArgOperand(1)}, call->getName() + "_safe");
                replMap.push_back(pair<Instruction*, Instruction*>(call, replCall));
              }
            }
          }
        }
      }
    }
    for (auto item : replMap) {
      auto orig = item.first;
      auto repl = item.second;
      orig->replaceAllUsesWith(repl);
      orig->eraseFromParent();
    }
  }
  bool isAnyOperandSensitive(CallInst& call) {
    if (sensitiveSet.count(&call)) { return true; }
    for (unsigned i = 0, e = call.getNumArgOperands(); i != e; ++i) {
      auto arg = call.getArgOperand(i);
      if (sensitiveSet.count(arg)) {
        return true;
      }
    }
    return false;
  }

  void replaceAllocationsWithSafe(Module& M, Function& F) {
    // for unsafe code i could just do the same thing I do for malloc/free and just replace
    // everything with its unsafe version and then selective replace that with the safe version
    TargetLibraryInfoImpl TLII(Triple(M.getTargetTriple()));
    TargetLibraryInfo TLI(TLII);
    ReplacementMap repls;
    for (auto& BB : F) {
      for (auto& inst : BB) {
        auto i = &inst;
        if (auto call = dyn_cast<CallInst>(i)) {
          if (isAnyOperandSensitive(*call) && (isAllocationFnDS(call, &TLI) || isFreeCallDS(call, &TLI))) {
            auto repl = getSafeReplacementForAllocationOrFree(*call, TLI);
            sensitiveSet.insert(repl);
            repls.push_back(pair<Instruction*, Instruction*>(call, repl));
          }
        }
      }
    }
    replaceAndEraseAndMakeSensitive(repls, sensitiveSet);
  }
  void moveSensitiveAllocsToHeap(Function &F, const DataLayout& DL) {
    // replace each sensitive alloca with a malloc (will replace with safe malloc later)
    // at new malloc to sensitive set
    // free all sensitive allocs before returning

    //dbgs() << "moving sensitive allocs in: " << F.getName() << "\n";

    vector<AllocaInst*> sensitiveAllocas;
    vector<CallInst*> replacementMallocs;

    // find all sensitive allocas
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
      Instruction *I = &*(It++);
      if (isa<AllocaInst>(I) && sensitiveSet.count(I)) {
          sensitiveAllocas.push_back(dyn_cast<AllocaInst>(I));
      }
    }

    if (sensitiveAllocas.size() == 0) { return; }

    // replace the allocas with mallocs and erase the allocas
    for (auto& alloca : sensitiveAllocas) {
      IRBuilder<> IRB(&*F.getEntryBlock().getFirstInsertionPt());
      auto sz = DL.getTypeAllocSize(alloca->getType()->getPointerElementType());
      auto sizeVal = ConstantInt::get(int64Ty, sz);
      CallInst* repl;
      if (DebugMode) {
        auto id = ConstantInt::get(int64Ty, IDCounter++);
        repl = IRB.CreateCall(safeAllocDebug, {sizeVal, id});
      } else {
        repl = IRB.CreateCall(safeMalloc, {sizeVal});
      }
      auto replCasted = IRB.CreateBitCast(repl, alloca->getType(), alloca->getName() + "_replaced");
      while(!alloca->use_empty()) {
          Use& U = *alloca->use_begin();
          if (auto I = dyn_cast<Instruction>(U.getUser())) {
            if (I->getParent()->getParent() == &F) {
              I->replaceUsesOfWith(alloca, replCasted);
              sensitiveSet.insert(I);
            }
          }
      }
      alloca->replaceAllUsesWith(replCasted);
      alloca->eraseFromParent();
      sensitiveSet.insert(replCasted);
      sensitiveSet.insert(repl);
      replacementMallocs.push_back(repl);
    }

    // find every return statement
    vector<ReturnInst*> returns;
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
      Instruction *I = &*(It++);
      if (auto ret = dyn_cast<ReturnInst>(I)){
        returns.push_back(ret);
      } else if (auto CI = dyn_cast<CallInst>(I)) {
        // setjmps require stack restore.
        if (CI->getCalledFunction() && CI->canReturnTwice())
          llvm_unreachable("setjumps not implemented");
      } else if (auto LP = dyn_cast<LandingPadInst>(I)) {
        // TODO do we need to do anything here? we're not really using safe stack
        // Exception landing pads require stack restore.
        dbgs() << "[DATASHIELD] not doing anything for landing pad.  hopefully that is correct?\n";
        LP->dump();
        //llvm_unreachable("exception landing pads not implemented: ");
        //returns.push_back(LP);
      } else if (auto II = dyn_cast<IntrinsicInst>(I)) {
        if (II->getIntrinsicID() == Intrinsic::gcroot)
          II->dump();
        llvm::report_fatal_error(
              "gcroot intrinsic not compatible with safestack attribute");
      }
    }

    // insert __ds_safe_free calls before every return instruction
    for (auto ret : returns) {
      for (auto repl : replacementMallocs) {
        IRBuilder<> IRB(ret);
        if (DebugMode) {
            auto id = ConstantInt::get(int64Ty, IDCounter++);
            IRB.CreateCall(safeDealloc, {repl, id});
        } else {
            IRB.CreateCall(safeFree, repl);
        }
      }
    }
  }
  void replaceByValSensitiveArguments(Module&M, Function& F, DataLayout& DL) {
    for (auto& a : F.getArgumentList()) {
      if (a.hasByValAttr() && sensitiveSet.count(&a)) {
        IRBuilder<> IRB(&*F.getEntryBlock().getFirstInsertionPt());
        auto typeSize = DL.getTypeAllocSize(dyn_cast<PointerType>(a.getType())->getElementType());
        auto typeSizeVal = ConstantInt::get(int64Ty, typeSize);
        auto repl = IRB.CreateCall(safeMalloc, {typeSizeVal}, a.getName() + "_replaced");
        auto casted = IRB.CreateBitCast(repl, a.getType());
        auto origCasted = IRB.CreateBitCast(&a, int8PtrTy);
        auto cpy = IRB.CreateCall(safeMemCpy, {repl, origCasted, typeSizeVal});
        sensitiveSet.insert(repl);
        sensitiveSet.insert(casted);
        vector<Use*> toReplace;
        for (auto& U : a.uses()) {
          if (U.getUser() != origCasted && U.getUser() != cpy) {
            toReplace.push_back(&U);
          }
        }
        for (auto& U : toReplace) {
          if (auto I = dyn_cast<Instruction>(U->getUser())) {
            I->replaceUsesOfWith(&a, casted);
            sensitiveSet.insert(I);
          }
        }
        auto AS = AttributeSet::get(M.getContext(), a.getArgNo(), {Attribute::ByVal});
        a.removeAttr(AS);
        for (auto& otherF : F.uses()) {
          otherF.getUser()->dump();
          if (auto call = dyn_cast<CallInst>(otherF.getUser())) {
            for (unsigned i = 0; i < call->getNumArgOperands() + 1; ++i) {
              if (call->paramHasAttr(i, Attribute::ByVal) && i == a.getArgNo() + 1) {
                //llvm_unreachable("we found it!");
                call->removeAttribute(i, Attribute::get(M.getContext(), Attribute::ByVal));
              }
            }
          }
        }
      }
    }
  }

  MemoryRegioner(Module& M, ValueSet& sens) : sensitiveSet(sens) {
    getRuntimeFunctions(M);
  }
};

class Sandboxer {
  private:
  MDNode* maskMD = nullptr;
  StringRef maskMDString = "mask";
  FunctionType* cloneFnArgTy;
  Function* buildStackSwitch = nullptr;
  Function* cloneFn = nullptr;
  Function* dsUnsafeCopyArgv = nullptr;
  Function* dsGetStackPtr = nullptr;
  Function* dsWait = nullptr;
  Function* copyEnvironToUnsafe = nullptr;
  Function* copyEnvironToSafe = nullptr;
  Function* unsafeGetTemporaryBuffer = nullptr;
  Function* safeCopyArgv = nullptr;
  const size_t mask = (1ull << 32) -1;
  void getRuntimeFunctions(Module& M) {
    auto switchStacksTy = FunctionType::get(int32Ty, {int32Ty, int8PtrPtrTy}, false);
    buildStackSwitch = dyn_cast<Function>(M.getOrInsertFunction("__ds_switch_stack", switchStacksTy));
    assert(buildStackSwitch && "function shouldnt be null!");

    cloneFnArgTy = FunctionType::get(int32Ty, {int8PtrTy}, false);
    auto fptrTy = cloneFnArgTy->getPointerTo();
    auto cloneTy = FunctionType::get(int32Ty, {fptrTy, int8PtrTy, int32Ty, int8PtrTy}, true);
    cloneFn = dyn_cast<Function>(M.getOrInsertFunction("clone", cloneTy));
    assert(cloneFn && "function shouldnt be null!");

    auto getStackPtrTy = FunctionType::get(int8PtrTy, {});
    dsGetStackPtr = dyn_cast<Function>(M.getOrInsertFunction("__ds_get_unsafe_stack_top", getStackPtrTy));
    assert(dsGetStackPtr && "should be able to get rt functions");

    auto dsCopyArgvTy = FunctionType::get(int8PtrPtrTy, {int32Ty, int8PtrPtrTy}, false);
    dsUnsafeCopyArgv = dyn_cast<Function>(M.getOrInsertFunction("__ds_copy_argv_to_unsafe_heap", dsCopyArgvTy));
    assert(dsUnsafeCopyArgv && "should be able to get rt functions");

    safeCopyArgv = dyn_cast<Function>(M.getOrInsertFunction("__ds_copy_argv_to_safe_heap", dsCopyArgvTy));
    assert(safeCopyArgv && "should be able to get rt functions");

    auto dsWaitTy = FunctionType::get(int32Ty, {int32Ty}, false);
    dsWait = dyn_cast<Function>(M.getOrInsertFunction("__ds_waitpid", dsWaitTy));
    assert(dsWait && "should be able to get rt functions");

    auto copyEnvironToUnsafeTy = FunctionType::get(int8PtrPtrTy, {int8PtrPtrTy}, false);
    copyEnvironToUnsafe = dyn_cast<Function>(M.getOrInsertFunction("__ds_copy_environ_to_unsafe", copyEnvironToUnsafeTy));
    assert(copyEnvironToUnsafe && "should be able to get rt functions");

    copyEnvironToSafe = dyn_cast<Function>(M.getOrInsertFunction("__ds_copy_environ_to_safe", copyEnvironToUnsafeTy));
    assert(copyEnvironToSafe && "should be able to get rt functions");

    auto unsafeGetTemporaryBufferTy = FunctionType::get(voidTy, {int8PtrTy, int8PtrTy, int8PtrTy, int64Ty}, false);
    unsafeGetTemporaryBuffer = dyn_cast<Function>(M.getOrInsertFunction("__ds_unsafe_get_temporary_buffer", unsafeGetTemporaryBufferTy));
    assert(unsafeGetTemporaryBuffer && "should be able to get rt functions");
  }
  void replaceEnviron(Module& M, CallInst* copyCall, LoadInst* origEnvironVal) {
    auto dsEnviron = getOrCreateCharPtrPtrGlobal(M, "__ds_environ");
    auto environ = M.getGlobalVariable("environ");
    if (!environ) {
      // this module doesn't use environ
      return;
    } else {
      vector<User*> worklist(environ->user_begin(), environ->user_end());
      for (auto& U : worklist) {
        if (U != copyCall && U != origEnvironVal) {
          if (auto I = dyn_cast<Instruction>(U)) {
            I->replaceUsesOfWith(environ, dsEnviron);
          } else if (auto cast = dyn_cast<Operator>(U)) {
            replaceOperator(cast, dsEnviron);
          } else {
            llvm_unreachable("dont know how to replace this");
          }
        }
      }
    }
  }
  void writeNewMain(Module& M, Function& oldMain, Function& newMain) {
    auto block = BasicBlock::Create(M.getContext(), "entry", &newMain);
    IRBuilder<> IRB(block);
    // copy environ to unsafe heap
    Value* environ = M.getGlobalVariable("environ");
    if (environ) {
      auto environLoaded = IRB.CreateLoad(environ);
      auto copyCall = IRB.CreateCall(copyEnvironToUnsafe, {environLoaded});
      replaceEnviron(M, copyCall, environLoaded);
    } else if (newMain.getArgumentList().size() == 3) {
      environ = &newMain.getArgumentList().back();
      IRB.CreateCall(copyEnvironToUnsafe, {environ});
      // we don't need to replace environ in this case because
      // we know environ wasnt used except through main()'s 3rd arg
      // because environ was null
    } // else environ isn't used
    auto switchStacksFn = buildSwitchStacksFunction(M, oldMain);
    Value* mainCopyRetVal = nullptr;
    if (newMain.getArgumentList().size() == 0) {
      auto zero = ConstantInt::get(int32Ty, 0);
      auto nullPtr = ConstantPointerNull::get(int8PtrPtrTy);
      mainCopyRetVal= IRB.CreateCall(switchStacksFn, {zero, nullPtr});
    } else {
      auto argc = &*newMain.arg_begin();
      auto argv = &*++newMain.arg_begin();
      mainCopyRetVal= IRB.CreateCall(switchStacksFn, {argc, argv});
    }
    IRB.CreateRet(mainCopyRetVal);
  }
  Function* buildSwitchStacksFunction(Module& M, Function& oldMain) {
    auto mainArgTy = StructType::create(M.getContext(), {int32Ty, int8PtrPtrTy}, "mainArgs", false);
    auto block = BasicBlock::Create(M.getContext(), "entry", buildStackSwitch);
    IRBuilder<> IRB(block);
    auto stack_top = IRB.CreateCall(dsGetStackPtr, {});

    auto argc = &*buildStackSwitch->arg_begin();
    auto argv = &*++buildStackSwitch->arg_begin();

    // copy argv to unsafe heap
    auto copiedArgv = IRB.CreateCall(dsUnsafeCopyArgv, {argc, argv});

    // set up the trampoline argument
    auto mainArgsPtr = IRB.CreateAlloca(mainArgTy);
    auto argc_idx =  IRB.CreateConstInBoundsGEP2_32(mainArgTy, mainArgsPtr, 0, 0, "argc_idx");
    auto argv_idx =  IRB.CreateConstInBoundsGEP2_32(mainArgTy, mainArgsPtr, 0, 1, "argv_idx");
    IRB.CreateStore(argc, argc_idx);
    IRB.CreateStore(copiedArgv, argv_idx);
    auto argsAsVoidPtr = IRB.CreateBitCast(mainArgsPtr, int8PtrTy);

    // call the trampoline
    auto tramp = createTrampoline(M, *cloneFnArgTy, oldMain, *mainArgTy);
    //auto flags = ConstantInt::get(int32Ty, 8465); // SIGCHLD | CLONE_PTRACE | CLONE_VM
    auto flags = ConstantInt::get(int32Ty, -2147473647); // SIGCHLD | CLONE_PTRACE | CLONE_VM | CLONE_IO | CLONE_FS | CLONE_FILES
    auto pid = IRB.CreateCall(cloneFn, {tramp, stack_top, flags, argsAsVoidPtr}, "cloneMain");

    // wait, get our exit code, and return
    auto exitCode = IRB.CreateCall(dsWait, {pid}, "exit_code");

    IRB.CreateRet(exitCode);

    return buildStackSwitch;
  }
  Function* createTrampoline(Module& M, FunctionType& trampolineTy, Function& mainCopy, Type& argsStructTy) {
    auto tramp = cast<Function>(M.getOrInsertFunction("__ds_trampoline", &trampolineTy));
    assert(tramp && "creating trampoline function should succeed");
    auto block = BasicBlock::Create(M.getContext(), "entry", tramp);
    IRBuilder<> IRB(block);
    auto arg1 = &*tramp->arg_begin();
    auto argsStruct = IRB.CreateBitCast(arg1, argsStructTy.getPointerTo());
    auto argsStructV = IRB.CreateLoad(argsStruct);
    auto argc = IRB.CreateExtractValue(argsStructV, (uint64_t)0, "argc");
    auto argv = IRB.CreateExtractValue(argsStructV, (uint64_t)1ull, "argv");
    Value* copyMainRV = nullptr;
    if (mainCopy.getArgumentList().size() == 3) {
      auto copiedEnviron = getOrCreateCharPtrPtrGlobal(M, "__ds_environ");
      auto copiedEnvironV = IRB.CreateLoad(copiedEnviron);
      copyMainRV = IRB.CreateCall(&mainCopy, {argc, argv, copiedEnvironV});
    } else if (mainCopy.getArgumentList().size() == 2) {
      copyMainRV = IRB.CreateCall(&mainCopy, {argc, argv});
    } else {
      // int main(void)
      copyMainRV = IRB.CreateCall(&mainCopy, {});
    }
    IRB.CreateRet(copyMainRV);
    return tramp;
  }
  template<typename T>
  void insertUnsafeBoundsCheckMPX(T* instruction) {
    IRBuilder<> IRB(instruction);
    auto ptrOp = instruction->getPointerOperand();
    auto ptrOpAsVoidPtr = IRB.CreateBitCast(ptrOp, int8PtrTy, "as_void_star");
    if (DebugMode) {
      llvm_unreachable("unsafe bounds check mpx not implemented in debug mode");
    } else {
      //IRB.CreateCall(unsafeBoundsCheckMPX, {ptrOpAsVoidPtr});
      auto checkTy = FunctionType::get(voidTy, {int8PtrTy}, false);
      auto checkAsm = InlineAsm::get(checkTy, "bndcu $0, %bnd0", "r,~{dirflag},~{fpsr},~{flags}", true);
      //auto checkAsm = InlineAsm::get(checkTy, "bndcu $0, %bnd0", "r,~{dirflag},~{fpsr},~{flags}", false);
      IRB.CreateCall(checkAsm, ptrOpAsVoidPtr);
    }
  }

  bool isCtype(Type* ty) {
      static map<Type*, bool> isCtypeMemo;
      if (isCtypeMemo.find(ty) != isCtypeMemo.end()) {
          return isCtypeMemo[ty];
      }
      if (ty->isPointerTy()) {
        auto elety = ty->getPointerElementType();
        auto rv = isCtype(elety);
        isCtypeMemo[ty] = rv;
        return rv;
      }
      if (auto sty = dyn_cast<StructType>(ty)) {
        if (!sty->isLiteral() && sty->getStructName().startswith("class.std::")) {
          isCtypeMemo[sty] = true;
          return true;
        }
      }
      isCtypeMemo[ty] = false;
      return false;
  }

  bool isFunctionType(Type* ty) {
      static map<Type*, bool> isFunctionTypeMemo;
      if (isFunctionTypeMemo.find(ty) != isFunctionTypeMemo.end()) {
          return isFunctionTypeMemo[ty];
      }
      if (ty->isPointerTy()) {
        auto elety = ty->getPointerElementType();
        auto rv = isFunctionType(elety);
        isFunctionTypeMemo[ty] = rv;
        return rv;
      }
      if (ty->isFunctionTy()) {
        isFunctionTypeMemo[ty] = true;
        return true;
      }
      isFunctionTypeMemo[ty] = false;
      return false;
  }

  template<typename T>
  bool shouldMask(T* instruction, ValueSet& sensitiveSet) {
    auto ptrOp = instruction->getPointerOperand();

    if (sensitiveSet.count(ptrOp) != 0) {
      return false;
    }
    if (isa<GlobalValue>(ptrOp)) {
      // since global variables are constant pointers,
      // directly loading/storing them is safe
      return false;
    }
    return true;
  }

  template<typename T>
  Value* maskPtr(T* instruction) {
    IRBuilder<> IRB(instruction);
    auto ptrOp = instruction->getPointerOperand();

    Value* maskedPtrOp = nullptr;
    if (DebugMode) {
      auto id = ConstantInt::get(int64Ty, IDCounter++);
      auto castedPtr = IRB.CreateBitCast(ptrOp, int8PtrTy, ptrOp->getName() + "_casted");
      auto maskedPtrOpVoid = IRB.CreateCall(maskDebug, {castedPtr, id}, ptrOp->getName() + "_as_void_sar_masked");
      maskedPtrOp =  IRB.CreateBitCast(maskedPtrOpVoid, ptrOp->getType(), "_masked");
    } else {
      auto intOp = IRB.CreatePtrToInt(ptrOp, int64Ty, ptrOp->getName() + "_as_int");
      auto maskedIntOp = IRB.CreateAnd(intOp, mask, ptrOp->getName() + "_as_int_masked");
      auto origType = ptrOp->getType();
      maskedPtrOp = IRB.CreateIntToPtr(maskedIntOp, origType, ptrOp->getName() + "_masked");
    }
    return maskedPtrOp;
  }
  public:
  void copyAndReplaceArgvIfNecessary(Module& M, ValueSet& sensitiveSet) {
    auto main = M.getFunction("main");
    if (main == nullptr) { llvm_unreachable("should be able to find main function"); }
    auto args = main->arg_begin();
    auto argc = &*args;
    auto argv = &*(++args);
    if (sensitiveSet.count(argv)) {
      IRBuilder<> IRB(&*main->getEntryBlock().getFirstInsertionPt());
      auto argv_repl = IRB.CreateCall(safeCopyArgv, {argc, argv}, "argv_repl");
      vector<User*> argv_users(argv->user_begin(), argv->user_end());
      for (auto &u : argv_users) {
        if (u != argv_repl) {
          u->replaceUsesOfWith(argv, argv_repl);
        }
      }
    }
  }
  void copyAndReplaceEnviron(Module& M, ValueSet& sensitiveSet) {
    Function* main = M.getFunction("main");
    if (!main) {
      llvm_unreachable("we should be able to find main with LTO");
    }
    auto dsEnviron = getOrCreateCharPtrPtrGlobal(M, "__ds_environ");
    Value* environ = M.getGlobalVariable("environ");
    if (!environ) { return; }
    IRBuilder<> IRB(&*main->getEntryBlock().getFirstInsertionPt());
    auto environLoaded = IRB.CreateLoad(environ);
    CallInst* copyCall;
    if (environ && !sensitiveSet.count(environ)) {
      copyCall = IRB.CreateCall(copyEnvironToUnsafe, {environLoaded});
    } else {
      copyCall = IRB.CreateCall(copyEnvironToSafe, {environLoaded});
      sensitiveSet.insert(dsEnviron);
      sensitiveSet.insert(copyCall);
    }
    IRB.CreateStore(copyCall, dsEnviron);
    replaceEnviron(M, copyCall, environLoaded);
  }

  Function* origMain;
  void insertPointerMasks(Function& F, ValueSet& sensitiveSet) {
    //DEBUG(dbgs() << "[Sandboxer] in function: " << F.getName() << "\n");
    ReplacementMap replacementMap;
    if (UsePrefix && !F.isDeclaration()) {
        F.setMetadata(maskMDString, maskMD);
        return;
    }
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
      Instruction *I = &*(It++);
      if (auto load = dyn_cast<LoadInst>(I)) {
        if (IntegrityOnlyMode) { continue; }
        if (sensitiveSet.count(load->getPointerOperand())) { continue; }
        if (!shouldMask(load, sensitiveSet)) { continue; }
        if (UseMPX) {
            insertUnsafeBoundsCheckMPX(load);
        } else if (UseMask) {
          auto maskedPtr = maskPtr(load);
          DEBUG(dbgs() << "[Sandboxer] masked load: "; load->dump());
          IRBuilder<> IRB(load);
          auto newload = IRB.CreateAlignedLoad(maskedPtr,
              load->getAlignment(),
              load->getName());
          replacementMap.push_back(pair<Instruction*, Instruction*>(load, newload));
        }
      }
      if (auto store = dyn_cast<StoreInst>(I)) {
        if (ConfidentialityOnlyMode) { continue; }
        if (sensitiveSet.count(store->getPointerOperand())) { continue; }
        if (!shouldMask(store, sensitiveSet)) { continue; }
        if (UseMPX) {
            insertUnsafeBoundsCheckMPX(store);
        } else if (UseMask) {
          auto maskedPtr = maskPtr(store);
          DEBUG(dbgs() << "[Sandboxer] masked store: "; store->dump());
          auto valOp = store->getValueOperand();
          IRBuilder<> IRB(store);
          auto newstore = IRB.CreateAlignedStore(valOp,
              maskedPtr,
              store->getAlignment(),
              store->isVolatile());
          replacementMap.push_back(pair<Instruction*, Instruction*>(store, newstore));
        }
      }
    }
    replacementMap.replaceAndErase();
  }
  Sandboxer(Module& M) {
    maskMD = MDNode::get(M.getContext(), MDString::get(M.getContext(), maskMDString));
    getRuntimeFunctions(M);
  }

}; // end class Sandboxer

class SensitivityAnalysis {
  private:
  bool anySensitiveOperands(Instruction* I) {
    if (sensitiveSet.count(I)) { return true; }
    for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
      auto val = I->getOperand(i);
      if (sensitiveSet.count(val)) {
          //dbgs() << "*** was sensitive: ";
          //val->dump();
        return true;
      }
    }
    return false;
  }
  void propagateSensitivity(Function& F) {
    bool changed;
    do {
      changed = false;
      for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie;) {
        Instruction *I = &*(It++);
        if (isa<BranchInst>(I)) { continue; } // ignore these
        if (auto call = dyn_cast<CallInst>(I)) {
          // allow the operands of memcpy to progagate sensitivity
          if (call->getCalledFunction() && call->getCalledFunction()->getName().startswith("llvm.memcpy")) {
            if (sensitiveSet.count(call->getArgOperand(0)) || sensitiveSet.count(call->getArgOperand(1))) {
              changed |= sensitiveSet.insert(call->getArgOperand(0)).second;
              changed |= sensitiveSet.insert(call->getArgOperand(1)).second;
              continue;
            }
          } else if (call->getCalledFunction() && call->getCalledFunction()->getName().startswith("strchr")) {
            if (sensitiveSet.count(call->getArgOperand((0))) || sensitiveSet.count(call)) {
              changed |= sensitiveSet.insert(call->getArgOperand(0)).second;
              changed |= sensitiveSet.insert(call).second;
              continue;
            }
          } else {
            continue;
          }
        } // we allow CallInst with mixed sensitivity so dont propagate them
        if (UseSeparationMode) {
          if (I->getOpcode() == Instruction::Add) { continue; }
          if (I->getOpcode() == Instruction::FAdd) { continue; }
          if (I->getOpcode() == Instruction::Sub) { continue; }
          if (I->getOpcode() == Instruction::FSub) { continue; }
          if (I->getOpcode() == Instruction::Mul) { continue; }
          if (I->getOpcode() == Instruction::FMul) { continue; }
          if (I->getOpcode() == Instruction::UDiv) { continue; }
          if (I->getOpcode() == Instruction::SDiv) { continue; }
          if (I->getOpcode() == Instruction::FDiv) { continue; }
          if (I->getOpcode() == Instruction::URem) { continue; }
          if (I->getOpcode() == Instruction::SRem) { continue; }
          if (I->getOpcode() == Instruction::FRem) { continue; }
          if (I->getOpcode() == Instruction::And) { continue; }
          if (I->getOpcode() == Instruction::Or) { continue; }
          if (I->getOpcode() == Instruction::Xor) { continue; }
        }
        if (anySensitiveOperands(I)) {
          changed |= sensitiveSet.insert(I).second;
          for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
            auto v = I->getOperand(i);
            changed |= sensitiveSet.insert(v).second;
          }
        }
      }
    } while (changed);
  }
  void propagateSensitivity(Module& M) {
    for (auto& F : M) {
      if (!isWhiteListed(F)) {
        //dbgs() << "scanning: " << F.getName() << "\n";
        propagateSensitivity(F);
        //sensitiveSet.dump();
      }
    }
  }
  void makeReturnsSensitive(Function* F) {
    for (inst_iterator It = inst_begin(F), Ie = inst_end(F); It != Ie;) {
      Instruction *I = &*(It++);
      if (auto ret = dyn_cast<ReturnInst>(I)) {
        if(auto rv = ret->getReturnValue()) {
          sensitiveSet.insert(rv);
        }
      }
    }
  }
  void propagateAcrossCallBoundary(Function* F, CallInfo& info) {
    // propagate sensitivity across caller/callee boundary in both directions
    if (info.isReturnValSensitive() || info.isReturnTypeSensitive()) {
      makeReturnsSensitive(F);
      sensitiveSet.insert(F);
      if (info.isDirectCall()) {
        sensitiveSet.insert(info.getCallee());
      }
    }
    auto it = F->arg_begin();
    for (unsigned i = 0, N = info.getNumArgs(); i != N; ++i, it++) {
      if (info.isArgSensitive(i) || info.isParamTypeSensitive(i) || info.isArgSensitiveAtCallee(i)) {
        sensitiveSet.insert(&*it);
        auto arg = info.callSite.getArgOperand(i);
        sensitiveSet.insert(arg);
      }
    }
  }
  void makeNthArgSensitive(Function& fn, unsigned n) {
    unsigned i = 0;
    auto arg = fn.getArgumentList().begin();
    for (i = 0; i < n; ++i) {
      arg++;
    }
    sensitiveSet.insert(&*arg);
  }
  void makeNthArgSensitive(StringRef fnName, unsigned n) {
    auto fn = M.getFunction(fnName);
    unsigned i = 0;
    if (fn) {
      auto arg = fn->getArgumentList().begin();
      for (i = 0; i < n; ++i) {
          arg++;
      }
      sensitiveSet.insert(&*arg);
    }
  }
  void makeFirstArgSensitive(StringRef fnName) {
    auto fn = M.getFunction(fnName);
    if (fn) {
      sensitiveSet.insert(&*fn->getArgumentList().begin());
    }
  }
  void makeFirstTwoArgSensitive(StringRef fnName) {
    auto fn = M.getFunction(fnName);
    if (fn) {
      sensitiveSet.insert(&*fn->getArgumentList().begin());
      sensitiveSet.insert(&*(++fn->getArgumentList().begin()));
    }
  }
  void makeThirdArgSensitive(StringRef fnName) {
    auto fn = M.getFunction(fnName);
    if (fn) {
      sensitiveSet.insert(&*(++++fn->getArgumentList().begin()));
    }
  }

  Function* cloneFreeFunction(Module& M, StringRef origName) {
    auto fn = M.getFunction(origName);
    if (fn) {
      vector<int> sens {0, 1};
      return cloneFunctionWithSensitivity(M, origName, sens);
    }
    return nullptr;
  }

  Function* cloneAllocFunction(Module& M, StringRef origName) {
    auto fn = M.getFunction(origName);
    if (fn) {
      vector<int> sens {1};
      return cloneFunctionWithSensitivity(M, origName, sens);
    }
    return nullptr;
  }

  Function* cloneFunctionWithSensitivity(Module& M, StringRef origName, vector<int>& sensitivity) {
    auto origFn = M.getFunction(origName);
    if (origFn) {
      stringstream suffix;
      if (sensitivity [0] == 1) {
        suffix << "_1";
      } else {
        suffix << "_0";
      }
      suffix << "_";
      for (unsigned i = 1; i < sensitivity.size(); ++i) {
        if (sensitivity[i] == 1) {
          suffix << "1";
        } else {
          suffix << "0";
        }
      }
      auto newName = origName.str() + suffix.str();
      auto newFn = duplicateFunction(M, *origFn, newName);
      if (sensitivity[0] == 1) {
        makeReturnsSensitive(newFn);
      }
      for (unsigned i = 1; i < sensitivity.size(); ++i) {
        makeNthArgSensitive(*newFn, i-1);
      }
      newFunctions.insert(newFn);
      return newFn;
    }
    return nullptr;
  }
  void doTLSPRFhack() {
    auto F = M.getFunction("mbedtls_ssl_derive_keys_1_1");
    if (!F) { return; }
    dbgs() << "mbedtls_ssl_derive_keys_1_1 found!\n";
    for (inst_iterator It = inst_begin(F), Ie = inst_end(F); It != Ie; ++It) {
      auto i = &*It;
      if (auto call = dyn_cast<CallInst>(i)) {
        auto calledFn = call->getCalledFunction();
        auto tls_prfTy = FunctionType::get(int32Ty, {int8PtrTy, int64Ty, int8PtrTy, int8PtrTy, int64Ty, int8PtrTy, int64Ty}, false)->getPointerTo();
        if (!calledFn
            && call->getCalledValue()
            && call->getCalledValue()->getType() == tls_prfTy
            )
        {
          if (auto load = dyn_cast<LoadInst>(call->getCalledValue())) {
            if (load->getPointerOperand()->hasName() && load->getPointerOperand()->getName().startswith("tls_prf"))
              dbgs() << "did tls_prf hack!\n";
              sensitiveSet.insert(call->getArgOperand(2));
          }
        }
      }
    }
  }
  void makeNamedVarSensitiveInFunction(StringRef varName, StringRef fnName) {
    auto fn = M.getFunction(fnName);
    if (!fn) { return; }
    for (inst_iterator It = inst_begin(fn), Ie = inst_end(fn); It != Ie; ++It) {
      auto i = &*It;
      if (i->hasName() && i->getName() == varName) {
        sensitiveSet.insert(i);
      }
    }
    propagateSensitivity(*fn);
  }

  public:
  set<Function*> newFunctions;
  FunctionToGlobalMapTy functionToGlobalMap;
  Module& M;
  CallInfoContainer callInfos;
  TypeSet sensitiveTypes;
  ValueSet sensitiveSet;
  SensitivityAnalysis(Module& M) : M(M), callInfos(M, functionToGlobalMap), sensitiveTypes(M), sensitiveSet(M, sensitiveTypes) {}
  void analyzeModule() {
    sensitiveTypes.dump();
    //sensitiveSet.dump();

    for (auto& F : M) {
      sensitiveSet.findSensitiveTypedValues(F);
    }

    propagateSensitivity(M);
    callInfos.makeCallInfoForEachCallInst(M, sensitiveSet, sensitiveTypes);

    vector<Function*> newFsThisLoop;
    do {
      sensitiveSet.isChanged = false;
      newFsThisLoop.clear();
      propagateSensitivity(M);
      for (auto& acall : callInfos.data) {
        if (acall.isDirectCall() && !acall.isCallToExternalFunction()) {
          DEBUG(dbgs() << "do we need a new function for: " << acall.getCallee()->getName() << "\n");
          if (acall.needsNewFunction()) {
            DEBUG(dbgs() << "yes\n");
          } else {
            DEBUG(dbgs() << "no\n");
          }
        }
        if (acall.needsNewFunction() && acall.isDirectCall() && !acall.isCallToExternalFunction()) {
          DEBUG(dbgs() << "duplicating: " << acall.getCallee()->getName() + acall.getSignatureString() << "\n");
          auto newName = acall.getCallee()->getName().str() + acall.getSignatureString();
          if (auto existingF = findFunctionWithSameName(newFunctions, newName)) {
            DEBUG(dbgs() << "we already had it\n");
            //if (acall.replacement != existingF) {
              acall.replacement = existingF; // we already cloned the appropriate function
              propagateAcrossCallBoundary(existingF, acall);
              propagateSensitivity(acall.parent);
            //}
          } else {
            DEBUG(dbgs() << "made new function\n");
            auto newF = duplicateFunction(M, *acall.getCallee(), newName);
            acall.replacement = newF;
            //replacedFunctions.insert(acall.getCallee());
            DEBUG(dbgs() << newF->getName() << "\n");
            if (UseMbedtlsAnnotations && newName == "x509_crt_verify_top_0_11111000") {
              makeNamedVarSensitiveInFunction("hash", "x509_crt_verify_top_0_11111000");
            }
            if (UseMbedtlsAnnotations && newName == "x509_crt_verify_top_1_11111111") {
              makeNamedVarSensitiveInFunction("hash", "x509_crt_verify_top_1_11111111");
            }
            propagateAcrossCallBoundary(newF, acall);
            propagateSensitivity(*newF);
            propagateSensitivity(acall.parent);
            newFsThisLoop.push_back(newF);
            newFunctions.insert(newF);
          }
        } 
      }
      for (auto newF : newFsThisLoop) {
        sensitiveSet.findSensitiveTypedValues(*newF);
        propagateSensitivity(*newF);
        callInfos.makeCallInfoForEachCallInst(*newF, sensitiveSet, sensitiveTypes);
      }
    } while (sensitiveSet.isChanged);

    if (UseMbedtlsAnnotations) {
      doTLSPRFhack();
      makeNamedVarSensitiveInFunction("buf", "ssl_ticket_gen_key_0_11");
      makeNamedVarSensitiveInFunction("tmp", "mbedtls_md_hmac_finish_0_11");
      makeNamedVarSensitiveInFunction("tmp.i", "tls_prf_generic_0_11111111");
    }

  }
}; // end class SensitivityAnalysis


class BasedOnValues {
public:
  vector<Value*> values;
  vector<BasicBlock*> edges;
  PHINode* phiNode = nullptr;
  BasedOnValues(vector<Value*> values, vector<BasicBlock*> edges) :
    values(values), edges(edges) {}
  BasedOnValues() {}
}; // end class BasedOnValues

class BoundsAnalysis {
  Module& M;
  const ValueSet& sensitiveSet;
  Function *setBoundsDebug, *getBoundsDebug, *setFnArgBoundsDebug, *getFnArgBoundsDebug, *abortDebug;
  Function *setBounds, *getBounds, *setFnArgBounds, *getFnArgBounds, *abortFn, *dsSafeCopyArgv;
  Constant* infiniteBounds;
  Constant* emptyBounds;
  BoundsMap globalBoundsMap;
  void getRuntimeFunctions() {

    // debug versions
    auto setBoundsDebugTy = FunctionType::get(voidTy, {int8PtrTy, boundsTy, int8PtrTy, int64Ty}, false);
    setBoundsDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_set_bounds_debug", setBoundsDebugTy));
    assert(setBoundsDebug && "should be able to get runtime functions");

    auto getBoundsDebugTy = FunctionType::get(boundsTy, {int8PtrTy, int8PtrTy, int64Ty}, false);
    getBoundsDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_get_bounds_debug", getBoundsDebugTy));
    assert(getBoundsDebug && "should be able to get runtime functions");

    auto getFnArgBoundsDebugTy = FunctionType::get(boundsTy, {int64Ty, int8PtrTy, int64Ty}, false);
    getFnArgBoundsDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_get_fn_arg_bounds_debug", getFnArgBoundsDebugTy));
    assert(getFnArgBoundsDebug && "should be able to get runtime functions");

    auto setFnArgBoundsDebugTy = FunctionType::get(voidTy, {int64Ty, boundsTy, int8PtrTy}, false);
    setFnArgBoundsDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_set_fn_arg_bounds_debug", setFnArgBoundsDebugTy));
    assert(setFnArgBoundsDebug && "should be able to get runtime functions");

    auto abortDebugTy = FunctionType::get(voidTy, {boundsTy, int8PtrTy, int8PtrTy, int8PtrTy, int64Ty}, false);
    abortDebug = dyn_cast<Function>(M.getOrInsertFunction("__ds_abort_debug", abortDebugTy));
    assert(abortDebug && "should be able to get runtime functions");

    // non debug versions
    auto getFnArgBoundsTy = FunctionType::get(boundsTy, {int64Ty}, false);
    getFnArgBounds = dyn_cast<Function>(M.getOrInsertFunction("__ds_get_fn_arg_bounds", getFnArgBoundsTy));
    assert(getFnArgBounds && "should be able to get runtime functions");

    auto setFnArgBoundsTy = FunctionType::get(voidTy, {int64Ty, boundsTy}, false);
    setFnArgBounds = dyn_cast<Function>(M.getOrInsertFunction("__ds_set_fn_arg_bounds", setFnArgBoundsTy));
    assert(setFnArgBounds && "should be able to get runtime functions");

    auto setBoundsTy = FunctionType::get(voidTy, {int8PtrTy, boundsTy}, false);
    setBounds = dyn_cast<Function>(M.getOrInsertFunction("__ds_set_bounds", setBoundsTy));
    assert(setBounds && "should be able to get runtime functions");

    auto getBoundsTy = FunctionType::get(boundsTy, {int8PtrTy}, false);
    getBounds = dyn_cast<Function>(M.getOrInsertFunction("__ds_get_bounds", getBoundsTy));
    assert(getBounds && "should be able to get runtime functions");

    auto abortTy = FunctionType::get(voidTy, {}, false);
    abortFn = dyn_cast<Function>(M.getOrInsertFunction("__ds_abort", abortTy));
    assert(abortFn && "should be able to get runtime functions");

    auto dsCopyArgvTy = FunctionType::get(int8PtrPtrTy, {int32Ty, int8PtrPtrTy}, false);
    dsSafeCopyArgv = dyn_cast<Function>(M.getOrInsertFunction("__ds_copy_argv_to_safe_heap", dsCopyArgvTy));
    assert(dsSafeCopyArgv && "should be able to get rt functions");
  }
  void searchForGlobalsIn(Constant* curr, IRBuilder<>& IRB, Value* dbgStr, DataLayout& DL, set<Value*>& visited, Value* ptrAddr = nullptr) {
      if (visited.count(curr)) { return; }
      visited.insert(curr);
      if (auto cs = dyn_cast<ConstantStruct>(curr)) {
        //dbgs() << "is constant struct\n";
        auto structTy = cs->getType();
        auto structLayout =  DL.getStructLayout(structTy);
        //auto globalAsInt8Ptr = IRB.CreateBitCast(curr, int8PtrTy, curr->getName() + "_as_int8_star");
        for (unsigned i = 0, e = cs->getNumOperands(); i != e; ++i) {
          auto ele = cs->getOperand(i);
          if (auto ce = dyn_cast<ConstantExpr>(ele)) {
            ele = ce->getOperand(0);
          }
          if (ele->getType()->isPointerTy() || ele->getType()->isAggregateType()) {
            auto off = structLayout->getElementOffset(i);
            auto offV = ConstantInt::get(int64Ty, off);
            auto eleAddr = IRB.CreateGEP(ptrAddr, offV, "element_address");
            if (auto nextGlobal = dyn_cast<GlobalVariable>(ele)) {
              setGlobalSection(nextGlobal);
              insertConstantBoundsStore(nextGlobal, IRB, dbgStr, DL, eleAddr);
              lookForBoundsInInitializer(nextGlobal, IRB, dbgStr, DL, visited, eleAddr);
            } else {
              // TODO we still need to set the bounds even if its not a named global
              // because you can assign a pointer to point to an unnamed element
              // we have to calculate the address of the element manually
              // and the bounds are the size of the element type
              insertConstantBoundsStore(ele, IRB, dbgStr, DL, eleAddr);
              searchForGlobalsIn(ele, IRB, dbgStr, DL, visited, eleAddr);
            }
          }
        }
      } else if (auto ce = dyn_cast<ConstantExpr>(curr)) {
        // TODO non all zeros GEP?
        auto op0 = ce->getOperand(0);
        if (op0->getType()->isPointerTy() || op0->getType()->isAggregateType()) {
          if (auto nextGlobal = dyn_cast<GlobalVariable>(op0)) {
            setGlobalSection(nextGlobal);
            insertConstantBoundsStore(nextGlobal, IRB, dbgStr, DL, ptrAddr);
            lookForBoundsInInitializer(nextGlobal, IRB, dbgStr, DL, visited, ptrAddr);
          } else {
            insertConstantBoundsStore(op0, IRB, dbgStr, DL, op0);
            searchForGlobalsIn(op0, IRB, dbgStr, DL, visited, op0);
          }
        }
      } else if (auto array = dyn_cast<ConstantArray>(curr)) {
        //dbgs() << "is constant data array\n";
        // loop over the elements and store their bounds if they are pointers
        auto ele0 = array->getAggregateElement(0u);
        auto ele0Ty = ele0->getType();
        if (ele0Ty->isPointerTy() || ele0Ty->isAggregateType()) {
          for (unsigned i = 0, e = array->getNumOperands(); i != e; ++i) {
            auto ele = array->getOperand(i);
            if (auto ce = dyn_cast<ConstantExpr>(ele)) {
              ele = ce->getOperand(0);
            }
            auto off = DL.getTypeStoreSize(ele0Ty);
            auto offV = ConstantInt::get(int64Ty, off * i);
            auto eleAddr = IRB.CreateGEP(ptrAddr, offV, "element_address");
            if (auto nextGlobal = dyn_cast<GlobalVariable>(ele)) {
              setGlobalSection(nextGlobal);
              insertConstantBoundsStore(nextGlobal, IRB, dbgStr, DL, eleAddr);
              lookForBoundsInInitializer(nextGlobal, IRB, dbgStr, DL, visited, eleAddr);
            } else {
              // TODO we still need to set the bounds even if its not a named global
              // because you can assign a pointer to point to an unnamed element
              // we have to calculate the address of the element manually
              // and the bounds are the size of the element type
              insertConstantBoundsStore(ele, IRB, dbgStr, DL, eleAddr);
              searchForGlobalsIn(ele, IRB, dbgStr, DL, visited, eleAddr);
            }
          }
        }
      }
  }

  void setGlobalSection(GlobalVariable* global) {
    if (global->getLinkage() == GlobalValue::LinkageTypes::ExternalLinkage) {
      if (global->getName() == "stdout") {
        return;
      } else {
        global->setSection(".sensitive1");
      }
    } else if (global->getLinkage() == GlobalValue::LinkageTypes::CommonLinkage) {
      global->setSection(".sensitive2");
      global->setAlignment(64); // i'm not 100% sure this is actually needed
    } else if (global->getLinkage() == GlobalValue::LinkageTypes::PrivateLinkage) {
      global->setSection(".sensitive3");
    } else if (global->getLinkage() == GlobalValue::LinkageTypes::InternalLinkage) {
      global->setSection(".sensitive4");
    } else {
      llvm_unreachable("it should have been one of those linkages?");
    }
  }
  bool isBasedOnExternal(Value* constant) {
    if (auto bc = dyn_cast<BitCastOperator>(constant)) {
      return isBasedOnExternal(bc->getOperand(0));
    }
    if (auto gep = dyn_cast<GEPOperator>(constant)) {
      return isBasedOnExternal(gep->getOperand(0));
    }
    if (auto global = dyn_cast<GlobalVariable>(constant)) {
      return global->getLinkage() == GlobalVariable::ExternalLinkage;
    }
    return false;
  }

  void insertConstantBoundsStore(Constant* constant, IRBuilder<>& IRB, Value* dbgStr, DataLayout& DL, Value* ptrAddr) {
    auto constantTy = constant->getType();
    Value *bounds = nullptr;
    DEBUG(dbgs() << "insertConstantBoundsStore: constant:\n");
    DEBUG(constant->dump());
    DEBUG(dbgs() << "ptrAddr:\n");
    DEBUG(ptrAddr->dump());
    if (isa<Function>(constant)) {
      bounds = infiniteBounds; // we do not do CFI but this could be a FIX ME
    } else if (isBasedOnExternal(constant)) {
      bounds = infiniteBounds;
    } else if (isa<ConstantPointerNull>(constant)) {
      bounds = emptyBounds;
    } else if (constantTy->isPointerTy() || constantTy->isAggregateType()) {
      if (auto pTy = dyn_cast<PointerType>(constantTy)) {
        constantTy = pTy->getElementType();
        auto size = DL.getTypeStoreSize(constant->getType());
        auto sizeV = ConstantInt::get(int64Ty, size);
        bounds = createBounds(IRB, constant, sizeV);
      } else {
        auto size = DL.getTypeStoreSize(constant->getType());
        auto sizeV = ConstantInt::get(int64Ty, size);
        bounds = createBounds(IRB, ptrAddr, sizeV);
      }
    } else {
      return; // don't store bounds for other things like scalars
    }
    auto id = ConstantInt::get(int64Ty, IDCounter++);
    if (DebugMode) {
      IRB.CreateCall(setBoundsDebug, {ptrAddr, bounds, dbgStr, id});
    } else {
      IRB.CreateCall(setBounds, {ptrAddr, bounds});
    }
  }
  void lookForBoundsInInitializer(GlobalVariable* global, IRBuilder<>& IRB, Value* dbgStr, DataLayout& DL, set<Value*>& visited, Value* ptrAddr = nullptr) {
    if (global->hasInitializer()) {
      auto init = global->getInitializer();
      auto baseAddr = IRB.CreateBitCast(global, int8PtrTy, "as_int8ptr");
      searchForGlobalsIn(init, IRB, dbgStr, DL, visited, baseAddr);
    }
  }
  void createGlobalBounds() {
    // for the top level globals a boundsMap is enough
    // but when we could a global and then load again a sub field of that global we need
    // a table look up

    set<Value*> visited;
    auto DL = M.getDataLayout();
    auto globalBoundsInitFnTy = FunctionType::get(voidTy, {}, false);
    auto globalBoundsInitFn = dyn_cast<Function>(M.getOrInsertFunction("__ds_init_global_bounds", globalBoundsInitFnTy));
    assert(globalBoundsInitFn && "should be able to create global bounds init funciton");
    auto entry = BasicBlock::Create(M.getContext(), "entry", globalBoundsInitFn);

    IRBuilder<> IRB(entry);

    auto dbgStr = getGlobalString(IRB, "globals init function");

    dbgs() << "begin createGlobalBounds\n";
    for (auto& g : M.globals()) {
      globalBoundsMap[&g] = infiniteBounds;  
      if (sensitiveSet.count(&g)) {
        //dbgs() << "sensitive\n";
        setGlobalSection(&g);
        lookForBoundsInInitializer(&g, IRB, dbgStr, DL, visited);
      } else {
        //dbgs() << "not sensitive\n";
      }
    }
    dbgs() << "end createGlobalBounds\n";
    IRB.CreateRetVoid();
    appendToGlobalCtors(M, globalBoundsInitFn, 99999);
  }

  // TODO fix this since I no longer clone main?
  void insertBoundsInTrampoline() {
    auto tramp = M.getFunction("__ds_trampoline");
    if (!tramp) {
        llvm_unreachable("there should always be a trampoline function");
    }
    for (inst_iterator It = inst_begin(tramp), Ie = inst_end(tramp); It != Ie; ++It) {
      auto i = &*It;
      if (auto mainCall = dyn_cast<CallInst>(i)) {
        if (mainCall->getCalledFunction() && mainCall->getCalledFunction()->getName() == "orig_main") {
          if (mainCall->getNumArgOperands() >= 2) {
            IRBuilder<> IRB(mainCall);
            auto orig_main = mainCall->getCalledFunction();
            auto argv = &*orig_main->arg_begin()++;
            if (sensitiveSet.count(argv)) {
              auto bounds = infiniteBounds; // FIX ME
              auto two = ConstantInt::get(int64Ty, 2);
              IRB.CreateCall(setFnArgBounds, {two, bounds});
              auto newArgv = IRB.CreateCall(dsSafeCopyArgv, {mainCall->getArgOperand(0), mainCall->getArgOperand(1)});
              mainCall->replaceUsesOfWith(mainCall->getArgOperand(1), newArgv);
            }
          }
        }
      }
    }
  }

  Bounds* getOrLoadBoundsFromBasedOn(Value* basedOnValue, BoundsMap& boundsMap, const TargetLibraryInfo& TLI,
                                     Value* DebugString, Instruction* insertionPoint) {
    DCDBG("getOrLoadBoundsFromBasedOn for: ");
    DEBUG(basedOnValue->dump());
    if (isa<ConstantPointerNull>(basedOnValue)) {
      return emptyBounds;
    }

    if (isa<LandingPadInst>(basedOnValue)) {
      return infiniteBounds;
    }

    if (isa<ConstantInt>(basedOnValue)) {
      return infiniteBounds;
    }

    if (auto cnst = dyn_cast<ConstantExpr>(basedOnValue)) {
      switch (cnst->getOpcode()) {
        case Instruction::IntToPtr:
          return infiniteBounds;
        default:
          cnst->dump();
          llvm_unreachable("basedOnValues shouldnt be other constants\n");
      }
    }
    if (isa<IntToPtrInst>(basedOnValue)) {
      return infiniteBounds;
    }

    if (isa<UndefValue>(basedOnValue)) {
      return infiniteBounds;
    }

    const Twine& boundsName = basedOnValue->getName() + "_bounds";
    if (auto call = dyn_cast<CallInst>(basedOnValue)) {
      // unfortunately we have two cases:
      // 1) a call to a malloc like fn, which creates new bounds, except for strdup!
      // 2) a call to an other function which copies bounds
      if ((isAllocationFnDS(call, &TLI))
          && call->getCalledFunction()->getName() != "strdup") {
        if (!boundsMap[call]) {
          call->dump();
          llvm_unreachable("all malloc like fn should be in bounds map");
        }
        return boundsMap[call];
      }

      if (boundsMap[call]) {
        return boundsMap[call];
      }

      // strchr just returns a pointer to within the original string so return the original string's bounds
      auto calledF = call->getCalledFunction();
      if (calledF && calledF->getName() == "strchr") {
         return getOrLoadBounds(call->getArgOperand(0), boundsMap, TLI, DebugString, insertionPoint);
      }

      // load the bounds immediatelly following the call
      IRBuilder<> IRB(call->getNextNode());
      Bounds* bounds = nullptr;
      if (DebugMode) {
        auto id = ConstantInt::get(int64Ty, IDCounter++);
        bounds = IRB.CreateCall(getFnArgBoundsDebug,
        {ConstantInt::get(int64Ty, 0), DebugString, id}, boundsName);
      } else {
        bounds = IRB.CreateCall(getFnArgBounds,
        {ConstantInt::get(int64Ty, 0)}, boundsName);
      }
      boundsMap[call] = bounds;
      NumBoundsLoads++;
      return bounds;
    } else if (auto alloc = dyn_cast<AllocaInst>(basedOnValue)) {
      alloc->dump();
      assert(boundsMap[alloc] && "all alloc should be in bounds map");
      return boundsMap[alloc];
    } else if (auto load = dyn_cast<LoadInst>(basedOnValue)) {
      if (boundsMap[load]) {
        return boundsMap[load];
      }

      // load the bounds right after the value is loaded
      auto ptrOp = load->getPointerOperand();

      if (auto ce = dyn_cast<ConstantExpr>(ptrOp)) {
        DCDBG("ptrOp name: " << ce->getOperand(0)->getName() << "\n");
        //switch (ce->getOpcode()) {
        //  case Instruction::BitCast:
        //  {
            auto innerPtr = ce->getOperand(0);
            // hack
            if (innerPtr->hasName() &&
                (   innerPtr->getName() == "stdout"
                    || innerPtr->getName() == "stdin"
                    || innerPtr->getName() == "stderr"
                    ) )
            {
              return infiniteBounds;
            }
         // }
        // }
      }

      // llvm does this weird thing where it turns a switch over global variables into
      // a new global variable "switch.table.xxx"  we do not support this
      if (ptrOp->hasName() && ptrOp->getName().startswith("switch.")) { return infiniteBounds; }


      Value* addr = ptrOp;
      IRBuilder<> IRB(load->getNextNode());
      auto baseCasted = IRB.CreateBitCast(addr, int8PtrTy);
      Value* bounds = nullptr;
      if (DebugMode) {
        auto id = ConstantInt::get(int64Ty, IDCounter++);
        bounds = IRB.CreateCall(getBoundsDebug, {baseCasted, DebugString, id}, boundsName);
      } else {
        bounds = IRB.CreateCall(getBounds, {baseCasted}, boundsName);
      }
      NumBoundsLoads++;
      boundsMap[load] = bounds;
      return bounds;
    } else if (auto global = dyn_cast<GlobalVariable>(basedOnValue)) {
      return globalBoundsMap[global];
    } else if (auto argu = dyn_cast<Argument>(basedOnValue)) {
      if (boundsMap[argu]) {
        return boundsMap[argu];
      }
      IRBuilder<> IRB(&*insertionPoint->getParent()->getParent()->getEntryBlock().getFirstInsertionPt());
      auto num = ConstantInt::get(int64Ty, argu->getArgNo()+1);
      Value* bounds = nullptr;
      if (DebugMode) {
        auto id = ConstantInt::get(int64Ty, IDCounter++);
        bounds = IRB.CreateCall(getFnArgBoundsDebug, {num, DebugString, id}, boundsName);
      } else {
        bounds = IRB.CreateCall(getFnArgBounds, {num}, boundsName);
      }
      NumBoundsLoads++;
      boundsMap[argu] = bounds;
      return bounds;
    } else if (auto alloc = dyn_cast<AllocaInst>(basedOnValue)) {
      assert(boundsMap[alloc] && "all alloca should be in bounds map");
      return boundsMap[alloc];
    } else if (isa<Function>(basedOnValue)) {
      return infiniteBounds; // we don't do cfi
    }
    basedOnValue->dump();
    llvm_unreachable("we should have found the bounds somehow?");
  }
  Value* backTrackOnce(Value* target) {
    if (auto cast = dyn_cast<BitCastInst>(target)) {
      return cast->getOperand(0);
    }
    if (auto cnst = dyn_cast<ConstantExpr>(target)) {
      switch (cnst->getOpcode()) {
        case Instruction::GetElementPtr:
          return cnst->getOperand(0); // backtrack to ptrOp
        case Instruction::BitCast:
          return cnst->getOperand(0); // backtrack to ptrOp
        case Instruction::PtrToInt:
          return cnst->getOperand(0); // backtrack to ptrOp
        default:
          cnst->dump();
          llvm_unreachable("dont know what to do for other operators?");
      }
    }
    if (auto GEP = dyn_cast<GetElementPtrInst>(target)) {
      return GEP->getPointerOperand();
    }

    // TODO probably we need to do something smart about phi and select nodes
    if (auto phi = dyn_cast<PHINode>(target)) {
      phi->dump();
      llvm_unreachable("cant backtrack through a phi");
    }
    if (auto sel = dyn_cast<SelectInst>(target)) {
      sel->dump();
      llvm_unreachable("i dont know what to do for a select");
    }
    if (isa<ConstantPointerNull>(target)) {
      return target;
    }

    target->dump();
    llvm_unreachable("couldn't back track once?");
  }
  bool isBasedOnValue(Value* target) {
    if (isa<LoadInst>(target)) {
      return true;
    }
    if (isa<CallInst>(target)) {
      //return getReturnValBounds(call);
      return true;
    }
    if (isa<Function>(target)) {
      return true;
    }
    if (isa<GlobalVariable>(target)) {
      return true;
    }
    if (isa<Argument>(target)) {
      return true;
    }
    if (isa<AllocaInst>(target)) {
      return true;
    }
    if (isa<ConstantPointerNull>(target)) {
      return true;
    }
    return false;
  }
  Value* getNonPhiBackTrack(Value* tmp, PHINode* thisPhi) {
    while(!isBasedOnValue(tmp)) {
      if (auto p = dyn_cast<PHINode>(tmp)) {
        if (thisPhi == p) {
          return nullptr;
        } else {
          return nullptr;
        }
      }
      tmp = backTrackOnce(tmp);
    }
    return tmp;
  }
  PHINode* getThisPhiBackTrack(Value* tmp, PHINode* thisPhi) {
    while(!isBasedOnValue(tmp)) {
      if (auto p = dyn_cast<PHINode>(tmp)) {
        if (thisPhi == p) {
          return p;
        } else {
          return nullptr;
        }
      }
      tmp = backTrackOnce(tmp);
    }
    return nullptr;
  }
  PHINode* getDifferentPhiBackTrack(Value* tmp, PHINode* thisPhi) {
    while(!isBasedOnValue(tmp)) {
      if (auto p = dyn_cast<PHINode>(tmp)) {
        if (thisPhi == p) {
          return nullptr;
        } else {
          return p;
        }
      }
      tmp = backTrackOnce(tmp);
    }
    return nullptr;
  }
  BasedOnValues getBasedOnValuePHI(PHINode* phi, BasedOnMap& basedOnMap) {
    //DCDBG("getting phi based on value: ");
    //DEBUG(phi->dump());

    unsigned N = phi->getNumIncomingValues();
    BasedOnValues basedOns;
    for (unsigned i = 0; i < N; ++i) {
      auto tmp = phi->getIncomingValue(i);
      //DCDBG("looking at incoming value: ");
      //DEBUG(tmp->dump());

      // i need to cash the based on values instead of re-exploring them
      if (basedOnMap[tmp]) {
        basedOns.values.push_back(basedOnMap[tmp]);
      } else {
        // TODO I need to fix my back track functions to check the basedOn map
        // then the basedOn map serves as a loop breaker ...
        if (getThisPhiBackTrack(tmp, phi)) {
          //DCDBG("adding null (loop) edge\n");
          // add a nullptr to signify a looping edge
          // TODO I need to use a different value, otherwise i cant
          // used the basedonmap ...
          basedOnMap[tmp] = phi;
          basedOns.values.push_back(phi);
        } else if (auto np = getNonPhiBackTrack(tmp, phi)) {
          //DCDBG("adding normal edge\n");
          basedOnMap[tmp] = np;
          basedOns.values.push_back(np);
        } else if (auto op = getDifferentPhiBackTrack(tmp, phi)) {
          if (basedOnMap[op]) {
            basedOns.values.push_back(basedOnMap[op]);
          } else {
            // here i need to make the a new PHInode with the values ...

            // now the problem is the basedon values might be different types ...
            // i could just bitcast them to void?

            // but then i need to bitcast them back ...
            // and they might be calls
            // calls i need to look up the bounds for immediately anyway ...
            IRBuilder<> IRB(op);
            unsigned N2 = op->getNumIncomingValues();
            auto newPhi = IRB.CreatePHI(op->getType(), N2, op->getName() + "based_on");
            basedOnMap[tmp] = newPhi;
            auto recursiveBasedOn = getBasedOnValuePHI(op, basedOnMap);
            for (unsigned i = 0; i < N2; ++i) {
              auto basedOn = recursiveBasedOn.values[i];
              auto edge = recursiveBasedOn.edges[i];
              if (basedOn == op) {
                // a loop
                newPhi->addIncoming(newPhi, edge);
              } else {
                newPhi->addIncoming(basedOn, edge);
              }
            }
            basedOns.values.push_back(newPhi);
          }
        } else {
          llvm_unreachable("how did we get here?");
        }
      }

      basedOns.edges.push_back(phi->getIncomingBlock(i));
      basedOns.phiNode = phi;
    }
    return basedOns;
  }
  void storeFnArg(IRBuilder<>& IRB, Bounds* bounds, uint64_t index, Value* debugStr) {
    auto idx = IRB.getInt64(index);
    if (DebugMode) {
      IRB.CreateCall(setFnArgBoundsDebug, {idx, bounds, debugStr});
    } else {
      IRB.CreateCall(setFnArgBounds, {idx, bounds});
    }
    NumBoundsStores++;
  }
  void insertBoundsStore(Value* bounds, Value* ptrAddr, Value* dbgStr, IRBuilder<>& IRB) {
    // store the bounds
    auto ptrCasted = IRB.CreateBitCast(ptrAddr, int8PtrTy);
    if (DebugMode) {
      auto id = ConstantInt::get(int64Ty, IDCounter++);
      IRB.CreateCall(setBoundsDebug, {ptrCasted, bounds, dbgStr, id});
    } else {
      IRB.CreateCall(setBounds, {ptrCasted, bounds});
    }
    NumBoundsStores++;
  }
  bool areAnyPHIValsInt2Ptr(PHINode* phi) {
    for (auto &incVal : phi->incoming_values()) {
      if (isa<PtrToIntInst>(incVal)) {
        return true;
      }
    }
    return false;
  }

  void insertBoundsStores(Function& F, BoundsMap& boundsMap, const DataLayout& DL, const TargetLibraryInfo& TLI) {
    // we need to store bounds in three cases.  Whenever we:
    // 1) store a pointer
    // 2) pass a sensitive pointer to a function
    // 3) return a sensitive pointer

    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie; ++It) {
      auto i = &*It;
      if (auto store = dyn_cast<StoreInst>(i)) {
        if (sensitiveSet.count(store->getPointerOperand()) || sensitiveSet.count(store->getValueOperand())) {
          auto val = store->getValueOperand();

          Value* bounds = nullptr;
          Value* dbgStr = nullptr;
          IRBuilder<> IRB(store);
          if (DebugMode) {
            dbgStr = getDebugString(IRB, store);
          }
          // sometimes LLVM stores a pointer as i64 for no good reason ..
          if (!val->getType()->isPointerTy()) {
            if (val->getType() == int64Ty) {
              if (auto load = dyn_cast<LoadInst>(val)) {
                bounds = getOrLoadBoundsFromBasedOn(load, boundsMap, TLI, dbgStr, load);
              } else if (auto ptr2int = dyn_cast<PtrToIntInst>(val)) {
                bounds = getOrLoadBounds(ptr2int->getPointerOperand(), boundsMap, TLI, dbgStr, ptr2int);
              } else if (auto phi = dyn_cast<PHINode>(val)) {
                if (areAnyPHIValsInt2Ptr(phi)) {
                  bounds = getOrLoadBounds(phi, boundsMap, TLI, dbgStr, load);
                } else {
                  continue;
                }
              } else {
                continue;
              }
            } else if (auto vecTy = dyn_cast<VectorType>(val->getType())) {
              // if the element type is less than 64 bits it cant be a pointer
              if (!vecTy->getElementType()->isPointerTy() && vecTy->getElementType()->getScalarSizeInBits() < 64) { continue; }
              if (isa<ConstantDataVector>(val)) { continue; }
              if (auto vec = dyn_cast<ConstantVector>(val)) {
                if (auto splat = vec->getSplatValue()) {
                  if (vecTy->getNumElements() != 2) {
                    store->dump();
                    llvm_unreachable("don't know what to do when there are not 2 elements");
                  }
                  bounds = getOrLoadBounds(splat, boundsMap, TLI, dbgStr, store);
                  auto ptrAddr = store->getPointerOperand();
                  auto ptrAddrAsVoid = IRB.CreateBitCast(ptrAddr, int8PtrTy);
                  auto ptrAddrPlusOne = IRB.CreateGEP(ptrAddrAsVoid, ConstantInt::get(int64Ty, 8));
                  insertBoundsStore(bounds, ptrAddr, dbgStr, IRB);
                  insertBoundsStore(bounds, ptrAddrPlusOne, dbgStr, IRB);
                } else {
                  if (vecTy->getNumElements() != 2) {
                    store->dump();
                    llvm_unreachable("don't know what to do when there are not 2 elements");
                  }
                  auto zero = ConstantInt::get(int32Ty, 0, false);
                  auto one = ConstantInt::get(int32Ty, 1, false);
                  auto el0 = ConstantExpr::getExtractElement(vec, zero);
                  auto el1 = ConstantExpr::getExtractElement(vec, one);
                  auto bounds0 = getOrLoadBounds(el0, boundsMap, TLI, dbgStr, store);
                  auto bounds1 = getOrLoadBounds(el1, boundsMap, TLI, dbgStr, store);
                  auto ptrAddr = store->getPointerOperand();
                  auto ptrAddrAsVoid = IRB.CreateBitCast(ptrAddr, int8PtrTy);
                  auto ptrAddrPlusOne = IRB.CreateGEP(ptrAddrAsVoid, ConstantInt::get(int64Ty, 8));
                  insertBoundsStore(bounds0, ptrAddr, dbgStr, IRB);
                  insertBoundsStore(bounds1, ptrAddrPlusOne, dbgStr, IRB);
                  continue;
                }
              } else {
                auto ptrAddr = store->getPointerOperand();
                auto ptrAddrAsVoid = IRB.CreateBitCast(ptrAddr, int8PtrTy);
                for (unsigned i = 0; i < vecTy->getNumElements(); ++i) {
                    auto ptrAddrPlusI = IRB.CreateGEP(ptrAddrAsVoid, ConstantInt::get(int64Ty, 8*i));
                    insertBoundsStore(infiniteBounds, ptrAddrPlusI, dbgStr, IRB);
                }
                continue;
              }
            } else {
              continue;
            }
          } else {
            // find the bounds
            DCDBG("looking for bounds for: "; store->dump());
            bounds = getOrLoadBounds(val, boundsMap, TLI, dbgStr, store);
          }
          auto ptrAddr = store->getPointerOperand();
          insertBoundsStore(bounds, ptrAddr, dbgStr, IRB);
        }
      }
      if (auto call = dyn_cast<CallInst>(i)) {
        // dont bother storing bounds when we call instrinc functions
        // __ds_ functions should probably check the bounds
        auto calledFn = call->getCalledFunction();
        if (calledFn && isWhiteListed(*calledFn)) { continue; }

        if (calledFn && calledFn->isDeclaration()) { continue; }


        //DCDBG("checking call: ");
        //DEBUG(call->dump());
        int i = 1;
        for (auto& argu : call->arg_operands()) {
          if (!argu->getType()->isPointerTy()) {
            // no bounds for non-pointer types
            // TODO dataflow tracking?
            i++; // this is a dumb wahy of doing it because i forgot to inc before this continue and that caused a bug :'(
            continue;
          }
          if (sensitiveSet.count(argu) || isNullPointerPassedAsSensitive(*call, argu, i-1)) {
            IRBuilder<> IRB(call);
            Value* dbgStr = nullptr;
            if (DebugMode) {
              dbgStr = getDebugString(IRB, call);
            }
            auto bounds = getOrLoadBounds(argu, boundsMap, TLI, dbgStr, call);
            storeFnArg(IRB, bounds, i, dbgStr);
          }
          i++;
        }
      }
      if (auto ret = dyn_cast<ReturnInst>(i)) {
        if (auto rv = ret->getReturnValue()) {
          if (sensitiveSet.count(rv) && couldHaveBounds(rv)) {
            IRBuilder<> IRB(ret);
            Value* dbgStr = nullptr;
            if (DebugMode) {
              dbgStr = getDebugString(IRB, ret);
            }
            auto bounds = getOrLoadBounds(rv, boundsMap, TLI, dbgStr, ret);
            storeFnArg(IRB, bounds, 0, dbgStr);
          }
        }
      }
    }
  }
  bool isNullPointerPassedAsSensitive(CallInst& call, Value* argu, unsigned argNo) {
    if (!isa<ConstantPointerNull>(argu)) { return false; }
    auto calledF = call.getCalledFunction();
    if (!calledF) { return false; }
    auto arg = calledF->arg_begin();
    for (unsigned i = 0; i != argNo; ++i) {
      ++arg;
    }
    return sensitiveSet.count(&*arg);
  }

  Value* getBasedOnValue(Value* target) {
    // we want to find out how this value entered the current function scope,
    // i.e., was it:
    // loaded
    // returned from another function
    // an argument to this function
    // a global variable
    // alloca'd in this function
    //
    // ultimately, we return the thing we need to look up the bounds
    // of target

    // these are the possible based-on values:
    if (auto load = dyn_cast<LoadInst>(target)) {
      // as an optimization, we can check if this pointer
      // was stored to inside this same function. if so,
      // we may have the bounds for that store within local
      // scope so return that instead
      //
      // maybe llvm optimizations take care of this?
      // it seems fairly obvious...
      return load;
    }
    if (auto call = dyn_cast<CallInst>(target)) {
      // hack (kind of)
      // so we don't instrument libc
      // and some libc functions return pointers
      // so when it tries to look up the returned pointer's bounds it fails
      //
      // in the case of strstr the bounds should just be the same as the str we're searching
      if (call->getCalledFunction() && call->getCalledFunction()->getName() == "strstr") {
        return getBasedOnValue(call->getArgOperand(0));
      }
      return call;
    }
    if (auto F = dyn_cast<Function>(target)) {
      return F;
    }
    if (auto gbl = dyn_cast<GlobalVariable>(target)) {
      return gbl;
    }
    if (isa<Argument>(target)) {
      return target;
    }
    if (auto alloca = dyn_cast<AllocaInst>(target)) {
      return alloca;
    }
    if (isa<InlineAsm>(target)) {
      return target; // why doesnt the type checker let me return inlineASM?
    }

    // for any other case we keep looking backwards
    if (auto cast = dyn_cast<BitCastInst>(target)) {
      return getBasedOnValue(cast->getOperand(0));
    }
    if (auto cnst = dyn_cast<ConstantExpr>(target)) {
      switch (cnst->getOpcode()) {
        case Instruction::GetElementPtr:
          return getBasedOnValue(cnst->getOperand(0)); // backtrack to ptrOp
        case Instruction::BitCast:
          return getBasedOnValue(cnst->getOperand(0)); // backtrack to ptrOp
        case Instruction::PtrToInt:
          return getBasedOnValue(cnst->getOperand(0)); // backtrack to ptrOp
        case Instruction::IntToPtr:
          return cnst;
        default:
          cnst->dump();
          llvm_unreachable("dont know what to do for other operators?");
      }
    }
    // for now sub-object bounds narrow isnt implemented
    if (auto GEP = dyn_cast<GetElementPtrInst>(target)) {
      return getBasedOnValue(GEP->getPointerOperand());
    }

    // TODO probably we need to do something smart about phi and select nodes
    if (auto phi = dyn_cast<PHINode>(target)) {
      return phi;
    }
    if (auto sel = dyn_cast<SelectInst>(target)) {
      return sel;
    }
    if (isa<ConstantPointerNull>(target)) {
      return target;
    }
    if (isa<ConstantInt>(target)) {
      return target;
    }
    if (auto inttoptr = dyn_cast<IntToPtrInst>(target)) {
      return inttoptr;
    }

    if (isa<UndefValue>(target)) {
      return target;
    }

    if (auto alias = dyn_cast<GlobalAlias>(target)) {
      return getBasedOnValue(alias->getAliasee());
    }

    if (auto ext = dyn_cast<ExtractValueInst>(target)) {
      return getBasedOnValue(ext->getAggregateOperand());
    }

    if (auto zext = dyn_cast<ZExtInst>(target)) {
      return getBasedOnValue(zext->getOperand(0));
    }

    if (auto ptrtoint = dyn_cast<PtrToIntInst>(target)) {
      return getBasedOnValue(ptrtoint->getPointerOperand());
    }
    
    if (isa<LandingPadInst>(target)) {
      // what? what are the bounds of a landing pad?
      // this seems wrong
      return target;
    }

    target->dump();
    llvm_unreachable("couldn't find the based on value");
  }
  Bounds* getOrLoadBounds(Value* val, BoundsMap& boundsMap, const TargetLibraryInfo& TLI,
    Value* DebugString, Instruction* instruction) {
    //DCDBG("getOrLoadBounds for: ");
    //DEBUG(val->dump());
    auto basedOnValue = getBasedOnValue(val);
    //DCDBG("based on:  ");
    //DEBUG(basedOnValue->dump());
    if (boundsMap[basedOnValue]) {
      //DCDBG("was in bounds map\n");
      return boundsMap[basedOnValue];
    }

    if (isa<ConstantInt>(basedOnValue)) {
      return infiniteBounds;
    }

    // if you call ASM, what can we do?
    if (auto call = dyn_cast<CallInst>(basedOnValue)) {
      if (call->isInlineAsm()) {
        return infiniteBounds;
      }
    }

    // apparently you can also store inline asm, like a fn ptr?
    if (isa<InlineAsm>(basedOnValue)) {
      return infiniteBounds;
    }

    if (isa<ConstantPointerNull>(val)) {
      return emptyBounds;
    }

    //DCDBG("not in bounds map\n");
    if (auto sel = dyn_cast<SelectInst>(basedOnValue)) {
      // are we caching the bounds at somepoint?
      auto cond = sel->getCondition();
      IRBuilder<> IRB(sel->getNextNode());
      auto trueBounds = getOrLoadBounds(sel->getTrueValue(), boundsMap, TLI, DebugString, sel);
      auto falseBounds = getOrLoadBounds(sel->getFalseValue(), boundsMap, TLI, DebugString, sel);
      auto boundsSel = IRB.CreateSelect(cond, trueBounds, falseBounds);
      return boundsSel;
    }
    if (auto phi = dyn_cast<PHINode>(basedOnValue)) {
      IRBuilder<> IRB(phi->getNextNode());
      unsigned N = phi->getNumIncomingValues();
      auto newPhi = IRB.CreatePHI(boundsTy, N);
      boundsMap[basedOnValue] = newPhi;
      vector<Value*> incBasedOns;
      for (auto& inc : phi->incoming_values()) {
        auto tmp = getBasedOnValue(inc);
        incBasedOns.push_back(tmp);
      }
      for (unsigned i = 0; i < N; ++i) {
        Value* bounds = nullptr;
        if (isa<PHINode>(incBasedOns[i]) || isa<SelectInst>(incBasedOns[i])) {
          // probably the insertion point should be incBasedOns[i], right?
          bounds = getOrLoadBounds(incBasedOns[i], boundsMap, TLI, DebugString, instruction);
        } else {
          auto incBB = phi->getIncomingBlock(i);
          auto incBBEnd = --(incBB->end());
          // where should we insert this bounds load?
          bounds = getOrLoadBoundsFromBasedOn(incBasedOns[i], boundsMap, TLI, DebugString, &*incBBEnd);
        }
        newPhi->addIncoming(bounds, phi->getIncomingBlock(i));
      }
      return newPhi;
    } else {
      return getOrLoadBoundsFromBasedOn(basedOnValue, boundsMap, TLI, DebugString, instruction);
    }
  }
  void findSensitiveAllocations(Function& F, const TargetLibraryInfo& TLI, InstructionSet& sensitiveAllocations) {
    for (inst_iterator I = inst_begin(&F), E = inst_end(&F); I != E; ++I) {
      auto i = &*I;
      if (auto call = dyn_cast<CallInst>(i)) {
          if (sensitiveSet.count(i) && isAllocationFnDS(call, &TLI)) {
            sensitiveAllocations.insert(call);
          }
      }
      if (isa<AllocaInst>(i) && sensitiveSet.count(i)) {
        sensitiveAllocations.insert(i);
      }
    }
  }
  void createBoundsForAllocations(const DataLayout& DL,
    const InstructionSet& sensitiveAllocations,
    BoundsMap& boundsMap) {
    for (auto alloc : sensitiveAllocations) {
      IRBuilder<> IRB(alloc->getNextNode());
      if (auto mallc = dyn_cast<CallInst>(alloc)) {
        Value* sz = nullptr;
        auto fnName = mallc->getCalledFunction()->getName();
        if (fnName.endswith("realloc")) {
          sz = mallc->getArgOperand(1);
        } else if (fnName.endswith("strdup")) {
          continue; 
        } else if (fnName.endswith("calloc")) {
          sz = IRB.CreateMul(mallc->getOperand(0), mallc->getOperand(1));
        } else if (fnName.endswith("malloc")) {
          sz = mallc->getArgOperand(0);
        } else if (fnName.endswith("alloc")) {
          sz = mallc->getArgOperand(0);
        } else if (fnName.endswith("_Znam")
                                   || fnName.endswith("_Znaj")
                                   || fnName.endswith("_Znwm")
                                   || fnName.endswith("_Znwj")) {
          sz = mallc->getArgOperand(0);
        } else {
          llvm_unreachable("mismatched allocation function name?");
        }
        auto bounds = createBounds(IRB, mallc, sz);
        boundsMap[mallc] = bounds;
        DCDBG("created bounds: ");
        DEBUG(mallc->dump());
        DEBUG(dbgs() << " => ");
        DEBUG(
              Value* lower = IRB.CreateExtractValue(bounds, 0);
            lower->dump();
        );
        DEBUG(dbgs() << " => ");
        DEBUG(
              Value* upper = IRB.CreateExtractValue(bounds, 1);
            upper->dump();
        );
      }
      if (auto alloca = dyn_cast<AllocaInst>(alloc)) {
        if (alloca->isStaticAlloca()) {
          auto nelements = alloca->getArraySize();
          auto typeSize = DL.getTypeAllocSize(alloca->getAllocatedType());
          auto typeSizeVal = ConstantInt::get(int32Ty, typeSize);
          Value* sz = IRB.CreateMul(nelements, typeSizeVal);
          auto bounds = createBounds(IRB, alloca, sz);
          boundsMap[alloca] = bounds;
          DCDBG("created bounds: ");
          DEBUG(alloca->dump());
          DEBUG(dbgs() << " => ");
          DEBUG(
                Value* lower = IRB.CreateExtractValue(bounds, 0);
              lower->dump();
          );
          DEBUG(dbgs() << " => ");
          DEBUG(
                Value* upper = IRB.CreateExtractValue(bounds, 1);
              upper->dump();
          );
        } else {
          auto sz = alloca->getArraySize();
          auto bounds = createBounds(IRB, alloca, sz);
          boundsMap[alloca] = bounds;
        }
      }
    }
  }
  Bounds* createBounds(IRBuilder<>& IRB, Value* Base, Value*Size) {
    auto last_idx = IRB.CreateSub(Size, ConstantInt::get(Size->getType(), 1));
    auto last = IRB.CreateGEP(Base, last_idx, "object_end");
    auto baseCasted = IRB.CreateBitCast(Base, int8PtrTy);
    auto lastCasted = IRB.CreateBitCast(last, int8PtrTy);
    Bounds* bounds = UndefValue::get(boundsTy);
    bounds = IRB.CreateInsertValue(bounds, baseCasted, 0);
    bounds = IRB.CreateInsertValue(bounds, lastCasted, 1);
    return bounds;
  }

  void insertInLineBoundsCheck(Function& F, Instruction* checkedInst,
                                         Bounds* bounds, Value* ptr, Value* debugStr, const DataLayout& DL) {

    if (bounds == infiniteBounds) {
      return;
    }

    //DCDBG("inline bounds checking:");
    //DEBUG(ptr->dump());
    auto origBB = checkedInst->getParent();
    BasicBlock* passBB = nullptr;

    for (auto& I : *origBB) {
      if (&I == checkedInst) {
        passBB = origBB->splitBasicBlock(&I);
        break;
      }
    }
    auto origEnd = origBB->end();
    auto uncond = --origEnd;
    IRBuilder<> origBuilder(&*uncond);

    auto ptrType = cast<PointerType>(ptr->getType());
    auto eleType = ptrType->getElementType();
    uint64_t sz = 0;
    if (isa<FunctionType>(eleType)) {
      sz = 1;
    } else {
      sz = DL.getTypeStoreSize(eleType);
    }
    auto idx = cast<ConstantInt>(ConstantInt::get(int64Ty, sz-1));

    if (DebugMode) {
      auto numBoundsChecksPtr = getOrCreateNumBoundsChecks(*F.getParent());
      assert(numBoundsChecksPtr && "__ds_num_bounds_checks should exist");
      auto numBoundsChecks = origBuilder.CreateLoad(numBoundsChecksPtr, "num_bounds_checks");
      auto newNum = origBuilder.CreateAdd(numBoundsChecks, ConstantInt::get(int64Ty, 1, false));
      origBuilder.CreateStore(newNum, numBoundsChecksPtr);
    }

    auto ptrCasted = origBuilder.CreateBitCast(ptr, int8PtrTy);
    auto objectBottom = origBuilder.CreateGEP(ptrCasted, idx, ptr->getName() + "_bottom");

    auto base = origBuilder.CreateExtractValue(bounds, 0);
    auto last = origBuilder.CreateExtractValue(bounds, 1);

    // make fail branch
    BasicBlock* failBB = BasicBlock::Create(F.getParent()->getContext(), "fail", &F);
    IRBuilder<> failBuilder(failBB);

    if (DebugMode) {
      auto id = ConstantInt::get(int64Ty, IDCounter++);
      failBuilder.CreateCall(abortDebug, {bounds, ptrCasted, objectBottom, debugStr, id});
    } else {
      failBuilder.CreateCall(abortFn, {});
    }

    failBuilder.CreateBr(passBB);

    // is the ptr greater than or equal to the base?
    auto isGreaterThanBase = origBuilder.CreateICmpUGE(ptrCasted, base);

    // TODO lower bound check too
    auto isLessThanLast = origBuilder.CreateICmpULE(objectBottom, last);

    auto isInBounds = origBuilder.CreateAnd(isGreaterThanBase, isLessThanLast);
    //auto isInBounds = isGreaterThanBase;

    // put the branch in the original block
    // TODO set branch weights?
    origBuilder.CreateCondBr(isInBounds, passBB, failBB);

    // now delete the unconditional branch
    // that splitbasicblock inserted
    uncond->eraseFromParent();

    return;
  }
  void insertBoundsChecks(Function& F, BoundsMap& boundsMap, const DataLayout& DL, const TargetLibraryInfo& TLI) {
    // we need to check bounds whenever we dereference a pointer ...
    // which is when we:
    // load
    // store
    // call a function through a function pointer


    // TODO I need to build up all the places that need bounds and then insert the checks
    // because the inline check moves the instruction
    vector<Value*> needsBounds;
    for (inst_iterator It = inst_begin(&F), Ie = inst_end(&F); It != Ie; ++It) {
      auto inst = &*It;
      if (auto load = dyn_cast<LoadInst>(inst)) {
        if (sensitiveSet.count(load->getPointerOperand())) {
          DCDBG("bounds checking: ");
          DEBUG(inst->dump());
          needsBounds.push_back(inst);
        }
      }
      if (auto store = dyn_cast<StoreInst>(inst)) {
        if (sensitiveSet.count(store->getPointerOperand())) {
          DCDBG("bounds checking: ");
          DEBUG(inst->dump());
          needsBounds.push_back(inst);
        }
      }
      if (auto call = dyn_cast<CallInst>(inst)) {
        if (!call->getCalledFunction()) {
          // we don't do cfi
        }
      }
    }

    for (auto& inst : needsBounds) {
      if (auto store = dyn_cast<StoreInst>(inst)) {
        IRBuilder<> IRB(store);
        auto ptrOp = store->getPointerOperand();
        Value* debugStr = nullptr;
        if (DebugMode) {
          debugStr = getDebugString(IRB, store);
        }
        if (isa<GlobalVariable>(ptrOp)) { continue; } // global variables are constant pointers
        auto bounds = getOrLoadBounds(ptrOp, boundsMap, TLI, debugStr, store);
        if (isStaticallyInBounds(bounds, ptrOp)) { continue; }
        insertInLineBoundsCheck(F, store, bounds, ptrOp, debugStr, DL);
        NumBoundsChecks++;
      } else if (auto load = dyn_cast<LoadInst>(inst)) {
        NumLoads++;
        IRBuilder<> IRB(load);
        auto debugStr = getDebugString(IRB, load);
        auto ptrOp = load->getPointerOperand();
        if (isa<GlobalVariable>(ptrOp)) { continue; } // global variables are constant pointers
        if (auto call = dyn_cast<CallInst>(ptrOp)) {
          if (call->getCalledFunction() &&
              call->getCalledFunction()->getName() == "__errno_location" )
          {
            continue;
          }
        }

        //DCDBG("adding bounds check for load: ");
        //DEBUG(load->dump());
        auto bounds = getOrLoadBounds(ptrOp, boundsMap, TLI, debugStr, load);
        if (isStaticallyInBounds(bounds, ptrOp)) { continue; }
        insertInLineBoundsCheck(F, load, bounds, ptrOp, debugStr, DL);
        NumBoundsChecks++;
      } else if (auto call = dyn_cast<CallInst>(inst)) {

        if (call->getCalledFunction() || call->isInlineAsm())  {
          continue;
        }
        if (isa<GlobalAlias>(call->getCalledValue())) {
          continue; // musl has these weird aliases but they're not really pointer
        }
        IRBuilder<> IRB(call);
        auto debugStr = getDebugString(IRB, call);
        auto fnPtr = call->getCalledValue();
        auto bounds = getOrLoadBounds(fnPtr, boundsMap, TLI, debugStr, call);
        insertInLineBoundsCheck(F, call, bounds, fnPtr, debugStr, DL);
        NumBoundsChecks++;
      }
    }

  }
  bool isStaticallyInBounds(Bounds* bounds, Value* ptr) {
    // this could probably we better? haha
    return false;
  }
  public:
  BoundsAnalysis(Module& M, const TargetLibraryInfo& TLI, const ValueSet& sensitiveSet) : M(M), sensitiveSet(sensitiveSet) {
    auto Zero = ConstantExpr::getIntToPtr(ConstantInt::get(int64Ty, 0), int8PtrTy);
    auto boundary = ConstantExpr::getIntToPtr(ConstantInt::get(int64Ty, 1ULL<<32), int8PtrTy);
    auto maxC = ConstantExpr::getIntToPtr(ConstantInt::get(int64Ty, ~0ULL), int8PtrTy);
    infiniteBounds = ConstantStruct::get(boundsTy, boundary, maxC, NULL);
    emptyBounds = ConstantStruct::get(boundsTy, Zero, Zero, NULL);
    auto DL = M.getDataLayout();
    getRuntimeFunctions();
    createGlobalBounds();
  }
  void runOnFunction(Function& F, const DataLayout& DL, const TargetLibraryInfo& TLI) {
      InstructionSet sensAllocs;
      BoundsMap boundsMap;
      findSensitiveAllocations(F, TLI, sensAllocs);
      createBoundsForAllocations(DL, sensAllocs, boundsMap);
      insertBoundsStores(F, boundsMap, DL, TLI);
      if (!F.getName().startswith("ngx_vslprintf")) {
        insertBoundsChecks(F, boundsMap, DL, TLI);
      }
  }

}; // end class BoundsAnalysis

struct DataShield : public ModulePass {
  private:
  set<Function*> calledLibFunctions;
  public:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
      //AU.addRequired<AAResultsWrapperPass>();
  }
  static char ID; // Pass identification, replacement for typeid
  explicit DataShield() : ModulePass(ID) {
    initializeDataShieldPass (*PassRegistry::getPassRegistry());
  }
  bool runOnModule(Module& M) override {

    dbgs() << "Starting DataShield pass on: " << M.getName() << "\n";


    if (SaveModuleBefore) {
        dbgs() << "[DATASHIELD] Saving module.\n";
        saveModule(M, M.getName() + "__before.ll");
        dbgs() << "[DATASHIELD] Finished saving module.\n";
    }

    whiteList.push_back("llvm.dbg");
    whiteList.push_back("llvm.lifetime");
    whiteList.push_back("__ds");

    initTypeShortHands(M);
    getRuntimeMemManFunctions(M);

    TargetLibraryInfoImpl TLII(Triple(M.getTargetTriple()));
    TargetLibraryInfo TLI(TLII);
    auto DL = M.getDataLayout();

    // conservatively replace every free/malloc/calloc/realloc/strdup with the unsafe verision

    dbgs() << "[DATASHIELD] Replacing all memman functions with unsafe ...\n";
    if (LibraryMode) {
      replaceAllByNameWith(M, "malloc", *unsafeMalloc);
      replaceAllByNameWith(M, "free", *unsafeFree);
      replaceAllByNameWith(M, "calloc", *unsafeCalloc);
      replaceAllByNameWith(M, "realloc", *unsafeRealloc);
      replaceAllByNameWith(M, "strdup", *unsafeStrdup);
      replaceAllByNameWith(M, "__strdup", *unsafeStrdup);
      replaceAllByNameWith(M, "xmalloc", *unsafeMalloc);
      return true;
    } else {
      // NormalMode
      replaceAllByNameWith(M, "malloc", *unsafeMalloc);
      replaceAllByNameWith(M, "free", *unsafeFree);
      replaceAllByNameWith(M, "_ZdlPv", *unsafeFree);
      replaceAllByNameWith(M, "_ZdaPv", *unsafeFree);
      replaceAllByNameWith(M, "_ZdlPvj", *unsafeFree);
      replaceAllByNameWith(M, "_ZdlPvm", *unsafeFree);
      replaceAllByNameWith(M, "_ZdaPvj", *unsafeFree);
      replaceAllByNameWith(M, "_ZdaPvm", *unsafeFree);
      replaceAllByNameWith(M, "_ZdaPvm", *unsafeFree);

      replaceAllByNameWith(M, "xmalloc", *unsafeMalloc);
      replaceAllByNameWith(M, "_Znwj", *unsafeMalloc);
      replaceAllByNameWith(M, "_Znwm", *unsafeMalloc);
      replaceAllByNameWith(M, "_Znaj", *unsafeMalloc);
      replaceAllByNameWith(M, "_Znam", *unsafeMalloc);
      replaceAllByNameWith(M, "calloc", *unsafeCalloc);
      replaceAllByNameWith(M, "realloc", *unsafeRealloc);
      replaceAllByNameWith(M, "strdup", *unsafeStrdup);
      replaceAllByNameWith(M, "__strdup", *unsafeStrdup);

      replaceAllWeirdCXXMemManFn(M);
    }

    dbgs() << "[DATASHIELD] Finished replacing all memman functions with unsafe\n";

    Sandboxer boxer(M);

    dbgs() << "[DATASHIELD] Starting sensitivity analysis.\n";
    SensitivityAnalysis SA(M);
    if (SA.sensitiveTypes.size() != 0) {
      SA.analyzeModule();

      // type check?

      // for every callinfo in SA that uses sensitive data,
      // redirect the callee to the replacement function
      for (auto& ci : SA.callInfos.data) {
        if (ci.needsNewFunction()) {
          if (!ci.isDirectCall()) {
            continue;
          }
          if (ci.replacement == nullptr && !ci.isCallToExternalFunction()) {
            ci.dump();
            dbgs() << ci.getSignatureString() << "\n";
            llvm_unreachable("we should be replacing all non external functions that need new functions...");
          }
          if (ci.replacement != nullptr) {
            auto& cs = ci.callSite;
            auto repl = ci.replacement;
            auto orig = ci.getCallee();
            //DEBUG(dbgs() << "in :");
            //DEBUG(cs.dump());
            //DEBUG(dbgs() << "replacing: " << orig->getName() << " with " << repl->getName() << "\n");
            cs.replaceUsesOfWith(orig, repl);
          }
        }
      }
    }

    boxer.copyAndReplaceEnviron(M, SA.sensitiveSet);


    //SA.sensitiveSet.dump();
    dbgs() << "[DATASHIELD] Finished sensitivity analysis.\n";

    for (auto& F : M) {
      if (!isWhiteListed(F)) {
        boxer.insertPointerMasks(F, SA.sensitiveSet);
      }
    }

    dbgs() << "start memory regioner\n";
    MemoryRegioner regioner(M, SA.sensitiveSet);

    for (auto& F : M) {
      regioner.replaceCFuncsThatAlloc(F);
    }

    for (auto& F : M) {
      regioner.moveSensitiveAllocsToHeap(F, DL);
      regioner.replaceAllocationsWithSafe(M, F); // the unsafe allocation functions are already replaced
      regioner.replaceByValSensitiveArguments(M, F, DL);
    }
    dbgs() << "end memory regioner\n";

    dbgs() << "start bounds analysis\n";
    BoundsAnalysis BA(M, TLI, SA.sensitiveSet);
    for (auto& F : M) {
      if (!isWhiteListed(F)) {
        BA.runOnFunction(F, DL, TLI);
      }
    }
    dbgs() << "end bounds analysis\n";

    for (auto& F : M) {
      for (inst_iterator It = inst_begin(F), Ie = inst_end(F); It != Ie;) {
        Instruction *I = &*(It++);
        if (auto call = dyn_cast<CallInst>(I)) {
          if (auto calledF = call->getCalledFunction()) {
            if (calledF->hasName() && calledF->getName().startswith("llvm.memcpy")) {
              auto dest = call->getArgOperand(0);
              auto src = call->getArgOperand(1);
              auto sz = call->getArgOperand(2);
              if (SA.sensitiveSet.count(dest)) {
                //if (isa<Constant>(src)) {
                //  continue; // right now globals don't have bounds.  and globals are probably strings anyway
                //}
                if (DebugMode) {
                  auto id = ConstantInt::get(int64Ty, IDCounter++);
                  IRBuilder<> IRB(call->getNextNode());
                  IRB.CreateCall(metadataCopyDebug, {dest, src, sz, id});
                } else {
                  // insert a call to bound the bounds of the interior members
                  IRBuilder<> IRB(call->getNextNode());
                  IRB.CreateCall(metadataCopy, {dest, src, sz});
                }
              }
            }
          }
        }
      }
    }
    dbgs() << "end llvm.memcpy metadata copying\n";

    boxer.copyAndReplaceArgvIfNecessary(M, SA.sensitiveSet);

    if (DebugMode) {
        insertStatsDump(M);
    }

    DEBUG(dbgs() << "pass finished\n");

    if (SaveModuleAfter) {
      dbgs() << "saving module\n";
      saveModule(M, M.getName() + "__after.ll");
      dbgs() << "done saving module\n";
    }

    return true;
  }
}; // end struct DataShield
} // end anonymous namespace

char DataShield::ID = 0;
INITIALIZE_PASS_BEGIN(DataShield, "datashield", "DataShield", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(DataShield, "datashield", "DataShield", false, false)

ModulePass *llvm::createDataShieldPass() {
  return new DataShield();
}
