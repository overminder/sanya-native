#include <stdio.h>
#include <stdlib.h>

#include "runtime.hpp"
#include "gc.hpp"
#include "object.hpp"

void Runtime::handleNotAClosure(Object *wat) {
  dprintf(2, "Not a closure: ");
  wat->displayDetail(2);
  dprintf(2, "\n");

  exit(1);
}

void Runtime::handleArgCountMismatch(Object *wat, intptr_t argc) {
  dprintf(2, "Argument count mismatch: ");
  wat->displayDetail(2);
  dprintf(2, " need %ld, but got %ld\n",
          wat->raw()->cloInfo()->funcArity(), argc);

  exit(1);
}

void Runtime::handleUserError(Object *wat, ThreadState *ts) {
  Util::logObj("UserError", wat);
  dprintf(2, "### Stack trace:\n");

  FrameDescr fd = ts->lastFrameDescr();
  intptr_t stackPtr = ts->lastStackPtr();
  intptr_t stackTop = ts->firstStackPtr();

  // Do a stack walk.
  // XXX: duplicate code since gcScavScheme does almost the same.
  intptr_t level = 0;
  while (true) {
    Object *thisClosure = NULL;
    for (intptr_t i = 0; i < fd.frameSize; ++i) {
      if (fd.isPtr(i)) {
        Object **loc = reinterpret_cast<Object **>(stackPtr + i * 8);
        if (i == fd.frameSize - 2) {
          // fd, thisClo.
          thisClosure = *loc;
        }
        else {
          dprintf(2, "#%-2ld Frame[%ld] ", level, i);
          (*loc)->displayDetail(2);
          dprintf(2, "\n");
        }
      }
    }
    assert(thisClosure);
    dprintf(2, "#%-2ld ^ Inside ", level);
    thisClosure->displayDetail(2);
    dprintf(2, "\n");
    ++level;

    // Find prev stack
    stackPtr += (1 + fd.frameSize) * 8;
    if (stackPtr == stackTop) {
      break;
    }
    assert(stackPtr < stackTop);
    fd = *reinterpret_cast<FrameDescr *>(stackPtr - 16);
  }

  ts->destroy();
  exit(1);
}

void Runtime::collectAndAlloc(ThreadState *ts) {
  // 8: return address slot
  dprintf(2, "[Runtime::collect]\n");
  ts->gcCollect();

  /*
  FrameDescr fd = ts->lastFrameDescr();
  while (true) {
    dprintf(2, "[Stack Walk] stackPtr = %p, fd = %ld, size = %d\n",
            (void *) stackPtr, fd.pack(), fd.frameSize);
    for (intptr_t i = 0; i < fd.frameSize; ++i) {
      if (fd.isPtr(i)) {
        dprintf(2, "[Stack Walk] %ld isPtr\n", i);
        Util::logObj("E.g.", *(Object **) (stackPtr + i * 8));
      }
    }
    // Get next stackPtr
    stackPtr += (1 + fd.frameSize) * 8;

    if (stackPtr == ts->firstStackPtr()) {
      dprintf(2, "[Stack Walk] done\n");
      break;
    }
    assert(stackPtr < ts->firstStackPtr());
    // Get next framedescr. -2 slot for (fd + retAddr)
    fd = *reinterpret_cast<FrameDescr *>(stackPtr - 16);
  }

  exit(1);
  */
}

void Runtime::traceObject(Object *wat) {
  dprintf(2, "[Runtime::Trace] ");
  wat->displayDetail(2);
  dprintf(2, "\n");
}

intptr_t Runtime::endOfCode(intptr_t entry) {
  auto info = RawObject::from(entry - RawObject::kFuncCodeOffset);
  return entry + info->funcSize() - RawObject::kFuncCodeOffset;
}

void Runtime::printNewLine(int fd) {
  dprintf(fd, "\n");
}

static Option option;

void Option::init() {
  if (option.kInitialized) {
    return;
  }

  option.kInitialized = true;

  char *notco = getenv("SANYA_TCO");
  if (notco && strcmp(notco, "NO") == 0) {
    option.kTailCallOpt = false;
  }
  else {
    option.kTailCallOpt = true;
  }
}

Option &Option::global() {
  return option;
}


