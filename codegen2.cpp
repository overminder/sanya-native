#include <assert.h>

#include "codegen2.hpp"
#include "runtime.hpp"

using namespace AsmJit;

static const int kPtrSize = sizeof(void *);
static const auto kClosureReg = rdi;
static const GpReg kArgRegs[5] = { rsi, rdx, rcx, r8, r9 };
static const auto kFrameDescrReg = r10;

Module::Module() {
  root = Object::newVector(2, Object::newNil());
  Handle hAssoc = Util::newAssocList();
  assoc() = hAssoc;

  Handle hArr = Util::newGrowableArray();
  array() = hArr;
}

Object *&Module::assoc() {
  return root->raw()->vectorAt(0);
}

Object *&Module::array() {
  return root->raw()->vectorAt(1);
}

intptr_t Module::addName(const Handle &name, const Handle &val) {
  Handle hAssoc = assoc();
  bool ok;
  Handle maybeIndex = Util::assocLookup(hAssoc, name, Util::kPtrEq, &ok);
  if (ok) {
    intptr_t ix = maybeIndex->fromFixnum();
    Util::arrayAt(array(), ix) = val;
    return ix;
  }
  else {
    intptr_t ix = Util::arrayLength(array());
    Util::arrayAppend(array(), val);
    Handle newAssoc = Util::assocInsert(
        hAssoc, name, Object::newFixnum(ix), Util::kPtrEq);
    assoc() = newAssoc;
    return ix;
  }
}

intptr_t Module::lookupName(const Handle &name) {
  bool ok;
  Handle maybeIndex = Util::assocLookup(assoc(), name, Util::kPtrEq, &ok);
  if (ok) {
    return maybeIndex->fromFixnum();
  }
  else {
    return -1;
  }
}

Object *Module::getRoot() {
  Handle trimmedVec = Util::arrayToVector(array());
  Handle tmpRoot = Object::newVector(2, Object::newNil());
  tmpRoot->raw()->vectorAt(0) = assoc();
  tmpRoot->raw()->vectorAt(1) = trimmedVec;
  return tmpRoot;
}

template <typename F>
static void forEachListItem(const Handle &kxs, F f) {
  intptr_t i = 0;
  Handle xs = kxs;

  while (xs->isPair()) {
    if (f(xs->raw()->car(), i++, xs->raw()->cdr())) {
      xs = xs->raw()->cdr();
    }
    else {
      break;
    }
  }
}

static Object *listToArray(const Handle &xs, Handle *out) {
  Handle restOut;
  forEachListItem(xs,
      [&](const Handle &x, intptr_t _, const Handle &rest) -> bool {

    Util::arrayAppend(*out, x);
    restOut = rest;
    return true;
  });
  return restOut;
}

CGModule::CGModule() {
  symDefine    = Object::internSymbol("define");
  symLambda    = Object::internSymbol("lambda");
  symQuote     = Object::internSymbol("quote");
  symIf        = Object::internSymbol("if");
  symPrimAdd   = Object::internSymbol("+#");
  symPrimSub   = Object::internSymbol("-#");
  symPrimLt    = Object::internSymbol("<#");
  symPrimTrace = Object::internSymbol("trace#");
  symMain      = Object::internSymbol("main");
}

CGModule::~CGModule() {
  for (auto f : cgfuncs) {
    delete f;
  }
}

Object *CGModule::genModule(const Handle &top) {
  Handle mainClo;

  assert(top->isPair());
  forEachListItem(
      top, [&](const Handle &defn, intptr_t _u, Object *_u2) -> bool {

    Handle items = Util::newGrowableArray();
    Handle rest = listToArray(defn, &items);
    assert(Util::arrayLength(items) == 3);
    assert(rest->isNil());

    assert(Util::arrayAt(items, 0) == symDefine);
    assert(Util::arrayAt(items, 1)->isSymbol());
    assert(Util::arrayAt(items, 2)->isPair());

    Handle name = Util::arrayAt(items, 1);
    Handle lamExpr = Util::newGrowableArray();
    rest = listToArray(Util::arrayAt(items, 2), &lamExpr);
    assert(Util::arrayLength(lamExpr) >= 3);
    assert(Util::arrayAt(lamExpr, 0) == symLambda);
    assert(Util::arrayAt(lamExpr, 1)->isList());

    CGFunction *cgf = new CGFunction(name, lamExpr, this);
    // Provides an indirection for other code to refer
    module.addName(name, cgf->makeClosure());
    if (name == symMain) {
      mainClo = cgf->closure;
    }
    cgfuncs.push_back(cgf);

    return true;
  });

  assert(mainClo.getPtr() && "main not defined");

  moduleRoot = module.getRoot();
  moduleGlobalVector = moduleRoot->raw()->vectorAt(1);

  for (auto cgf : cgfuncs) {
    // Do the actual compilation
    cgf->compileFunction();
  }

  return mainClo;
}

intptr_t CGModule::lookupGlobal(const Handle &name) {
  assert(moduleRoot);
  return module.lookupName(name);
}

// Short-hand
#define __ xasm.

CGFunction::CGFunction(const Handle &name, const Handle &lamBody,
                       CGModule *parent)
  : name(name)
  , lamBody(lamBody)
  , parent(parent)
  , locals(Util::newAssocList())
  , ptrOffsets(Util::newGrowableArray())
{ }

const Handle &CGFunction::makeClosure() {
  assert(!closure.getPtr());
  closure = Object::newClosure(NULL);
  //dprintf(2, "[CodeGen::makeClosure] %s => %p\n", name->rawSymbol(), closure);
  return closure;
}

void CGFunction::emitFuncHeader() {
  // Keep in sync with object.hpp's function definition.
  __ emitQWord(0);
  __ emitQWord(0);
  __ emitQWord(0);
  __ emitQWord(0);
}

void CGFunction::compileFunction() {
  Util::logObj("CompileFunction Start", name);

  emitFuncHeader();

  // push frameDescr
  __ push(kFrameDescrReg);

  // push thisClosure
  __ push(kClosureReg);

  Handle argArray = Util::newGrowableArray();
  Handle restArgs = listToArray(Util::arrayAt(lamBody, 1), &argArray);
  intptr_t arity = Util::arrayLength(argArray);
  // To be able to pass by reg
  assert(arity <= 5);

  // Move args to stack
  for (intptr_t i = 0; i < arity; ++i) {
    Handle arg = Util::arrayAt(argArray, i);
    assert(arg->isSymbol());
    assert(lookupLocal(arg) == -1);

    // +1 for closure reg
    __ push(kArgRegs[i]);
    shiftLocal(1);
    addNewLocal(arg);
  }

  compileBody(lamBody, 2, true);

  // Return last value on the stack
  __ pop(rax);
  shiftLocal(-1);
  __ add(rsp, kPtrSize * (2 + Util::assocLength(locals)));
  __ ret();

  // Create function and patch closure.
  void *rawPtr = __ make();

  Handle trimmedConstOffsets = Util::arrayToVector(ptrOffsets);
  rawFunc = Object::newFunction(rawPtr, arity, name,
      /* const ptr offset array */ trimmedConstOffsets,
      /* num payload */ 0);
  closure->raw()->cloInfo() = rawFunc;

  Util::logObj("CompileFunction Done", closure);
}

void CGFunction::compileBody(const Handle &body, intptr_t start, bool isTail) {
  intptr_t len = Util::arrayLength(body);
  for (intptr_t i = start; i < len; ++i) {
    Handle x = Util::arrayAt(body, i);
    if (i == len - 1) {
      compileExpr(x, isTail);
    }
    else {
      compileExpr(x, false);
      __ pop(rax);
      shiftLocal(-1);
    }
  }
}

void CGFunction::compileExpr(const Handle &expr, bool isTail) {
  intptr_t ix;

  switch (expr->getTag()) {
  case RawObject::kFixnumTag:
    emitConst(expr);
    break;

  case RawObject::kSymbolTag:
    // Lookup first
    ix = lookupLocal(expr);
    if (ix != -1) {
      // Is local
      __ mov(rax, qword_ptr(rsp, ix * kPtrSize));
      __ push(rax);
      shiftLocal(1);
      break;
    }
    else {
      ix = parent->lookupGlobal(expr);
      if (ix == -1) {
        dprintf(2, "lookupGlobal: %s not found\n", expr->rawSymbol());
        exit(1);
      }
      __ mov(rax, parent->moduleGlobalVector->as<intptr_t>());
      // References a ptr so need to do this
      recordLastPtrOffset();
      __ mov(rax, qword_ptr(rax,
            RawObject::kVectorElemOffset - RawObject::kVectorTag +
            kPtrSize * ix));
      __ push(rax);
      shiftLocal(1);
      break;
    }

  case RawObject::kPairTag:
  {
    Handle xs = Util::newGrowableArray();
    assert(listToArray(expr, &xs)->isNil());

    // Check for define
    if (tryIf(xs, isTail)) {
      break;
    }
    else if (tryQuote(xs)) {
      break;
    }
    else if (tryPrimOp(xs, isTail)) {
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

void CGFunction::compileCall(const Handle &xs, bool isTail) {
  assert(false);
}

bool CGFunction::tryIf(const Handle &xs, bool isTail) {
  intptr_t len = Util::arrayLength(xs);
  if (len != 4 || !Util::arrayAt(xs, 0) == parent->symIf) {
    return false;
  }

  Label labelFalse = __ newLabel(),
        labelDone  = __ newLabel();

  // Pred
  compileExpr(Util::arrayAt(xs, 1));
  __ pop(rax);
  shiftLocal(-1);

  __ cmp(rax, Object::newFalse()->as<intptr_t>());
  __ je(labelFalse);

  compileExpr(Util::arrayAt(xs, 2), isTail);
  __ jmp(labelDone);

  // Since we need to balance out those two branches
  shiftLocal(-1);
  __ bind(labelFalse);
  compileExpr(Util::arrayAt(xs, 3), isTail);

  __ bind(labelDone);

  return true;
}

bool CGFunction::tryQuote(const Handle &expr) {
  if (Util::arrayLength(expr) != 2 ||
      Util::arrayAt(expr, 0) != parent->symQuote) {
    return false;
  }
  emitConst(Util::arrayAt(expr, 1));
  return true;
}

bool CGFunction::tryPrimOp(const Handle &xs, bool isTail) {
  intptr_t len = Util::arrayLength(xs);
  if (len < 1) {
    return false;
  }

  const Handle opName = Util::arrayAt(xs, 0);

  //dprintf(2, "opName = %s, size = %ld\n", opName.c_str(), xs.size());

  if (opName == parent->symPrimAdd && len == 3) {
    compileExpr(Util::arrayAt(xs, 1));
    compileExpr(Util::arrayAt(xs, 2));
    __ pop(rax);
    shiftLocal(-1);
    __ add(rax, qword_ptr(rsp));
    __ sub(rax, RawObject::kFixnumTag);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == parent->symPrimSub && len == 3) {
    compileExpr(Util::arrayAt(xs, 1));
    compileExpr(Util::arrayAt(xs, 2));
    __ mov(rax, qword_ptr(rsp, kPtrSize));
    __ sub(rax, qword_ptr(rsp));
    __ add(rax, RawObject::kFixnumTag);
    __ add(rsp, kPtrSize);
    shiftLocal(-1);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == parent->symPrimLt && len == 3) {
    compileExpr(Util::arrayAt(xs, 1));
    compileExpr(Util::arrayAt(xs, 2));
    __ pop(rax);
    shiftLocal(-1);
    __ cmp(rax, qword_ptr(rsp));
    __ mov(rcx, Object::newTrue()->as<intptr_t>());
    __ mov(rax, Object::newFalse()->as<intptr_t>());
    __ cmovg(rax, rcx);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == parent->symPrimTrace && len == 3) {
    compileExpr(Util::arrayAt(xs, 1));
    __ pop(rdi);
    shiftLocal(-1);
    __ call(reinterpret_cast<intptr_t>(&Runtime::traceObject));
    compileExpr(Util::arrayAt(xs, 2), isTail);
  }
  else {
    return false;
  }

  return true;
}

void CGFunction::shiftLocal(intptr_t n) {
  // XXX: loss of encapsulation
  forEachListItem(locals,
      [=](const Handle &x, intptr_t _u, Object *_u2) -> bool {
    x->raw()->cdr() = Object::newFixnum(x->raw()->cdr()->fromFixnum() + n);
    return true;
  });
}

void CGFunction::emitConst(const Handle &expr) {
  __ mov(rax, expr->as<intptr_t>());
  __ push(rax);
  shiftLocal(1);

  if (expr->isHeapAllocated()) {
    recordLastPtrOffset();
  }
}

void CGFunction::recordLastPtrOffset() {
  intptr_t size = __ lastImmOffset().size,
           offset = __ lastImmOffset().offset;
  assert(size == 8);
  Util::arrayAppend(ptrOffsets,
      Object::newFixnum(offset - RawObject::kFuncCodeOffset));
}

