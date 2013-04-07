#include <assert.h>

#include "codegen.hpp"
#include "object.hpp"
#include "runtime.hpp"

using namespace AsmJit;

static const int kPtrSize = sizeof(void *);
static auto frameDescrReg = r10;
static const GpReg argRegs[6] = { rdi, rsi, rdx, rcx, r8, r9 };

template <typename F>
static void forEachListItem(Object *xs, F f) {
  intptr_t i = 0;
  while (xs->isPair()) {
    if (f(xs->raw()->car(), i++, xs->raw()->cdr())) {
      xs = xs->raw()->cdr();
    }
    else {
      break;
    }
  }
}

static Object *listToVector(Object *xs, std::vector<Object *> *out) {
  Object *restOut;
  forEachListItem(xs, [&](Object *x, intptr_t _, Object *rest) -> bool {
    out->push_back(x);
    restOut = rest;
    return true;
  });
  return restOut;
}

static void unpackListItems(Object *xs, intptr_t max,
                            Object **out, intptr_t *len,
                            Object **restOut = NULL) {
  forEachListItem(xs, [&](Object *x, intptr_t i, Object *rest) -> bool {
    if (restOut) {
      *restOut = rest;
    }
    if (i < max) {
      out[i] = x;
      if (len) *len = i + 1;
      return true;
    }
    else {
      return false;
    }
  });
}

Object *CGModule::genModule(Object *top) {
  assert(top->isPair());
  forEachListItem(top, [&](Object *x, intptr_t _u, Object *_u2) -> bool {
    // x is a def block
    assert(x->isPair());
    Object *defItems[3];
    intptr_t len;
    unpackListItems(x, 3, defItems, &len);
    assert(len == 3);

    assert(defItems[0]->isSymbol() &&
           strcmp(defItems[0]->rawSymbol(), "define") == 0);

    Object *name = defItems[1];
    assert(name->isSymbol());

    x = defItems[2];
    assert(x->isPair());
    Object *lamItems[2];
    unpackListItems(x, 2, lamItems, &len);
    assert(len == 2);
    assert(lamItems[0]->isSymbol() &&
           strcmp(lamItems[0]->rawSymbol(), "lambda") == 0);
    Object *args = lamItems[1];
    assert(args->isPair() || args->isNil());
    Object *body = x->raw()->cdr()->raw()->cdr();

    CGFunction *cgfunc = new CGFunction(name, args, body, this);
    cgfuncs[name->rawSymbol()] = cgfunc;
    cgfunc->makeClosure();

    return true;
  });

  for (auto iter : cgfuncs) {
    iter.second->compileFunction();
  }

  bool gotMain;
  auto mainClo = lookupGlobal("main", &gotMain, false);
  assert (gotMain);
  return mainClo;
}

Object *CGModule::lookupGlobal(const std::string &name,
                               bool *ok, bool wantBox) {
  auto iter = cgfuncs.find(name);
  if (iter == cgfuncs.end()) {
    *ok = false;
    return NULL;
  }
  else {
    *ok = true;
    if (wantBox) {
      return iter->second->closureBox;
    }
    else {
      return iter->second->closure;
    }
  }
}

CGFunction::CGFunction(Object *name, Object *args, Object *body,
                       CGModule *parent)
  : name(name)
  , args(args)
  , body(body)
  , parent(parent)
  , closure(NULL)
  , rawFunc(NULL)
{ }

void CGFunction::makeClosure() {
  assert(!closure);
  closure = Object::newClosure(NULL);
  // dprintf(2, "[CodeGen::makeClosure] %s => %p\n", name->rawSymbol(), closure);
  closureBox = Object::newPair(closure, Object::newNil());
}

#define __ xasm.

void CGFunction::emitFuncHeader() {
  // Keep in sync with object.hpp's function definition.
  __ emitQWord(0);
  __ emitQWord(0);
  __ emitQWord(0);
  __ emitQWord(0);
}

// S2S (Scheme to scheme) calling convention:
//
// Stack after call instr:
//   [...]
//   retAddr <- %rsp
//   r10 = frameDescr
//   rdi = thisClosure
//   rsi = arg0
//   rdx = arg1
//   rcx = arg2
//
// So the prologue should be:
//   push r10
//   push rdi
//   push rsi
//   push rdx
//   push rcx
//
// (define a val) would be:
//   [code for val]
//
// (func a1 a2 a3) would be:
//   [code for func]
//   pop %rdi
//   [code for a1]
//   pop %rsi
//   [code for a2]
//   pop %rdx
//   [code for a3]
//   pop %rcx
//   mov frameDescr, %r10
//   [test and extract codeptr to %rax]
//   call %rax
//   push %rax
//
// return x would be (frameSize = args + locals + thisClosure + frameDescr)
//   [code for x]
//   pop %rax
//   add $8 * frameSize, %rsp
//   ret
//
// tailcall (func a1 a2 a3) would be:
//   [code for func]
//   pop %rdi
//   [code for a1]
//   pop %rsi
//   [code for a2]
//   pop %rdx
//   [code for a3]
//   pop %rcx
//   add $8, frameSize
//   [test and extract codeptr to %rax]
//   jmp %rax
//
void CGFunction::compileFunction() {

  emitFuncHeader();

  // push frameDescr
  __ push(frameDescrReg);

  // push thisClosure
  __ push(rdi);

  intptr_t myArity = 0;

  // Move args to stack
  forEachListItem(args, [&](Object *arg, intptr_t nth, Object *_u2) -> bool {
    assert(nth < 5);
    assert(arg->isSymbol());
    assert(lookupLocal(arg->rawSymbol()) == -1);

    myArity = nth + 1;

    __ push(argRegs[nth + 1]);
    shiftLocal(1);
    // dprintf(2, "[MoveArgs] %s [%s] is at %ld\n",
    //         name->rawSymbol(), arg->rawSymbol(),
    //         lookupLocal(arg->rawSymbol()));
    addNewLocal(arg->rawSymbol());
    return true;
  });

  //dprintf(2, "compileFunction: %s, arity=%ld\n", name->rawSymbol(), myArity);

  compileBody(body, true);

  // Return last value on the stack
  __ pop(rax);
  shiftLocal(-1);
  __ add(rsp, kPtrSize * (2 + locals.size()));
  __ ret();

  // Create function and patch closure.
  void *rawPtr = __ make();

  rawFunc = Object::newFunction(rawPtr, myArity, name,
      /* reloc */ Object::newNil(), 0);

  dprintf(2, "[CodeGen::compileFunction] %s @%p\n", name->rawSymbol(),
          rawFunc->funcCodeAs<void *>());

  closure->raw()->cloInfo() = rawFunc;
}

void CGFunction::compileBody(Object *exprs, bool isTail) {
  // dprintf(1, "compile ");
  // exprs->displayDetail(1);
  // dprintf(1, "\n");

  forEachListItem(exprs, [&](Object *x, intptr_t _u, Object *rest) -> bool {
    if (rest->isNil()) {
      compileExpr(x, isTail);
    }
    else {
      compileExpr(x, false);
      __ pop(rax);
      shiftLocal(-1);
    }
    return true;
  });
}

void CGFunction::compileExpr(Object *expr, bool isTail) {
  intptr_t ix;
  bool ok;

  switch (expr->getTag()) {
  case RawObject::kFixnumTag:
    __ push(expr->as<intptr_t>());
    shiftLocal(1);
    break;

  case RawObject::kSymbolTag:
    // Lookup first
    ix = lookupLocal(expr->rawSymbol());
    if (ix != -1) {
      // Is local
      __ mov(rax, qword_ptr(rsp, ix * kPtrSize));
      __ push(rax);
      shiftLocal(1);
      break;
    }
    else {
      Object *box = parent->lookupGlobal(expr->rawSymbol(), &ok);
      if (!ok) {
        dprintf(2, "lookupGlobal: %s not found\n", expr->rawSymbol());
        exit(1);
      }
      __ mov(rax, box->as<intptr_t>());
      __ mov(rax, qword_ptr(rax, -RawObject::kPairTag));
      // XXX: add to ptr offset vector
      __ push(rax);
      shiftLocal(1);
      break;
    }

  case RawObject::kPairTag:
  {
    std::vector<Object *>xs;
    Object *rest = listToVector(expr, &xs);
    assert(rest->isNil());

    // Check for define
    if (tryIf(xs, isTail)) {
      break;
    }
    else if (tryQuote(xs)) {
      break;
    }
    else if (tryPrimOp(xs)) {
      break;
    }
    else {
      // Should be funcall
      compileCall(xs, isTail);
    }
    break;
  }

  case RawObject::kSingletonTag:
    if (expr->isTrue() || expr->isFalse()) {
      emitConst(expr);
    }
    else if (expr->isNil()) {
      assert(0 && "Unexpected nil in code");
    }
    else {
      assert(0 && "Unexpected singleton in code");
    }
    break;

  default:
    assert(false);
  }
}

void CGFunction::compileCall(const std::vector<Object *> &xs, bool isTail) {
  // Evaluate func and args
  intptr_t argc = xs.size() - 1;
  assert(argc < 6);

  for (auto arg : xs) {
    compileExpr(arg, false);
  }

  intptr_t nth = 0;
  for (auto arg : xs) {
    // Reverse pop
    __ pop(argRegs[argc - nth]);
    shiftLocal(-1);
    ++nth;
  }

  // Check closure type
  __ mov(rax, rdi);
  __ and_(eax, 15);
  __ cmp(rax, RawObject::kClosureTag);
  Label labelNotAClosure = __ newLabel();
  __ jne(labelNotAClosure);

  // Check arg count
  __ mov(rax, qword_ptr(rdi, -RawObject::kClosureTag));
  __ mov(rax, qword_ptr(rax, RawObject::kFuncArityOffset));
  __ cmp(rax, argc);
  Label labelArgCountMismatch = __ newLabel();
  __ jne(labelArgCountMismatch);

  // Extract and call the code pointer
  // XXX: where is the frameDescr?
  // XXX: how about stack overflow checking?
  __ mov(frameDescrReg, 0);
  __ mov(rax, qword_ptr(rdi, -RawObject::kClosureTag));
  __ lea(rax, qword_ptr(rax, RawObject::kFuncCodeOffset));

  Label labelOk = __ newLabel();
  if (!isTail) {
    // If doing normal call:
    __ call(rax);

    // After call
    __ push(rax);
    shiftLocal(1);
    __ jmp(labelOk);
  }
  else {
    // Pop frame
    __ add(rsp, kPtrSize * (2 + locals.size()));

    // Tail call
    __ jmp(rax);
  }

  // Not a closure(%rdi = func)
  __ bind(labelNotAClosure);
  __ call(reinterpret_cast<void *>(&Runtime::handleNotAClosure));

  // Wrong arg count(%rdi = func, %rsi = actual argc)
  __ bind(labelArgCountMismatch);
  __ mov(rsi, argc);
  __ call(reinterpret_cast<void *>(&Runtime::handleArgCountMismatch));

  if (!isTail) {
    __ bind(labelOk);
  }
}

bool CGFunction::tryQuote(const std::vector<Object *> &xs) {
  if (xs.size() != 2 || !xs[0]->isSymbol() ||
      strcmp(xs[0]->rawSymbol(), "quote") != 0) {
    return false;
  }
  emitConst(xs[1]);
  return true;
}

bool CGFunction::tryIf(const std::vector<Object *> &xs, bool isTail) {
  if (xs.size() != 4 || !xs[0]->isSymbol() ||
      strcmp(xs[0]->rawSymbol(), "if") != 0) {
    return false;
  }

  Label labelFalse = __ newLabel(),
        labelDone  = __ newLabel();

  // Pred
  compileExpr(xs[1]);
  __ pop(rax);
  shiftLocal(-1);

  __ cmp(rax, Object::newFalse()->as<intptr_t>());
  __ je(labelFalse);

  compileExpr(xs[2], isTail);
  __ jmp(labelDone);

  // Since we need to balance out those two branches
  shiftLocal(-1);
  __ bind(labelFalse);
  compileExpr(xs[3], isTail);

  __ bind(labelDone);

  return true;
}

// Primitive operator, without checks
bool CGFunction::tryPrimOp(const std::vector<Object *> &xs) {
  if (xs.size() < 1 || !xs[0]->isSymbol()) {
    return false;
  }

  std::string opName = xs[0]->rawSymbol();

  //dprintf(2, "opName = %s, size = %ld\n", opName.c_str(), xs.size());

  if (opName == "+#" && xs.size() == 3) {
    compileExpr(xs[1]);
    compileExpr(xs[2]);
    __ pop(rax);
    shiftLocal(-1);
    __ add(rax, qword_ptr(rsp));
    __ sub(rax, RawObject::kFixnumTag);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == "-#" && xs.size() == 3) {
    compileExpr(xs[1]);
    compileExpr(xs[2]);
    __ mov(rax, qword_ptr(rsp, kPtrSize));
    __ sub(rax, qword_ptr(rsp));
    __ add(rax, RawObject::kFixnumTag);
    __ add(rsp, kPtrSize);
    shiftLocal(-1);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == "<#" && xs.size() == 3) {
    compileExpr(xs[1]);
    compileExpr(xs[2]);
    __ pop(rax);
    shiftLocal(-1);
    __ cmp(rax, qword_ptr(rsp));
    __ mov(rcx, Object::newTrue()->as<intptr_t>());
    __ mov(rax, Object::newFalse()->as<intptr_t>());
    __ cmovg(rax, rcx);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == "trace#" && xs.size() == 3) {
    compileExpr(xs[1]);
    __ pop(rdi);
    shiftLocal(-1);
    __ call(reinterpret_cast<intptr_t>(&Runtime::traceObject));
    compileExpr(xs[2]);
  }
  else {
    return false;
  }

  return true;
}

void CGFunction::emitConst(Object *x, bool needsGc) {
  __ mov(rax, x->as<intptr_t>());
  __ push(rax);
  shiftLocal(1);
}

#undef __
