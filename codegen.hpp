#ifndef CODEGEN_HPP
#define CODEGEN_HPP

#include <string>
#include <vector>
#include <unordered_map>

#include <asmjit/asmjit.h>
#include <asmjit/core/memorymanager.h>

class Object;
class RawObject;
class CGFunction;

class CGModule {
 public:
  CGModule() { }

  Object *genModule(Object *);

  Object *lookupGlobal(const std::string &, bool *, bool wantBox = true);

 private:
  std::unordered_map<std::string, CGFunction *> cgfuncs;

  friend class CGFunction;
};

class CGFunction {
 public:
  CGFunction(Object *name, Object *args, Object *body, CGModule *);

  // Make an empty closure, and a box to hold the closure
  void makeClosure();

  // Do the actual compilation, reloc, etc
  void compileFunction();

  void compileBody(Object *exprs, bool isTail);
  void compileExpr(Object *expr, bool isTail = false);

  void compileCall(const std::vector<Object *> &xs, bool isTail);

  // Put placeholders there
  void emitFuncHeader();

 private:
  bool tryIf(const std::vector<Object *> &xs, bool isTail);
  bool tryQuote(const std::vector<Object *> &xs);
  bool tryPrimOp(const std::vector<Object *> &xs);

  void emitConst(Object *, bool needsGc = false);

  intptr_t lookupLocal(const std::string &name) {
    auto iter = locals.find(name);
    if (iter != locals.end()) {
      return iter->second;
    }
    return -1;
  }

  void addNewLocal(const std::string &name) {
    locals[name] = 0;
  }

  void shiftLocal(intptr_t n) {
    for (auto iter = locals.begin(); iter != locals.end(); ++iter) {
      //dprintf(2, "[shiftLocal] %s: %ld => %ld\n", iter->first.c_str(),
      //        iter->second, iter->second + n);
      iter->second += n;
    }
  }

  AsmJit::X86Assembler xasm;
  Object *name, *args, *body;
  CGModule *parent;

  RawObject *rawFunc;
  Object *closure;
  Object *closureBox;
  std::unordered_map<std::string, intptr_t> locals;
  std::vector<Object *> ptrOffsets;
  std::unordered_map<std::string, Object *> relocMap;

  friend class CGModule;
};

#endif
