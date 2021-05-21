//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// All symbols are handled as SymbolBodies regardless of their types.
// This file defines various types of SymbolBodies.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_SYMBOLS_H
#define LLD_ELF_SYMBOLS_H

#include "InputSection.h"
#include "Strings.h"

#include "lld/Core/LLVM.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELF.h"

namespace lld {
namespace elf {

class ArchiveFile;
class BitcodeFile;
class InputFile;
class LazyObjectFile;
template <class ELFT> class ObjectFile;
template <class ELFT> class OutputSection;
class OutputSectionBase;
template <class ELFT> class SharedFile;

struct Symbol;

// The base class for real symbol classes.
class SymbolBody {
public:
  enum Kind {
    DefinedFirst,
    DefinedRegularKind = DefinedFirst,
    SharedKind,
    DefinedCommonKind,
    DefinedSyntheticKind,
    DefinedLast = DefinedSyntheticKind,
    UndefinedKind,
    LazyArchiveKind,
    LazyObjectKind,
  };

  SymbolBody(Kind K) : SymbolKind(K) {}

  Symbol *symbol();
  const Symbol *symbol() const {
    return const_cast<SymbolBody *>(this)->symbol();
  }

  Kind kind() const { return static_cast<Kind>(SymbolKind); }

  bool isUndefined() const { return SymbolKind == UndefinedKind; }
  bool isDefined() const { return SymbolKind <= DefinedLast; }
  bool isCommon() const { return SymbolKind == DefinedCommonKind; }
  bool isLazy() const {
    return SymbolKind == LazyArchiveKind || SymbolKind == LazyObjectKind;
  }
  bool isShared() const { return SymbolKind == SharedKind; }
  bool isLocal() const { return IsLocal; }
  bool isPreemptible() const;
  StringRef getName() const { return Name; }
  uint8_t getVisibility() const { return StOther & 0x3; }
  void parseSymbolVersion();

  bool isInGot() const { return GotIndex != -1U; }
  bool isInPlt() const { return PltIndex != -1U; }
  template <class ELFT> bool hasThunk() const;

  template <class ELFT>
  typename ELFT::uint getVA(typename ELFT::uint Addend = 0) const;

  template <class ELFT> typename ELFT::uint getGotOffset() const;
  template <class ELFT> typename ELFT::uint getGotVA() const;
  template <class ELFT> typename ELFT::uint getGotPltOffset() const;
  template <class ELFT> typename ELFT::uint getGotPltVA() const;
  template <class ELFT> typename ELFT::uint getPltVA() const;
  template <class ELFT> typename ELFT::uint getThunkVA() const;
  template <class ELFT> typename ELFT::uint getSize() const;

  // The file from which this symbol was created.
  InputFile *File = nullptr;

  uint32_t DynsymIndex = 0;
  uint32_t GotIndex = -1;
  uint32_t GotPltIndex = -1;
  uint32_t PltIndex = -1;
  uint32_t GlobalDynIndex = -1;

protected:
  SymbolBody(Kind K, StringRefZ Name, bool IsLocal, uint8_t StOther,
             uint8_t Type);

  const unsigned SymbolKind : 8;

public:
  // True if the linker has to generate a copy relocation for this shared
  // symbol or if the symbol should point to its plt entry.
  unsigned NeedsCopyOrPltAddr : 1;

  // True if this is a local symbol.
  unsigned IsLocal : 1;

  // True if this symbol has an entry in the global part of MIPS GOT.
  unsigned IsInGlobalMipsGot : 1;

  // True if this symbol is referenced by 32-bit GOT relocations.
  unsigned Is32BitMipsGot : 1;

  // True if this symbol is in the Iplt sub-section of the Plt.
  unsigned IsInIplt : 1;

  // True if this symbol is in the Igot sub-section of the .got.plt or .got.
  unsigned IsInIgot : 1;

  // True if this is a shared symbol in a read-only segment which requires a
  // copy relocation. This causes space for the symbol to be allocated in the
  // .bss.rel.ro section.
  unsigned CopyIsInBssRelRo : 1;

  // The following fields have the same meaning as the ELF symbol attributes.
  uint8_t Type;    // symbol type
  uint8_t StOther; // st_other field value

  // The Type field may also have this value. It means that we have not yet seen
  // a non-Lazy symbol with this name, so we don't know what its type is. The
  // Type field is normally set to this value for Lazy symbols unless we saw a
  // weak undefined symbol first, in which case we need to remember the original
  // symbol's type in order to check for TLS mismatches.
  enum { UnknownType = 255 };

  bool isSection() const { return Type == llvm::ELF::STT_SECTION; }
  bool isTls() const { return Type == llvm::ELF::STT_TLS; }
  bool isFunc() const { return Type == llvm::ELF::STT_FUNC; }
  bool isGnuIFunc() const { return Type == llvm::ELF::STT_GNU_IFUNC; }
  bool isObject() const { return Type == llvm::ELF::STT_OBJECT; }
  bool isFile() const { return Type == llvm::ELF::STT_FILE; }

protected:
  StringRefZ Name;
};

// The base class for any defined symbols.
class Defined : public SymbolBody {
public:
  Defined(Kind K, StringRefZ Name, bool IsLocal, uint8_t StOther, uint8_t Type);
  static bool classof(const SymbolBody *S) { return S->isDefined(); }
};

class DefinedCommon : public Defined {
public:
  DefinedCommon(StringRef N, uint64_t Size, uint64_t Alignment, uint8_t StOther,
                uint8_t Type, InputFile *File);

  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::DefinedCommonKind;
  }

  // The output offset of this common symbol in the output bss. Computed by the
  // writer.
  uint64_t Offset;

  // The maximum alignment we have seen for this symbol.
  uint64_t Alignment;

  uint64_t Size;
};

// Regular defined symbols read from object file symbol tables.
template <class ELFT> class DefinedRegular : public Defined {
  typedef typename ELFT::Sym Elf_Sym;
  typedef typename ELFT::uint uintX_t;

public:
  DefinedRegular(StringRefZ Name, bool IsLocal, uint8_t StOther, uint8_t Type,
                 uintX_t Value, uintX_t Size, InputSectionBase<ELFT> *Section,
                 InputFile *File)
      : Defined(SymbolBody::DefinedRegularKind, Name, IsLocal, StOther, Type),
        Value(Value), Size(Size),
        Section(Section ? Section->Repl : NullInputSection) {
    this->File = File;
  }

  // Return true if the symbol is a PIC function.
  bool isMipsPIC() const;

  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::DefinedRegularKind;
  }

  uintX_t Value;
  uintX_t Size;

  // The input section this symbol belongs to. Notice that this is
  // a reference to a pointer. We are using two levels of indirections
  // because of ICF. If ICF decides two sections need to be merged, it
  // manipulates this Section pointers so that they point to the same
  // section. This is a bit tricky, so be careful to not be confused.
  // If this is null, the symbol is an absolute symbol.
  InputSectionBase<ELFT> *&Section;

  // If non-null the symbol has a Thunk that may be used as an alternative
  // destination for callers of this Symbol.
  Thunk<ELFT> *ThunkData = nullptr;

private:
  static InputSectionBase<ELFT> *NullInputSection;
};

template <class ELFT>
InputSectionBase<ELFT> *DefinedRegular<ELFT>::NullInputSection;

// DefinedSynthetic is a class to represent linker-generated ELF symbols.
// The difference from the regular symbol is that DefinedSynthetic symbols
// don't belong to any input files or sections. Thus, its constructor
// takes an output section to calculate output VA, etc.
// If Section is null, this symbol is relative to the image base.
class DefinedSynthetic : public Defined {
public:
  DefinedSynthetic(StringRef Name, uint64_t Value,
                   const OutputSectionBase *Section)
      : Defined(SymbolBody::DefinedSyntheticKind, Name, /*IsLocal=*/false,
                llvm::ELF::STV_HIDDEN, 0 /* Type */),
        Value(Value), Section(Section) {}

  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::DefinedSyntheticKind;
  }

  uint64_t Value;
  const OutputSectionBase *Section;
};

template <class ELFT> class Undefined : public SymbolBody {
public:
  Undefined(StringRefZ Name, bool IsLocal, uint8_t StOther, uint8_t Type,
            InputFile *F);

  static bool classof(const SymbolBody *S) {
    return S->kind() == UndefinedKind;
  }

  // If non-null the symbol has a Thunk that may be used as an alternative
  // destination for callers of this Symbol. When linking a DSO undefined
  // symbols are implicitly imported, the symbol lookup will be performed by
  // the dynamic loader. A call to an undefined symbol will be given a PLT
  // entry and on ARM this may need a Thunk if the caller is in Thumb state.
  Thunk<ELFT> *ThunkData = nullptr;
  InputFile *file() { return this->File; }
};

template <class ELFT> class SharedSymbol : public Defined {
  typedef typename ELFT::Sym Elf_Sym;
  typedef typename ELFT::Verdef Elf_Verdef;
  typedef typename ELFT::uint uintX_t;

public:
  static bool classof(const SymbolBody *S) {
    return S->kind() == SymbolBody::SharedKind;
  }

  SharedSymbol(SharedFile<ELFT> *F, StringRef Name, const Elf_Sym &Sym,
               const Elf_Verdef *Verdef)
      : Defined(SymbolBody::SharedKind, Name, /*IsLocal=*/false, Sym.st_other,
                Sym.getType()),
        Sym(Sym), Verdef(Verdef) {
    // IFuncs defined in DSOs are treated as functions by the static linker.
    if (isGnuIFunc())
      Type = llvm::ELF::STT_FUNC;
    this->File = F;
  }

  SharedFile<ELFT> *file() { return (SharedFile<ELFT> *)this->File; }

  const Elf_Sym &Sym;

  // This field is a pointer to the symbol's version definition.
  const Elf_Verdef *Verdef;

  // CopyOffset is significant only when needsCopy() is true.
  uintX_t CopyOffset = 0;

  // If non-null the symbol has a Thunk that may be used as an alternative
  // destination for callers of this Symbol.
  Thunk<ELFT> *ThunkData = nullptr;
  bool needsCopy() const { return this->NeedsCopyOrPltAddr && !this->isFunc(); }

  OutputSection<ELFT> *getBssSectionForCopy() const;
};

// This class represents a symbol defined in an archive file. It is
// created from an archive file header, and it knows how to load an
// object file from an archive to replace itself with a defined
// symbol. If the resolver finds both Undefined and Lazy for
// the same name, it will ask the Lazy to load a file.
class Lazy : public SymbolBody {
public:
  static bool classof(const SymbolBody *S) { return S->isLazy(); }

  // Returns an object file for this symbol, or a nullptr if the file
  // was already returned.
  InputFile *fetch();

protected:
  Lazy(SymbolBody::Kind K, StringRef Name, uint8_t Type)
      : SymbolBody(K, Name, /*IsLocal=*/false, llvm::ELF::STV_DEFAULT, Type) {}
};

// LazyArchive symbols represents symbols in archive files.
class LazyArchive : public Lazy {
public:
  LazyArchive(ArchiveFile &File, const llvm::object::Archive::Symbol S,
              uint8_t Type);

  static bool classof(const SymbolBody *S) {
    return S->kind() == LazyArchiveKind;
  }

  ArchiveFile *file() { return (ArchiveFile *)this->File; }
  InputFile *fetch();

private:
  const llvm::object::Archive::Symbol Sym;
};

// LazyObject symbols represents symbols in object files between
// --start-lib and --end-lib options.
class LazyObject : public Lazy {
public:
  LazyObject(StringRef Name, LazyObjectFile &File, uint8_t Type);

  static bool classof(const SymbolBody *S) {
    return S->kind() == LazyObjectKind;
  }

  LazyObjectFile *file() { return (LazyObjectFile *)this->File; }
  InputFile *fetch();
};

// Some linker-generated symbols need to be created as
// DefinedRegular symbols.
template <class ELFT> struct ElfSym {
  // The content for __ehdr_start symbol.
  static DefinedRegular<ELFT> *EhdrStart;

  // The content for _etext and etext symbols.
  static DefinedRegular<ELFT> *Etext;
  static DefinedRegular<ELFT> *Etext2;

  // The content for _edata and edata symbols.
  static DefinedRegular<ELFT> *Edata;
  static DefinedRegular<ELFT> *Edata2;

  // The content for _end and end symbols.
  static DefinedRegular<ELFT> *End;
  static DefinedRegular<ELFT> *End2;

  // The content for _gp_disp/__gnu_local_gp symbols for MIPS target.
  static DefinedRegular<ELFT> *MipsGpDisp;
  static DefinedRegular<ELFT> *MipsLocalGp;
  static DefinedRegular<ELFT> *MipsGp;
};

template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::EhdrStart;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Etext;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Etext2;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Edata;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::Edata2;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::End;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::End2;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::MipsGpDisp;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::MipsLocalGp;
template <class ELFT> DefinedRegular<ELFT> *ElfSym<ELFT>::MipsGp;

// A real symbol object, SymbolBody, is usually stored within a Symbol. There's
// always one Symbol for each symbol name. The resolver updates the SymbolBody
// stored in the Body field of this object as it resolves symbols. Symbol also
// holds computed properties of symbol names.
struct Symbol {
  // Symbol binding. This is on the Symbol to track changes during resolution.
  // In particular:
  // An undefined weak is still weak when it resolves to a shared library.
  // An undefined weak will not fetch archive members, but we have to remember
  // it is weak.
  uint8_t Binding;

  // Version definition index.
  uint16_t VersionId;

  // Symbol visibility. This is the computed minimum visibility of all
  // observed non-DSO symbols.
  unsigned Visibility : 2;

  // True if the symbol was used for linking and thus need to be added to the
  // output file's symbol table. This is true for all symbols except for
  // unreferenced DSO symbols and bitcode symbols that are unreferenced except
  // by other bitcode objects.
  unsigned IsUsedInRegularObj : 1;

  // If this flag is true and the symbol has protected or default visibility, it
  // will appear in .dynsym. This flag is set by interposable DSO symbols in
  // executables, by most symbols in DSOs and executables built with
  // --export-dynamic, and by dynamic lists.
  unsigned ExportDynamic : 1;

  // True if this symbol is specified by --trace-symbol option.
  unsigned Traced : 1;

  // This symbol version was found in a version script.
  unsigned InVersionScript : 1;

  bool includeInDynsym() const;
  uint8_t computeBinding() const;
  bool isWeak() const { return Binding == llvm::ELF::STB_WEAK; }

  // This field is used to store the Symbol's SymbolBody. This instantiation of
  // AlignedCharArrayUnion gives us a struct with a char array field that is
  // large and aligned enough to store any derived class of SymbolBody. We
  // assume that the size and alignment of ELF64LE symbols is sufficient for any
  // ELFT, and we verify this with the static_asserts in replaceBody.
  llvm::AlignedCharArrayUnion<
      DefinedCommon, DefinedRegular<llvm::object::ELF64LE>, DefinedSynthetic,
      Undefined<llvm::object::ELF64LE>, SharedSymbol<llvm::object::ELF64LE>,
      LazyArchive, LazyObject>
      Body;

  SymbolBody *body() { return reinterpret_cast<SymbolBody *>(Body.buffer); }
  const SymbolBody *body() const { return const_cast<Symbol *>(this)->body(); }
};

void printTraceSymbol(Symbol *Sym);

template <typename T, typename... ArgT>
void replaceBody(Symbol *S, ArgT &&... Arg) {
  static_assert(sizeof(T) <= sizeof(S->Body), "Body too small");
  static_assert(alignof(T) <= alignof(decltype(S->Body)),
                "Body not aligned enough");
  assert(static_cast<SymbolBody *>(static_cast<T *>(nullptr)) == nullptr &&
         "Not a SymbolBody");

  new (S->Body.buffer) T(std::forward<ArgT>(Arg)...);

  // Print out a log message if --trace-symbol was specified.
  // This is for debugging.
  if (S->Traced)
    printTraceSymbol(S);
}

inline Symbol *SymbolBody::symbol() {
  assert(!isLocal());
  return reinterpret_cast<Symbol *>(reinterpret_cast<char *>(this) -
                                    offsetof(Symbol, Body));
}
} // namespace elf

std::string toString(const elf::SymbolBody &B);
} // namespace lld

#endif
