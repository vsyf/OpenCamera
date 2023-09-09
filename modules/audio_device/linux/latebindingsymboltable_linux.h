/*
 * latebindingsymboltable_linux.h
 * Copyright (C) 2023 youfa <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef LATEBINDINGSYMBOLTABLE_LINUX_H
#define LATEBINDINGSYMBOLTABLE_LINUX_H

#include <stddef.h>  // for NULL
#include <string.h>

#include "base/checks.h"
#include "base/constructor_magic.h"

// This file provides macros for creating "symbol table" classes to simplify the
// dynamic loading of symbols from DLLs. Currently the implementation only
// supports Linux and pure C symbols.
// See talk/sound/pulseaudiosymboltable.(h|cc) for an example.

namespace avp {
namespace adm_linux {

// #ifdef AVP_LINUX
typedef void* DllHandle;

const DllHandle kInvalidDllHandle = NULL;
// #else
// #error Not implemented
// #endif

// These are helpers for use only by the class below.
DllHandle InternalLoadDll(const char dll_name[]);

void InternalUnloadDll(DllHandle handle);

bool InternalLoadSymbols(DllHandle handle,
                         int num_symbols,
                         const char* const symbol_names[],
                         void* symbols[]);

template <int SYMBOL_TABLE_SIZE,
          const char kDllName[],
          const char* const kSymbolNames[]>
class LateBindingSymbolTable {
 public:
  LateBindingSymbolTable()
      : handle_(kInvalidDllHandle), undefined_symbols_(false) {
    memset(symbols_, 0, sizeof(symbols_));
  }

  ~LateBindingSymbolTable() { Unload(); }

  static int NumSymbols() { return SYMBOL_TABLE_SIZE; }

  // We do not use this, but we offer it for theoretical convenience.
  static const char* GetSymbolName(int index) {
    DCHECK_LT(index, NumSymbols());
    return kSymbolNames[index];
  }

  bool IsLoaded() const { return handle_ != kInvalidDllHandle; }

  // Loads the DLL and the symbol table. Returns true iff the DLL and symbol
  // table loaded successfully.
  bool Load() {
    if (IsLoaded()) {
      return true;
    }
    if (undefined_symbols_) {
      // We do not attempt to load again because repeated attempts are not
      // likely to succeed and DLL loading is costly.
      return false;
    }
    handle_ = InternalLoadDll(kDllName);
    if (!IsLoaded()) {
      return false;
    }
    if (!InternalLoadSymbols(handle_, NumSymbols(), kSymbolNames, symbols_)) {
      undefined_symbols_ = true;
      Unload();
      return false;
    }
    return true;
  }

  void Unload() {
    if (!IsLoaded()) {
      return;
    }
    InternalUnloadDll(handle_);
    handle_ = kInvalidDllHandle;
    memset(symbols_, 0, sizeof(symbols_));
  }

  // Retrieves the given symbol. NOTE: Recommended to use LATESYM_GET below
  // instead of this.
  void* GetSymbol(int index) const {
    DCHECK(IsLoaded());
    DCHECK_LT(index, NumSymbols());
    return symbols_[index];
  }

 private:
  DllHandle handle_;
  bool undefined_symbols_;
  void* symbols_[SYMBOL_TABLE_SIZE];

  AVP_DISALLOW_COPY_AND_ASSIGN(LateBindingSymbolTable);
};

// This macro must be invoked in a header to declare a symbol table class.
#define LATE_BINDING_SYMBOL_TABLE_DECLARE_BEGIN(ClassName) enum {
// This macro must be invoked in the header declaration once for each symbol
// (recommended to use an X-Macro to avoid duplication).
// This macro defines an enum with names built from the symbols, which
// essentially creates a hash table in the compiler from symbol names to their
// indices in the symbol table class.
#define LATE_BINDING_SYMBOL_TABLE_DECLARE_ENTRY(ClassName, sym) \
  ClassName##_SYMBOL_TABLE_INDEX_##sym,

// This macro completes the header declaration.
#define LATE_BINDING_SYMBOL_TABLE_DECLARE_END(ClassName)       \
  ClassName##_SYMBOL_TABLE_SIZE                                \
  }                                                            \
  ;                                                            \
                                                               \
  extern const char ClassName##_kDllName[];                    \
  extern const char* const                                     \
      ClassName##_kSymbolNames[ClassName##_SYMBOL_TABLE_SIZE]; \
                                                               \
  typedef ::avp::adm_linux::LateBindingSymbolTable<            \
      ClassName##_SYMBOL_TABLE_SIZE, ClassName##_kDllName,     \
      ClassName##_kSymbolNames>                                \
      ClassName;

// This macro must be invoked in a .cc file to define a previously-declared
// symbol table class.
#define LATE_BINDING_SYMBOL_TABLE_DEFINE_BEGIN(ClassName, dllName) \
  const char ClassName##_kDllName[] = dllName;                     \
  const char* const ClassName##_kSymbolNames[ClassName##_SYMBOL_TABLE_SIZE] = {
// This macro must be invoked in the .cc definition once for each symbol
// (recommended to use an X-Macro to avoid duplication).
// This would have to use the mangled name if we were to ever support C++
// symbols.
#define LATE_BINDING_SYMBOL_TABLE_DEFINE_ENTRY(ClassName, sym) #sym,

#define LATE_BINDING_SYMBOL_TABLE_DEFINE_END(ClassName) \
  }                                                     \
  ;

// Index of a given symbol in the given symbol table class.
#define LATESYM_INDEXOF(ClassName, sym) (ClassName##_SYMBOL_TABLE_INDEX_##sym)

// Returns a reference to the given late-binded symbol, with the correct type.
#define LATESYM_GET(ClassName, inst, sym) \
  (*reinterpret_cast<__typeof__(&sym)>(   \
      (inst)->GetSymbol(LATESYM_INDEXOF(ClassName, sym))))

}  // namespace adm_linux
}  // namespace avp

#endif /* !LATEBINDINGSYMBOLTABLE_LINUX_H */
