#include <assert.h>

#include "codegen.hpp"
#include "object.hpp"

using namespace AsmJit;

static const int kPtrSize = sizeof(void *);

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

static void unpackListItems(Object *xs, intptr_t max,
                            Object **out, intptr_t *len) {
  forEachListItem(xs, [&](Object *x, intptr_t i, Object *_) -> bool {
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
           strcmp(lamItems[0]->rawSymbol(), "lambda"));
    Object *args = lamItems[1];
    assert(args->isPair());
    Object *body = x->raw()->cdr()->raw()->cdr();

    CGFunction *cgfunc = new CGFunction(name, args, body, this);
    cgfuncs[name->rawSymbol()] = cgfunc;
    cgfunc->makeClosure();

    return true;
  });

  for (auto iter : cgfuncs) {
    iter.second->compileFunction();
  }

  auto iter = cgfuncs.find("main");
  assert (iter != cgfuncs.end());
  return iter->second->closure;
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

Object *CGFunction::makeClosure() {
  assert(!closure);
  return (closure = Object::newClosure(NULL));
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
// After call instr:
//
// [...]
// retAddr <- %rsp
// r10 = frameDescr
// rdi = thisClosure
// rsi = arg0
// rdx = arg1
// rcx = arg2
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
  __ push(r10);

  // push thisClosure
  __ push(rdi);

  intptr_t myArity = 0;
  static const GpReg argRegs[5] = { rsi, rdx, rcx, r8, r9 };

  // Move args to stack
  forEachListItem(args, [&](Object *arg, intptr_t nth, Object *_u2) -> bool {
    assert(nth < 5);
    assert(arg->isSymbol());
    assert(lookupLocal(arg->rawSymbol()) == -1);

    myArity = nth + 1;

    addNewLocal(arg->rawSymbol());
    shiftLocal(1);
    __ push(argRegs[nth]);
    return true;
  });

  compileExpr(body, true);

  // Return last value on the stack
  __ pop(rax);
  __ add(rsp, kPtrSize * (2 + locals.size()));
  __ ret();

  // Create function and patch closure.
  void *rawPtr = __ make();
  rawFunc = Object::newFunction(rawPtr, myArity, name,
      /* reloc */ Object::newNil(), 0);
  closure->raw()->cloInfo() = rawFunc;
}

void CGFunction::compileExpr(Object *expr, bool isTail) {
  switch (expr->getTag()) {
  case RawObject::kFixnumTag:
    __ push(expr->as<intptr_t>());
    break;
  default:
    assert(false);
  }
}

#undef __
