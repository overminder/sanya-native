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

    shiftLocal(1);
    addNewLocal(arg->rawSymbol());
    // dprintf(2, "[MoveArgs] %s [%s] is at %ld\n",
    //         name->rawSymbol(), arg->rawSymbol(),
    //         lookupLocal(arg->rawSymbol()));
    __ push(argRegs[nth + 1]);
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

  dprintf(2, "[Function compiled] %s @%p\n", name->rawSymbol(),
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
      assert(ok);
      __ mov(rax, box->as<intptr_t>());
      __ mov(rax, qword_ptr(rax, -RawObject::kPairTag));
      // XXX: add to ptr offset vector
      __ push(rax);
      shiftLocal(1);
      break;
    }

#define isDefine(_) false
#define compileDefine(_)
#define isSet(_) false
#define compileSet(_)
#define isIf(_) false
#define compileIf(_)
#define isBegin(_) false
#define compileBegin(_)

  case RawObject::kPairTag:
    // Check for define
    if (isDefine(expr)) {
      compileDefine(expr);
    }
    else if (isSet(expr)) {
      compileSet(expr);
    }
    else if (isIf(expr)) {
      compileIf(expr);
    }
    else if (isBegin(expr)) {
      compileBegin(expr);
    }
    else {
      // Should be funcall
      compileCall(expr, isTail);
    }
    break;

  default:
    assert(false);
  }
}

void CGFunction::compileCall(Object *expr, bool isTail) {
  // Evaluate func and args
  intptr_t argc;
  forEachListItem(expr, [&](Object *x, intptr_t nth, Object *_u2) -> bool {
    assert(nth < 6);
    compileExpr(x, false);
    __ pop(argRegs[nth]);
    shiftLocal(-1);
    argc = nth;
    return true;
  });

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


#undef __
