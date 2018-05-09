//===----- X86SafeStackBoundsCheckingCombiner.cpp - Combine checks --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that analyzes MPX BNDCU instructions inserted by the
// X86SafeStackBoundsChecking pass to combine redundant checks and eliminate
// unneeded checks.
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetInstrInfo.h"
#include <forward_list>
#include <unordered_set>

using namespace llvm;

#define SAFESTKBCCOMBINER_DESC "X86 Safe Stack Bounds Checking Combiner"
#define SAFESTKBCCOMBINER_NAME "x86-safestack-bounds-checking-combiner"
#define DEBUG_TYPE SAFESTKBCCOMBINER_NAME
#define DEBUG_LABEL "[X86SafeStackBoundsCheckingCombiner] "

namespace {

class X86SafeStackBoundsCheckingCombiner : public MachineFunctionPass {
public:
  static char ID;

  X86SafeStackBoundsCheckingCombiner();

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  llvm::StringRef getPassName() const override {
    return SAFESTKBCCOMBINER_DESC;
  }

  bool processInstr(MachineInstr *MI);

  const TargetInstrInfo *TII;
  const X86RegisterInfo *TRI;
  std::unordered_set<MachineInstr *> PrevBndChks;
  std::unordered_set<MachineInstr *> ToErase;
};

char X86SafeStackBoundsCheckingCombiner::ID = 0;
}

INITIALIZE_PASS(X86SafeStackBoundsCheckingCombiner,
                SAFESTKBCCOMBINER_NAME, SAFESTKBCCOMBINER_DESC, false, false)

FunctionPass *llvm::createX86SafeStackBoundsCheckingCombinerPass() {
  return new X86SafeStackBoundsCheckingCombiner();
}

X86SafeStackBoundsCheckingCombiner::X86SafeStackBoundsCheckingCombiner()
  : MachineFunctionPass(ID) {

  initializeX86SafeStackBoundsCheckingCombinerPass(*PassRegistry::getPassRegistry());
}

bool X86SafeStackBoundsCheckingCombiner::processInstr(MachineInstr *MI) {
  X86AddressMode AM;
  bool IsMem = false, IsBndc = false;

  constexpr unsigned BNDCU_IASM_MEM_OP = 3;

  std::forward_list<MachineInstr *> PrevToErase;
  for (MachineInstr *PrevMI : PrevBndChks) {
    for (MachineOperand &Op : PrevMI->uses()) {
      if (!(Op.isReg() && MI->definesRegister(Op.getReg())))
        continue;

      DEBUG(dbgs() << DEBUG_LABEL << "Register " << Op.getReg() << " used by "; PrevMI->print(dbgs());
            dbgs() << " is redefined by "; MI->print(dbgs()); dbgs() << "\n");

      PrevToErase.push_front(PrevMI);
      break;
    }
  }
  for (MachineInstr *PrevMI : PrevToErase)
    PrevBndChks.erase(PrevMI);

  unsigned Opcode = MI->getOpcode();

  const MCInstrDesc &Desc = TII->get(Opcode);
  uint64_t TSFlags = Desc.TSFlags;

  // Determine where the memory operand starts:
  int MemoryOperand = -1;
  if (MI->mayStore())
    MemoryOperand = X86II::getMemoryOperandNo(TSFlags);
  if (MemoryOperand == -1) {
    if (!MI->isInlineAsm())
      return false;

    const char *IAsmStr = MI->getOperand(0).getSymbolName();
    if (strcmp(IAsmStr, "bndcu $0, %bnd0") != 0)
      return false;

    IsMem = true;
    IsBndc = true;

    MemoryOperand = BNDCU_IASM_MEM_OP;
  } else {
    DEBUG(dbgs() << DEBUG_LABEL
                 << "Found store: ";
          MI->print(dbgs()); dbgs() << "\n");

    MemoryOperand += X86II::getOperandBias(Desc);

    IsMem = true;
  }

  if (!IsMem)
    return false;

  AM = getAddressFromInstr(MI, MemoryOperand);

  if (!IsBndc)
    return false;

  DEBUG(dbgs() << DEBUG_LABEL
               << "Identified bound check: ";
        MI->print(dbgs()); dbgs() << "\n");

  MachineOperand &SegRegOp = MI->getOperand(MemoryOperand + X86::AddrSegmentReg);
  if (TRI->isPhysicalRegister(SegRegOp.getReg())) {
    // This erases any bound checks with segment override prefixes that may be
    // emitted by the X86SafeStackBoundsChecking pass, since that pass may not
    // detect all such thread-local accesses.
    DEBUG(dbgs() << DEBUG_LABEL << "Erasing bound check with segment override "
                 << "prefix.\n");
    ToErase.insert(MI);
    return false;
  }

  assert(AM.BaseType == X86AddressMode::RegBase);

  unsigned BaseReg = AM.Base.Reg;
  unsigned IndexReg = AM.IndexReg;

  bool BaseIsPhysReg = TRI->isPhysicalRegister(BaseReg);
  bool IndexIsPhysReg = TRI->isPhysicalRegister(IndexReg);

  if (!(BaseIsPhysReg || IndexIsPhysReg)) {
    DEBUG(dbgs() << DEBUG_LABEL << "Memory operand without a base or index "
                 << "register assumed to be safe.\n");
    ToErase.insert(MI);
    return false;
  }

  if (BaseReg == X86::RIP && !IndexIsPhysReg) {
    DEBUG(dbgs() << DEBUG_LABEL << "Memory operand with simple RIP-relative "
                 << "displacement assumed to be safe.\n");
    ToErase.insert(MI);
    return false;
  }

  auto hasSameBaseMemOp = [&](const X86AddressMode &ChkAM) {
    return AM.Base.Reg == ChkAM.Base.Reg &&
        AM.IndexReg == ChkAM.IndexReg &&
        AM.Scale == ChkAM.Scale &&
        AM.GV == ChkAM.GV &&
        AM.GVOpFlags == ChkAM.GVOpFlags;
  };

  for (MachineInstr *PrevMI : PrevBndChks) {
    X86AddressMode ChkAM = getAddressFromInstr(PrevMI, BNDCU_IASM_MEM_OP);
    if (!hasSameBaseMemOp(ChkAM)) continue;

    DEBUG(dbgs() << DEBUG_LABEL
                 << "Eliding check by merging with earlier one: ";
          PrevMI->print(dbgs()); dbgs() << "\n");

    ToErase.insert(MI);

    MachineOperand &DispOp =
        PrevMI->getOperand(BNDCU_IASM_MEM_OP + X86::AddrDisp);
    if (DispOp.isImm()) {
      auto CurrOff = DispOp.getImm();
      if (AM.Disp <= CurrOff)
        return false;

      DispOp.setImm(AM.Disp);
    } else {
      auto CurrOff = DispOp.getOffset();
      if (AM.Disp <= CurrOff)
        return false;

      DispOp.setOffset(AM.Disp);
    }

    return true;
  }

  PrevBndChks.insert(MI);

  return false;
}

bool X86SafeStackBoundsCheckingCombiner::runOnMachineFunction(MachineFunction &MF) {
  const X86Subtarget *STI = &MF.getSubtarget<X86Subtarget>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();

  if (!(STI->useSeparateStackSeg() && STI->is64Bit()))
    return false;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB)
      Changed |= processInstr(&MI);

    PrevBndChks.clear();
  }

  for (MachineInstr *MI : ToErase) {
    MI->eraseFromParent();
    Changed = true;
  }
  ToErase.clear();

  return Changed;
}
