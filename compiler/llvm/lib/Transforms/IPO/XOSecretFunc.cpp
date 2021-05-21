//===----- XOSecretFunc.cpp - replace yolk constant with inline assembly --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that replaces yolk constant with inline assembly
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/iterator.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <sstream>

using namespace llvm;

#define DEBUG_TYPE "xo-secret-func"

namespace {

// create inline assembly instruction for initialized yolk
InlineAsm *generateAsm(ConstantInt *val) {
  Type *returnTy = val->getType();
  FunctionType *FTy =
    FunctionType::get(returnTy, /*Variadic=*/false);
  DEBUG(dbgs() << "finish getting type\n");
  int32_t num32;
  int64_t num64;
  std::string AsmString;
  if (returnTy->isIntegerTy(32)) {
    num32 = val->getSExtValue();
    AsmString = "movl $$" + std::to_string(num32) + ", $0";
  }
  else if (returnTy->isIntegerTy(64)) {
    num64 = val->getSExtValue();
    AsmString = "movabs $$" + std::to_string(num64) + ", $0";
  }
  else
    return NULL;
  std::string Constraints = "=r";
  InlineAsm *AsmFunc =
    llvm::InlineAsm::get(FTy, AsmString, Constraints, /*SideEffects=*/false);
  DEBUG(dbgs() << "create asm \n");
  return AsmFunc;
};

// create inline assembly instruction for uninitialized yolk
InlineAsm *generateAsmNoInit(Type *returnTy, std::string sourceName) {
  FunctionType *FTy =
    FunctionType::get(returnTy, /*Variadic=*/false);
  DEBUG(dbgs() << "finish getting type\n");
  std::string AsmString;
  if (returnTy->isIntegerTy(32)) {
    //AsmString = "movl " + sourceName + ", $0";
    AsmString = "movl $$" + std::to_string(0) + ", $0 ${:comment}" + sourceName;
  }
  else if (returnTy->isIntegerTy(64)) {
    //AsmString = "movabs " + sourceName + ", $0";
    AsmString = "movabs $$" + std::to_string(0) + ", $0 ${:comment}" + sourceName;
  }
  else
    return NULL;
  std::string Constraints = "=r";
  InlineAsm *AsmFunc =
    llvm::InlineAsm::get(FTy, AsmString, Constraints, /*SideEffects=*/false);
  DEBUG(dbgs() << "create asm \n");
  return AsmFunc;
};
  
class XOSecretFunc : public ModulePass {
  public:
  static char ID;
  explicit XOSecretFunc() : ModulePass(ID) {
    initializeXOSecretFuncPass (*PassRegistry::getPassRegistry());
  }
  bool runOnModule(Module& M) override;

  // generates and inserts the XO Secret Function for initialized yolk
  void buildXOfunc(Module& M, GlobalVariable *g, Type *ArrTy, std::string Name);

  //Yolk is int or const int
  bool processYolkSingle(Module& M, Value* senPtr);

  //Yolk is int array
  bool processYolkArray(Module& M, Value* senPtr);


  //Yolk is constant int array
  bool processConstYolkArray(Module& M, GlobalVariable *yolkG);

  //Put yolk constants in yolkSet
  void findYolkAnnotations(Module& M);

  std::unordered_set<GlobalVariable *> yolkSet;

  std::unordered_set<Instruction *> ToErase;
  std::unordered_set<Instruction *> ToEraseGEP;
  std::unordered_set<GlobalVariable *> ToEraseGlobal;

};
}


char XOSecretFunc::ID = 0;
INITIALIZE_PASS(XOSecretFunc, "xo-secret-func", "XOSecretFunc", false, false)

ModulePass *llvm::createXOSecretFuncPass() {
  return new XOSecretFunc();
}

void XOSecretFunc::buildXOfunc(Module& M, GlobalVariable *g, Type *ArrTy, 
                               std::string senName) {
  std::string fName = senName + "_XO_Sec";
  std::string vName;
  Type *returnTy = ArrTy->getArrayElementType();
  unsigned numCases = ArrTy->getArrayNumElements();
  FunctionType *Ty = FunctionType::get(returnTy, {IntegerType::get(M.getContext(), 64)}, false);
  Function *xoArrFn = dyn_cast<Function>(M.getOrInsertFunction(fName, Ty));
  if (!xoArrFn->hasFnAttribute(Attribute::AlwaysInline)) {
    xoArrFn->addFnAttr(Attribute::AlwaysInline);
    assert(xoArrFn->hasFnAttribute(Attribute::AlwaysInline));
  }
  Function::arg_iterator args = xoArrFn->arg_begin();
  Value *index = &(*args);
  index->setName("index");
  DEBUG(dbgs() << "create entry block\n");
  auto entry = BasicBlock::Create(M.getContext(), "entry", xoArrFn);
  IRBuilder<> IRB(entry);
  auto defaultBB = BasicBlock::Create(M.getContext(), "sw.default", xoArrFn);

  SwitchInst *switchInst = IRB.CreateSwitch(index, defaultBB, numCases);
  for (unsigned i = 0; i < numCases; i++) {
    auto caseBB = BasicBlock::Create(M.getContext(), "sw.bb" + std::to_string(i), xoArrFn);
    ConstantInt *OnVal = ConstantInt::get(IntegerType::get(M.getContext(), 64), i);
    switchInst->addCase(OnVal, caseBB);
    IRB.SetInsertPoint(caseBB);
    InlineAsm *AsmFn;
    // initialized yolk
    if (g && !isa<ConstantAggregateZero>(g->getInitializer())) {
      Constant *init = g->getInitializer();
      DEBUG(dbgs() << "Constant " << *init << "\n");
      Type *gTy = g->getType();
      DEBUG(gTy->dump());
      ConstantDataArray *numArr = cast<ConstantDataArray>(init);
      Constant *numC = numArr->getElementAsConstant(i);
      ConstantInt *num = cast<ConstantInt>(numC);
      AsmFn = generateAsm(num);
     }
     // uninitialized yolk
     else {
       if (returnTy->isIntegerTy(32))
         vName = "yolk_32_" + senName + "_" + std::to_string(i);
       else
         vName = "yolk_64_" + senName + "_" + std::to_string(i);  
       AsmFn = generateAsmNoInit(returnTy, vName);
     }
     CallInst *callInst = IRB.CreateCall(AsmFn, {});
     // attach metadata to InlineAsm
     //if (!g) {
       //MDNode *metaD = MDNode::get(M.getContext(), MDString::get(M.getContext(), vName));
       //callInst->setMetadata(vName, metaD);
       //GlobalVariable *yolk = new GlobalVariable(M, returnTy, false, GlobalValue::LinkageTypes::CommonLinkage, Constant::getNullValue(returnTy), vName, 0, GlobalValue::ThreadLocalMode::NotThreadLocal, 0, false);
       //yolk->setSection(".yolk"); 
     //} 
     IRB.CreateRet(callInst);
  }
  IRB.SetInsertPoint(defaultBB);
  Ty = FunctionType::get(Type::getVoidTy(M.getContext()), {}, false);
  Function *debugTrapFn = dyn_cast<Function>(M.getOrInsertFunction("llvm.debugtrap", Ty));
  IRB.CreateCall(debugTrapFn, {});
  IRB.CreateUnreachable();
  DEBUG(dbgs() << "function is " << *xoArrFn << "\n");
}


bool XOSecretFunc::processYolkSingle(Module& M, Value* senPtr) {
  bool changed = false;
  bool hasInit = false;
  for (auto senUse = senPtr->use_begin(); senUse != senPtr->use_end(); senUse++) {
    User *U = senUse->getUser();
    if (auto senStore = dyn_cast<StoreInst>(U)) {
      hasInit = true;
      // get the source of Store instruction 
      Value *op0 = senStore->getOperand(0);
      // if the source is immediate, replace it with a call to inline assembly
      if (ConstantInt *num = dyn_cast<ConstantInt>(op0)) {
        IRBuilder<> IRB(senStore);
        InlineAsm *AsmFn = generateAsm(num);
        CallInst *callInst = IRB.CreateCall(AsmFn, {} );
        DEBUG(dbgs() << *callInst << "\n");
        op0->replaceAllUsesWith(callInst);
        changed = true;
      }
    }
  }
  // uninitialized yolk
  if (!hasInit) {
    for (auto senUse = senPtr->use_begin(); senUse != senPtr->use_end(); senUse++) {
      User *U = senUse->getUser();
      if (auto senLoad = dyn_cast<LoadInst>(U)) {
        IRBuilder<> IRB(senLoad);
        Type *returnTy = senPtr->getType()->getPointerElementType();
        std::string sourceName;
        if (returnTy->isIntegerTy(32))
          sourceName = "yolk_32_" + senPtr->getName().str();
        else
          sourceName = "yolk_64_" + senPtr->getName().str(); 
        InlineAsm *AsmFn = generateAsmNoInit(returnTy, sourceName);
        CallInst *callInst = IRB.CreateCall(AsmFn, {} );
        // attach metadata to InlineAsm
        //MDNode *metaD = MDNode::get(M.getContext(), MDString::get(M.getContext(), sourceName));
        //callInst->setMetadata(sourceName, metaD);
       // GlobalVariable *yolk = new GlobalVariable(M, returnTy, false, GlobalValue::LinkageTypes::CommonLinkage, Constant::getNullValue(returnTy), sourceName, 0, GlobalValue::ThreadLocalMode::NotThreadLocal, 0, false);
       //yolk->setSection(".yolk");  

        DEBUG(dbgs() << *callInst << "\n");
        senLoad->replaceAllUsesWith(callInst);
        changed = true;
        ToErase.insert(senLoad);
      }
    }
  }
  return changed;
}
                 
bool XOSecretFunc::processYolkArray(Module& M, Value* senPtr) {
  bool changed = false;
  
  GlobalVariable *globalArr = NULL;
  for (auto senUse = senPtr->use_begin(); senUse != senPtr->use_end(); senUse++) {
    User *U = senUse->getUser();
    if (auto cast = dyn_cast<CastInst>(U)) {
      for (auto castUse = cast->use_begin(); castUse != cast->use_end(); castUse++) {
        User *castU = castUse->getUser();
        if (CallInst *call = dyn_cast<CallInst>(castU)) {
          if (Function *F = call->getCalledFunction()) {
            // memcpy copies a glbal constant array to the local yolk array
            if (F->getName().startswith("llvm.memcpy") && call->getArgOperand(0) == cast) {
              Value *op1 = call->getArgOperand(1);
              Type *op1Ty = op1->getType();
              op1Ty->dump();
              Value *newop = op1->stripPointerCasts();
              DEBUG(dbgs() << "newop " << *newop << "\n");

              if (globalArr = dyn_cast<GlobalVariable>(newop)) {
                DEBUG(dbgs() << "global constant array: " << *globalArr << "\n");
              }
            ToErase.insert(call);
            break;
            }
          }
        }
      }
    }
  }

  // accessing the array element
  bool firstGEP = true;
  Type *ArrTy = senPtr->getType()->getPointerElementType();
  for (auto senUse = senPtr->use_begin(); senUse != senPtr->use_end(); senUse++) {
    //if (!globalArr)
    //  break;

    User *U = senUse->getUser();
    if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(U)) {
       bool hasUse = false;
       DEBUG(dbgs() << "gep is " << *gep << "\n");
       Use *op = gep->idx_begin() + 1;
       Value *ArrIdx = op->get();
       DEBUG(dbgs() << "index value is " << *ArrIdx << "\n");
             
      for (auto valUse = gep->use_begin(); valUse != gep->use_end(); valUse++) {
        User *valU = valUse->getUser();
        if (LoadInst *load = dyn_cast<LoadInst>(valU)) {
          // build and insert the XO Secret Function
          if (firstGEP) {
            std::string name = senPtr->getName().str();
            buildXOfunc(M, globalArr, ArrTy, name);
            firstGEP = false;
            DEBUG(dbgs() << "build " << senPtr->getName() << "_XO_Sec");
          }
          IRBuilder<> IRB(load);
          std::string name = senPtr->getName().str() + "_XO_Sec";
          if (Function *XOFn = M.getFunction(name)) {
            ArrayRef<Value *> Args(ArrIdx);
            CallInst *callInst = IRB.CreateCall(XOFn, Args);
            DEBUG(dbgs() << *callInst << "\n");
            load->replaceAllUsesWith(callInst);
            changed = true;
            DEBUG(dbgs() << "done replacing a load \n");
            ToErase.insert(load);
          }
          else
            hasUse = true;               
        }
        else
          hasUse = true;
      } 
      if (!hasUse)
        ToEraseGEP.insert(gep);
    } 
  }
  
  if (globalArr)
    ToEraseGlobal.insert(globalArr);
  return changed;
}
 
bool XOSecretFunc::processConstYolkArray(Module& M, GlobalVariable *yolkG) {
  bool firstGEP = true;
  bool changed = false;
  // change gep constant expression to gep instruction
  for (auto senUse = yolkG->use_begin(); senUse != yolkG->use_end(); senUse++) {

    User *U = senUse->getUser();
    DEBUG(dbgs() << "user of yolk constant array " << *U << "\n");
    if (ConstantExpr *expr = dyn_cast<ConstantExpr>(U)) {
      DEBUG(dbgs() << "\nconstant expr is " << *expr << "\n");
      if (expr->getOpcode() == Instruction::GetElementPtr) {
        DEBUG(dbgs() << "is gep constant expr\n");
        bool firstLoad = true;
        Instruction *exprI = expr->getAsInstruction();
        for (auto valUse = expr->use_begin(); valUse != expr->use_end(); valUse++) {
          User *valU = valUse->getUser();
          if (LoadInst *load = dyn_cast<LoadInst>(valU)) {
            if (firstLoad) {
              exprI->insertBefore(load);
              firstLoad = false;
             }
           load->replaceUsesOfWith(expr, exprI);
          }
        }
      } 
    }
  }
  // process each gep instruction
  Type *ArrTy = yolkG->getType()->getPointerElementType();
  for (auto senUse = yolkG->use_begin(); senUse != yolkG->use_end(); senUse++) {
    // build and insert the XO Secret Function
    if (firstGEP) {
      std::string name = yolkG->getName().str();
      buildXOfunc(M, yolkG, ArrTy, name);
      firstGEP = false;
      DEBUG(dbgs() << "build " << yolkG->getName() << "_XO_Sec");
    }

    User *U = senUse->getUser();
    DEBUG(dbgs() << "user of yolk constant array " << *U << "\n");

    if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(U)) {
       bool hasUse = false;
       DEBUG(dbgs() << "gep is " << *gep << "\n");
       Use *op = gep->idx_begin() + 1;
       Value *ArrIdx = op->get(); 
       DEBUG(dbgs() << "index value is " << *ArrIdx << "\n");
             
      for (auto valUse = gep->use_begin(); valUse != gep->use_end(); valUse++) {
        User *valU = valUse->getUser();
        if (LoadInst *load = dyn_cast<LoadInst>(valU)) {
          IRBuilder<> IRB(load);
          std::string name = yolkG->getName().str() + "_XO_Sec";
          if (Function *XOFn = M.getFunction(name)) {
            ArrayRef<Value *> Args(ArrIdx);
            CallInst *callInst = IRB.CreateCall(XOFn, Args);
            DEBUG(dbgs() << *callInst << "\n");
            load->replaceAllUsesWith(callInst);
            changed = true;
            DEBUG(dbgs() << "done replacing a load \n");
            ToErase.insert(load);
          }
          else
            hasUse = true;
        }
        else
          hasUse = true;
      }
      if (!hasUse)
        ToEraseGEP.insert(gep);
    }
  }
  ToEraseGlobal.insert(yolkG);
  return changed;
}  
 
void XOSecretFunc::findYolkAnnotations(Module& M) {
  if (auto anno = M.getNamedGlobal("llvm.global.annotations")) {
    GlobalVariable *strG = NULL;
    DEBUG(dbgs() << "global annotation: " << *anno << "\n");
    Value *op0 = anno->getOperand(0);
    if (ConstantArray *arr = dyn_cast<ConstantArray>(op0)) {
      std::vector<Constant *> otherAnnos;
      for (unsigned i = 0; i< arr->getNumOperands(); i++) {
        bool deleteAnno = false;
        if (ConstantStruct *annoStruct = dyn_cast<ConstantStruct>(arr->getOperand(i))) {
          Value *cast = annoStruct->getOperand(0);
          Value *annoStr = annoStruct->getOperand(1);
          DEBUG(dbgs() << "first " << *cast << " second " << *annoStr << "\n");
          if (GlobalVariable *yolkG = dyn_cast<GlobalVariable>(cast->stripPointerCasts())) {
            if (strG && annoStr->stripPointerCasts() == strG) {
              DEBUG(dbgs() <<"yolk constant " << *yolkG << "\n");
              yolkSet.insert(yolkG);
              deleteAnno = true;
            }
            else {
              if (GlobalVariable *strS = dyn_cast<GlobalVariable>(annoStr->stripPointerCasts())) {
                auto stringInit = strS->getInitializer();
                if (auto stringArr = dyn_cast<ConstantDataArray>(stringInit)) {
                  DEBUG(dbgs() << "array\n");
                  if (stringArr->isString() && stringArr->getAsString().startswith("yolk") && stringArr->getNumElements() == 5) {
                    DEBUG(dbgs() << "yolk\n");
                    strG = strS;
                    yolkSet.insert(yolkG);
                    deleteAnno = true;
                  }
                }
              }
            } 
          }
          if (!deleteAnno) 
            otherAnnos.push_back(annoStruct);
        }
      }
      // if there are non-yolk annotations, create 
      // a new llvm.global.annotations and delete the old one
      if (!otherAnnos.empty()) {
        Constant *newArr = ConstantArray::get(ArrayType::get(otherAnnos[0]->getType(), otherAnnos.size()), otherAnnos);
        auto AnnoSection = anno->getSection();
        anno->eraseFromParent();
        auto *newAnno = new GlobalVariable(M, newArr->getType(), false,
                                           GlobalVariable::AppendingLinkage,
                                         newArr, "llvm.global.annotations");
        newAnno->setSection("llvm.metadata");
        DEBUG(dbgs() << "new global annoation : " << *newAnno << "\n");
      }
      // there are no non-yolk annotaions, delete llvm.global.annotations
      else
        anno->eraseFromParent();
    }
  }
}


bool XOSecretFunc::runOnModule(Module& M) {
  DEBUG(dbgs() << "before XOSecretFunc pass: " << M << "\n");
  bool changed = false;
  ToErase.clear();
  ToEraseGEP.clear();
  ToEraseGlobal.clear();

  yolkSet.clear();
  findYolkAnnotations(M);
 
   // process yolks from global annotations 
   for (GlobalVariable *yolkG : yolkSet) {
    DEBUG(dbgs() << "yolk " << *yolkG << "\n");
    Type* senEleTy = yolkG->getType()->getPointerElementType();
      if (senEleTy->isIntegerTy()) {
        DEBUG(dbgs() << "int\n");
      //  changed |= processYolkSingle(M, yolkG);
      }
      else if (senEleTy->isArrayTy()) {
        DEBUG(dbgs() << "array\n");
        Type* arrEleTy = senEleTy->getArrayElementType();
        if (arrEleTy->isIntegerTy()) {
          DEBUG(dbgs() << "int array\n");
          changed |= processConstYolkArray(M, yolkG);
        }
      }
    }


  // process yolks from local annotations
  for (auto& F : M) {
    DEBUG(dbgs() << "function is " << F.getName() << "\n");
    if (F.isDeclaration()) {
      DEBUG(dbgs() << "declaration\n");
      continue;
    }
    for (inst_iterator It = inst_begin(F); It != inst_end(F); It++) {
      Instruction *I = &(*It);
      if (auto call = dyn_cast<CallInst>(I)) {
        if (call->getCalledFunction() &&
            call->getCalledFunction()->getName() == "llvm.var.annotation") {
          Value *arg0 = call->getArgOperand(0);
          DEBUG(dbgs() << *arg0 << "\n");
          Value *stringV = call->getArgOperand(1)->stripPointerCasts(); 
          DEBUG(dbgs() << "string is " << *stringV << "\n");  
          if (auto stringG = dyn_cast<GlobalVariable>(stringV)) {
            auto stringInit = stringG->getInitializer(); 
            if (auto stringArr = dyn_cast<ConstantDataArray>(stringInit)) {
              DEBUG(dbgs() << "array\n");
              if (stringArr->isString() && stringArr->getAsString().startswith("yolk") && stringArr->getNumElements() == 5) {
                DEBUG(dbgs() << "yolk\n");
                Value *senPtr = arg0;
                if (auto cast = dyn_cast<CastInst>(arg0)) 
                  senPtr = cast->getOperand(0);       
                DEBUG(dbgs() << *senPtr << "\n");
                Type* senPtrTy = senPtr->getType();
                DEBUG(dbgs() << "type is " );
                senPtrTy->dump();
                Type* senEleTy = senPtrTy->getPointerElementType();
                if (senEleTy->isIntegerTy()) {
                  DEBUG(dbgs() << "int\n");
                  changed |= processYolkSingle(M, senPtr);
                }
                else if (senEleTy->isArrayTy()) {
                  DEBUG(dbgs() << "array\n");
                  Type* arrEleTy = senEleTy->getArrayElementType();
                    if (arrEleTy->isIntegerTy()) {
                      DEBUG(dbgs() << "int array\n");
                      changed |= processYolkArray(M, senPtr);
                    }
                }  
                else if (senEleTy->isStructTy()) {
                  DEBUG(dbgs() << "struct\n");
                }
              }  
            }
          }
        }
      }
    }
  }



  for (Instruction *I : ToErase) {
    DEBUG(dbgs() << "erase " << *I << "\n");
    I->eraseFromParent();
  }
  DEBUG(dbgs() << "done erase\n");
  for (Instruction *I : ToEraseGEP) {
    DEBUG(dbgs() << "erase " << *I << "\n");
    I->eraseFromParent();
  }
  DEBUG(dbgs() << "done erase gep\n");
  for (GlobalVariable *g : ToEraseGlobal){
    DEBUG(dbgs() << "erase " << *g << "\n");
    g->eraseFromParent();
  }
  DEBUG(dbgs() << "done erase global\n");

  return changed;
}
