#include <assert.h>

#include "codegen2.hpp"
#include "runtime.hpp"

using namespace AsmJit;

static const int kPtrSize = sizeof(void *);
static const GpReg kArgRegs[5] = { rsi, rdx, rcx, r8, r9 };
static const GpReg kArgRegsWithClosure[6] = { rdi, rsi, rdx, rcx, r8, r9 };
static const auto kClosureReg    = rdi,
                  kFrameDescrReg = r10,
                  kHeapPtr       = r12,
                  kHeapLimit     = r13,
                  kThreadState   = r14;

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
  symBegin     = Object::internSymbol("begin");
  symIf        = Object::internSymbol("if");
  symPrimAdd   = Object::internSymbol("+#");
  symPrimSub   = Object::internSymbol("-#");
  symPrimLt    = Object::internSymbol("<#");
  symPrimCons  = Object::internSymbol("cons#");
  symPrimTrace = Object::internSymbol("trace#");
  symPrimError = Object::internSymbol("error#");
  symMain      = Object::internSymbol("main");

#define DEF_SYM(scmName, cxxName) \
  symPrim ## cxxName ## p = Object::internSymbol(#scmName "#");
PRIM_TAG_PREDICATES(DEF_SYM)
PRIM_SINGLETON_PREDICATES(DEF_SYM)
#undef DEF_SYM

#define DEF_SYM(scmName, _unused, attrName) \
  symPrim ## attrName = Object::internSymbol(#scmName "#");
PRIM_ATTR_ACCESSORS(DEF_SYM)
#undef DEF_SYM
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
  : frameSize(0)
  , name(name)
  , lamBody(lamBody)
  , parent(parent)
  , locals(Util::newAssocList())
  , stackItemList(Object::newNil())
  , ptrOffsets(Util::newGrowableArray())
  , relocArray(Util::newGrowableArray())
{ }

const Handle &CGFunction::makeClosure() {
  assert(!closure.getPtr());
  closure = Object::newClosure(NULL);
  //dprintf(2, "[mkClosure] %s => %p\n", name->rawSymbol(), closure);
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
  pushReg(kFrameDescrReg, kIsNotPtr);

  // push thisClosure
  pushReg(kClosureReg, kIsPtr);

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
    pushReg(kArgRegs[i], kIsPtr);
    addNewLocal(arg);
  }

  // TCO can be runtime-specified
  compileBody(lamBody, 2, Option::global().kTailCallOpt);

  // Return last value on the stack
  popReg(rax);
  popFrame();
  __ ret();

  // Used for debugging
  intptr_t codeSize = __ getCodeSize();
  // Create function and patch closure.
  void *rawPtr = __ make();

  Handle trimmedConstOffsets = Util::arrayToVector(ptrOffsets);

  rawFunc = Object::newFunction(rawPtr, arity, name,
      /* const ptr offset array */ trimmedConstOffsets,
      /* num payload */ 0);
  rawFunc->funcSize() = codeSize;
  closure->raw()->cloInfo() = rawFunc;

  // Patch relocs
  for (intptr_t i = 0, len = Util::arrayLength(relocArray);
       i < len; ++i) {
    intptr_t base = rawFunc->funcCodeAs<intptr_t>();
    Handle ptrVal = Util::arrayAt(relocArray, i);
    intptr_t offset = Util::arrayAt(ptrOffsets, i)->fromFixnum();
    Object **addr = reinterpret_cast<Object **>(base + offset);
    *addr = ptrVal;

    //dprintf(2, "[PatchCodeReloc] %s[%ld] ", name->rawSymbol(), offset);
    //ptrVal->displayDetail(2);
    //dprintf(2, "\n");
  }

  Util::logPtr("CompileFunction Done", rawFunc->funcCodeAs<void *>());
}

void CGFunction::compileBody(const Handle &body, intptr_t start,
                             bool isTail) {
  intptr_t len = Util::arrayLength(body);
  for (intptr_t i = start; i < len; ++i) {
    Handle x = Util::arrayAt(body, i);
    if (i == len - 1) {
      compileExpr(x, isTail);
    }
    else {
      compileExpr(x, false);
      popSome(1);
    }
  }
}

void CGFunction::compileExpr(const Handle &expr, bool isTail) {
  intptr_t ix;

  switch (expr->getTag()) {
  case RawObject::kFixnumTag:
    pushObject(expr);
    break;

  case RawObject::kSymbolTag:
    // Lookup first
    ix = lookupLocal(expr);
    if (ix != -1) {
      // Is local
      __ mov(rax, qword_ptr(rsp, ix * kPtrSize));
      pushReg(rax, kIsPtr);
      break;
    }
    else {
      ix = parent->lookupGlobal(expr);
      if (ix == -1) {
        dprintf(2, "lookupGlobal: %s not found\n", expr->rawSymbol());
        exit(1);
      }
      __ mov(rax, 0L);
      // References a ptr so need to do this
      recordLastPtrOffset();
      recordReloc(parent->moduleGlobalVector);

      __ mov(rax, qword_ptr(rax,
            RawObject::kVectorElemOffset - RawObject::kVectorTag +
            kPtrSize * ix));
      pushReg(rax, kIsPtr);
      break;
    }

  case RawObject::kPairTag:
  {
    Handle xs = Util::newGrowableArray();
    assert(listToArray(expr, &xs)->isNil());

    // Check for define
    if (tryIf(xs, isTail)) {
    }
    else if (tryBegin(xs, isTail)) {
    }
    else if (tryQuote(xs)) {
    }
    else if (tryPrimOp(xs, isTail)) {
    }
    else {
      // Should be funcall
      compileCall(xs, isTail);
    }
    break;
  }

  case RawObject::kSingletonTag:
    if (expr->isTrue() || expr->isFalse()) {
      pushObject(expr);
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
  intptr_t argc = Util::arrayLength(xs) - 1;
  assert(argc < 6);

  for (intptr_t i = 0; i < argc + 1; ++i) {
    // Evaluate func and args
    compileExpr(Util::arrayAt(xs, i), false);
  }

  for (intptr_t i = argc; i >= 0; --i) {
    // Reverse pop values to argument pos
    popReg(kArgRegsWithClosure[i]);
  }

  // Check closure type
  __ mov(rax, rdi);
  __ and_(eax, RawObject::kTagMask);
  __ cmp(eax, RawObject::kClosureTag);
  auto labelNotAClosure = __ newLabel();
  __ jne(labelNotAClosure);

  // Check arg count
  __ mov(rax, qword_ptr(rdi, -RawObject::kClosureTag));
  __ mov(rax, qword_ptr(rax, RawObject::kFuncArityOffset));
  __ cmp(rax, argc);
  auto labelArgCountMismatch = __ newLabel();
  __ jne(labelArgCountMismatch);

  // Extract and call the code pointer
  // XXX: how about stack overflow checking?
  __ mov(rax, qword_ptr(rdi, -RawObject::kClosureTag));
  __ lea(rax, qword_ptr(rax, RawObject::kFuncCodeOffset));

  auto labelOk = __ newLabel();
  if (!isTail) {
    __ mov(kFrameDescrReg, makeFrameDescr());

    // If doing normal call:
    __ call(rax);

    // After call
    pushReg(rax, kIsPtr);
    __ jmp(labelOk);
  }
  else {
    // Get caller's FD
    __ mov(kFrameDescrReg, qword_ptr(rsp, getFrameDescr()));

    popPhysicalFrame();
    // Tail call
    __ jmp(rax);

    // To compensate for stack depth
    pushVirtual(kIsPtr);
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

bool CGFunction::tryIf(const Handle &xs, bool isTail) {
  intptr_t len = Util::arrayLength(xs);
  if (len != 4 || !(Util::arrayAt(xs, 0) == parent->symIf)) {
    return false;
  }

  Label labelFalse = __ newLabel(),
        labelDone  = __ newLabel();

  // Pred
  compileExpr(Util::arrayAt(xs, 1));
  popReg(rax);

  __ cmp(rax, Object::newFalse()->as<intptr_t>());
  __ je(labelFalse);

  compileExpr(Util::arrayAt(xs, 2), isTail);
  __ jmp(labelDone);
  // Since we need to balance out those two branches
  popVirtual(1);

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
  pushObject(Util::arrayAt(expr, 1));
  return true;
}

bool CGFunction::tryBegin(const Handle &expr, bool isTail) {
  if (Util::arrayAt(expr, 0) != parent->symBegin) {
    return false;
  }
  compileBody(expr, 1, isTail);
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
    popReg(rax);
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
    popSome(1);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == parent->symPrimLt && len == 3) {
    compileExpr(Util::arrayAt(xs, 1));
    compileExpr(Util::arrayAt(xs, 2));
    popReg(rax);
    __ cmp(rax, qword_ptr(rsp));
    __ mov(ecx, Object::newTrue()->as<intptr_t>());
    __ mov(eax, Object::newFalse()->as<intptr_t>());
    __ cmovg(rax, rcx);
    __ mov(qword_ptr(rsp), rax);
  }
  else if (opName == parent->symPrimCons && len == 3) {
    // (cons# 1 2)
    compileExpr(Util::arrayAt(xs, 1));
    compileExpr(Util::arrayAt(xs, 2));

    allocPair();
  }

#define MK_IMPL(_unused, klsName, attrName)                             \
  else if (opName == parent->symPrim ## attrName && len == 2) {         \
    compileExpr(Util::arrayAt(xs, 1));                                  \
    popReg(rax);                                                        \
    __ mov(rax, qword_ptr(rax,                                          \
          RawObject::k ## attrName ## Offset -                          \
          RawObject::k ## klsName ## Tag));                             \
    pushReg(rax, kIsPtr);                                               \
  }
PRIM_ATTR_ACCESSORS(MK_IMPL)
#undef MK_IMPL

#define MK_IMPL(_unused, typeName)                                      \
  else if (opName == parent->symPrim ## typeName ## p && len == 2) {    \
    compileExpr(Util::arrayAt(xs, 1));                                  \
    popReg(rax);                                                        \
    __ and_(eax, RawObject::kTagMask);                                  \
    __ cmp(eax, RawObject::k ## typeName ## Tag);                       \
    __ mov(ecx, Object::newTrue()->as<intptr_t>());                     \
    __ mov(eax, Object::newFalse()->as<intptr_t>());                    \
    __ cmove(eax, ecx);                                                 \
    pushReg(rax, kIsPtr);                                               \
  }
PRIM_TAG_PREDICATES(MK_IMPL)
#undef MK_IMPL

#define MK_IMPL(_unused, objName)                                       \
  else if (opName == parent->symPrimNilp && len == 2) {                 \
    compileExpr(Util::arrayAt(xs, 1));                                  \
    popReg(rax);                                                        \
    __ mov(ecx, reinterpret_cast<intptr_t>(Object::new ## objName()));  \
    __ cmp(rax, rcx);                                                   \
    __ mov(ecx, Object::newTrue()->as<intptr_t>());                     \
    __ mov(eax, Object::newFalse()->as<intptr_t>());                    \
    __ cmove(eax, ecx);                                                 \
    pushReg(rax, kIsPtr);                                               \
  }
PRIM_SINGLETON_PREDICATES(MK_IMPL)
#undef MK_IMPL

  else if (opName == parent->symPrimTrace && len == 3) {
    compileExpr(Util::arrayAt(xs, 1));
    popReg(rdi);
    __ call(reinterpret_cast<intptr_t>(&Runtime::traceObject));
    compileExpr(Util::arrayAt(xs, 2), isTail);
  }
  else if (opName == parent->symPrimError && len == 2) {
    // (error# anything)
    compileExpr(Util::arrayAt(xs, 1));

    popReg(rdi);
    syncThreadState();
    __ mov(rsi, kThreadState);
    __ jmp((void *) &Runtime::handleUserError);
  }
  else {
    return false;
  }

  return true;
}

void CGFunction::allocPair() {
  size_t hSize = sizeof(GcHeader);
  size_t rawAllocSize = RawObject::kSizeOfPair + hSize;
  assert(Util::isAligned<4>(rawAllocSize));
  assert(hSize == 0x10);

  auto labelAllocOk = __ newLabel();

#ifndef kSanyaGCDebug
  // Try alloc
  __ lea(rcx, qword_ptr(kHeapPtr, rawAllocSize));
  __ cmp(rcx, kHeapLimit);
  __ jle(labelAllocOk);
#endif

  // Alloc failed: Do GC
  syncThreadState();
  __ mov(rax, rawAllocSize);
  __ mov(qword_ptr(kThreadState,
        kPtrSize * ThreadState::kLastAllocReqOffset),
        rax);

  __ mov(rdi, kThreadState);
  __ call(reinterpret_cast<void *>(&Runtime::collectAndAlloc));
  // Extract new heapPtr and limitPtr
  __ mov(kHeapPtr,
      qword_ptr(kThreadState, kPtrSize * ThreadState::kHeapPtrOffset));
  __ mov(kHeapLimit,
      qword_ptr(kThreadState, kPtrSize * ThreadState::kHeapLimitOffset));
  // And retry
  __ lea(rcx, qword_ptr(kHeapPtr, rawAllocSize));

  // Alloc ok: fill content
  __ bind(labelAllocOk);
  // Init gc header
  // Mark: 0
  __ mov(dword_ptr(kHeapPtr, 0), 0);
  // size: (precalculated)
  __ mov(dword_ptr(kHeapPtr, 4), rawAllocSize);

  // Put cdr
  popReg(rax);
  __ mov(qword_ptr(kHeapPtr, hSize + RawObject::kCdrOffset), rax);

  // Put car
  popReg(rax);
  __ mov(qword_ptr(kHeapPtr, hSize + RawObject::kCarOffset), rax);

  // GcHeader padding and tag
  __ add(kHeapPtr, hSize + RawObject::kPairTag);
  pushReg(kHeapPtr, kIsPtr);

  // Write new heapPtr back
  __ mov(kHeapPtr, rcx);
}

void CGFunction::shiftLocal(intptr_t n) {
  // XXX: loss of encapsulation
  forEachListItem(locals,
      [=](const Handle &x, intptr_t _u, Object *_u2) -> bool {
    x->raw()->cdr() = Object::newFixnum(x->raw()->cdr()->fromFixnum() + n);
    return true;
  });
}

intptr_t CGFunction::getThisClosure() {
  return (frameSize - 2) * kPtrSize;
}

intptr_t CGFunction::getFrameDescr() {
  return (frameSize - 1) * kPtrSize;
}

void CGFunction::recordReloc(const Handle &e) {
  Util::arrayAppend(relocArray, e);
}

void CGFunction::recordLastPtrOffset() {
  intptr_t size = __ lastImmOffset().size,
           offset = __ lastImmOffset().offset;
  assert(size == 8);
  Util::arrayAppend(ptrOffsets,
      Object::newFixnum(offset - RawObject::kFuncCodeOffset));
}

void CGFunction::pushObject(const Handle &x) {
  if (x->isHeapAllocated()) {
    // To be patched later. See @Invariant
    __ mov(rax, 0L);
    recordReloc(x);
    recordLastPtrOffset();
  }
  else {
    __ mov(rax, x->as<intptr_t>());
  }
  __ push(rax);
  pushVirtual(kIsPtr);
}

void CGFunction::pushInt(intptr_t i) {
  __ mov(rax, i);
  __ push(rax);
  pushVirtual(kIsNotPtr);
}

void CGFunction::pushReg(const GpReg &r, IsPtr isPtr) {
  __ push(r);
  pushVirtual(isPtr);
}

void CGFunction::pushVirtual(IsPtr isPtr) {
  shiftLocal(1);
  stackItemList = Object::newPair(Object::newBool(isPtr == kIsPtr),
                                  stackItemList);
  ++frameSize;
  //dprintf(2, "[pushV] += 1, frameSize = %ld\n", frameSize);
}

void CGFunction::popSome(intptr_t n) {
  __ add(rsp, kPtrSize * n);
  popVirtual(n);
}

void CGFunction::popFrame() {
  popSome(frameSize);
}

void CGFunction::popPhysicalFrame() {
  __ add(rsp, kPtrSize * frameSize);
}

void CGFunction::popReg(const GpReg &r) {
  __ pop(r);
  popVirtual(1);
}

void CGFunction::popVirtual(intptr_t n) {
  shiftLocal(-n);
  for (intptr_t i = 0; i < n; ++i) {
    stackItemList = stackItemList->raw()->cdr();
  }
  frameSize -= n;
  //dprintf(2, "[popV] -= %ld, frameSize = %ld\n", n, frameSize);
}

intptr_t CGFunction::makeFrameDescr() {
  FrameDescr fd;
  // Current max fd size.
  assert(frameSize <= 48);
  fd.frameSize = frameSize;
  Handle stackIter = stackItemList;
  for (intptr_t i = 0; i < frameSize; ++i) {
    assert(stackIter->isPair());
    if (stackIter->raw()->car()->isTrue()) {
      //dprintf(2, "[mkFd %s] %ld is ptr\n", name->rawSymbol(), i);
      fd.setIsPtr(i);
    }
    else {
      //dprintf(2, "[mkFd %s] %ld is not ptr\n", name->rawSymbol(), i);
      assert(!fd.isPtr(i));
    }
    stackIter = stackIter->raw()->cdr();
  }
  //dprintf(2, "[mkFd %s] fd = %ld\n", name->rawSymbol(), fd.pack());
  return fd.pack();
}

void CGFunction::syncThreadState() {
  // Store gc info
  __ mov(
      qword_ptr(kThreadState, kPtrSize * ThreadState::kHeapPtrOffset),
      kHeapPtr);
  __ mov(
      qword_ptr(kThreadState, kPtrSize * ThreadState::kHeapLimitOffset),
      kHeapLimit);

  // Store frame descr
  __ mov(rax, makeFrameDescr());
  __ mov(
      qword_ptr(kThreadState,
        kPtrSize * ThreadState::kLastFrameDescrOffset),
      rax);

  // And stack ptr
  __ mov(
      qword_ptr(kThreadState,
        kPtrSize * ThreadState::kLastStackPtrOffset),
      rsp);
}

