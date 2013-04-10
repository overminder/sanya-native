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

  FrameDescr fd = ts->lastFrameDescr();
  intptr_t stackPtr = ts->lastStackPtr();

  for (intptr_t i = 0; i < fd.frameSize; ++i) {
    if (fd.isPtr(i)) {
      Object **loc = reinterpret_cast<Object **>(stackPtr + i * 8);
      Util::logObj("Frame Local", *loc);
    }
  }
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

