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
         symIf,
         symPrimAdd,
         symPrimSub,
         symPrimLt,
         symPrimTrace,
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

  void compileFunction();
  void compileBody(const Handle &exprs, intptr_t start, bool isTail);
  void compileExpr(const Handle &expr, bool isTail = false);
  void compileCall(const Handle &xs, bool isTail);

  bool tryIf(const Handle &expr, bool isTail);
  bool tryQuote(const Handle &expr);
  bool tryPrimOp(const Handle &expr, bool isTail);

  void emitConst(const Handle &expr);

  void recordLastPtrOffset();

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
    locals = Util::assocInsert(locals, name, Object::newFixnum(0), Util::kPtrEq);
  }

  void shiftLocal(intptr_t n);

 private:
  AsmJit::X86Assembler xasm;

  Handle name, lamBody;
  CGModule *parent;

  RawObject *rawFunc;
  Handle closure;
  // Maps symbol to index
  Handle locals;
  // Growable array of (size . offset)
  Handle ptrOffsets;

  friend class CGModule;
};

#endif
