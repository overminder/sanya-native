#ifndef CODEGEN2_HPP
#define CODEGEN2_HPP

#include <vector>

#include <asmjit/asmjit.h>
#include <asmjit/core/memorymanager.h>

#include "gc.hpp"
#include "object.hpp"
#include "util.hpp"

// Runtime representation of module, referenced by generated functions
class Module {
 public:
  Module();
  ~Module() { }

  intptr_t addName(const Handle &name, const Handle &val);
  intptr_t lookupName(const Handle &name);

  // Trim the array to a vector
  // (# <assoc> <vec>)
  Object *getRoot();

 private:
  Object *&assoc();
  Object *&array();
  // (# <assoc-list : name -> index>
  //    <growable-array : index -> val>)
  Handle root;
};

class CGFunction;

class CGModule {
 public:
  CGModule();
  ~CGModule();
  Object *genModule(const Handle &);

 protected:
  // Returns -1 when not found
  intptr_t lookupGlobal(const Handle &name);

 private:
  Module module;
  Handle moduleRoot, moduleGlobalVector;

  Handle symDefine,
         symLambda,
         symQuote,
         symBegin,
         symIf,
         symPrimAdd,
         symPrimSub,
         symPrimLt,

         symPrimCons,
         symPrimCar,
         symPrimCdr,
         symPrimPairp,
         symPrimNullp,

         symPrimTrace,
         symPrimError,
         symMain;

  std::vector<CGFunction *> cgfuncs;

  friend class CGFunction;
};

class CGFunction {
 protected:
  CGFunction(const Handle &name, const Handle &lamBody, CGModule *parent);

  const Handle &makeClosure();

  // Put placeholders there
  void emitFuncHeader();

  // Invariant: cannot GC during single function compilation, since some of
  // the generated pointers are inside the AsmJit's assembler buffer.
  // XXX: Fix it by utilizing reloc is fine, but it's kind of...
  void compileFunction();
  void compileBody(const Handle &exprs, intptr_t start, bool isTail);
  void compileExpr(const Handle &expr, bool isTail = false);
  void compileCall(const Handle &xs, bool isTail);

  bool tryIf(const Handle &expr, bool isTail);
  bool tryQuote(const Handle &expr);
  bool tryBegin(const Handle &expr, bool isTail);
  bool tryPrimOp(const Handle &expr, bool isTail);

  void syncThreadState();

  intptr_t getThisClosure();
  intptr_t getFrameDescr();

  void recordReloc(const Handle &e);
  void recordLastPtrOffset();
  intptr_t makeFrameDescr();

  // Alloc related

  // Assume car and cdr are pushed
  void allocPair();

  // Also records virtual frame
  void pushObject(const Handle &);
  void pushInt(intptr_t);
  enum IsPtr { kIsNotPtr = 0, kIsPtr = 1 };
  void pushReg(const AsmJit::GpReg &r, IsPtr isPtr);
  void pushVirtual(IsPtr isPtr);
  void popSome(intptr_t n = 1);
  void popVirtual(intptr_t);
  void popFrame();
  // But don't pop virtual. Used by tailcall.
  void popPhysicalFrame();
  void popReg(const AsmJit::GpReg &r);

  intptr_t lookupLocal(const Handle &name) {
    bool ok;
    Handle result = Util::assocLookup(locals, name, Util::kPtrEq, &ok);
    if (ok) {
      return result->fromFixnum();
    }
    else {
      return -1;
    }
  }

  void addNewLocal(const Handle &name) {
    locals = Util::assocInsert(
        locals, name, Object::newFixnum(0), Util::kPtrEq);
  }

  void shiftLocal(intptr_t n);

 private:
  AsmJit::X86Assembler xasm;

  // Offset between current rsp and the stack slot for return address,
  // in ptrSize (8).
  intptr_t frameSize;

  Handle name, lamBody;
  CGModule *parent;

  RawObject *rawFunc;
  Handle closure;
  // Maps symbol to index
  Handle locals;

  // (# #t #f #t ...) vector of stack items. 
  // True if is pointer
  Handle stackItemList;

  // Growable array of #<Fixnum const-offset>
  Handle ptrOffsets;

  // Growable array of objects. Will be used to patch the generated
  // code.
  Handle relocArray;

  friend class CGModule;
};

#endif
