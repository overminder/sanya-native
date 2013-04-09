#include "gc.hpp"
#include "object.hpp"
#include "util.hpp"
#include "runtime.hpp"

#define KB 1024
#define MB (KB * KB)

intptr_t GcHeader::fromRuntimeAlloc(size_t size) {
  GcHeader h;
  h.setMarkAt<kCopied, false>();
  h.size = size;
  return *reinterpret_cast<intptr_t *>(&h);
}

ThreadState *ThreadState::global_ = NULL;

ThreadState *ThreadState::create() {
  void *raw = malloc(kLastOffset * sizeof(void *));
  ThreadState *ts = reinterpret_cast<ThreadState *>(raw);

  // Mocking compiled code info
  ts->lastFrameDescr() = FrameDescr();
  ts->firstStackPtr()  = 0;
  ts->lastStackPtr()   = 0;

  // Init gc
  ts->heapSize()      = 7 * KB;
  ts->heapBase()      = reinterpret_cast<intptr_t>(malloc(ts->heapSize() * 2));
  ts->heapPtr()       = ts->heapBase();
  ts->heapLimit()     = ts->heapBase() + ts->heapSize();
  ts->heapFromSpace() = ts->heapBase();
  ts->heapToSpace()   = ts->heapBase() + ts->heapSize();

  // Create linkedlist head
  ts->handleHead() = reinterpret_cast<Handle *>(malloc(sizeof(Handle)));
  ts->handleHead()->initFromThreadState(ts);

  // There's only one intern table
  ts->symbolInternTable() = NULL;

  return ts;
}

void ThreadState::initGlobalState() {
  global_ = ThreadState::create();

  // Create global symbol intern table
  global_->symbolInternTable() = Util::newAssocList();
}

void *ThreadState::initGcHeader(intptr_t raw, size_t size) {
  GcHeader *h = reinterpret_cast<GcHeader *>(raw);
  h->setMarkAt<GcHeader::kCopied, false>();
  h->size = size;
  return reinterpret_cast<void *>(h->toRawObject());
}

void ThreadState::destroy() {
  free(handleHead());
  free(reinterpret_cast<void *>(heapBase()));
  free(this);
}

void ThreadState::display(int fd) {
  dprintf(fd, "[ThreadState] Hp = %ld, HpLim = %ld\n", heapPtr(), heapLimit());
}

void *ThreadState::gcAllocSlow(size_t size) {
  heapPtr() -= size;
  lastAllocReq() = size;
  gcCollect();

  intptr_t raw = heapPtr();
  heapPtr() += size;
  return initGcHeader(raw, size);
}

// XXX: tags
void ThreadState::gcScavenge(Object **loc) {
  Object *ptr = *loc;

  if (!ptr || !ptr->isHeapAllocated()) {
    return;
  }
  RawObject::Tag ptrTag = ptr->getTag();
  GcHeader *h = GcHeader::fromRawObject(ptr->raw());

  if (h->markAt<GcHeader::kCopied>()) {
    // If is in from space and already copied: do redirection
    *loc = h->copiedTo->toRawObject()->tagWith(ptrTag);
    return;
  }
  else if (isInToSpace(h)) {
    // If is in to space: do nothing
    return;
  }

  //dprintf(2, "[GC] Scavenge %p: ", ptr->raw());
  //ptr->displayDetail(2);
  //dprintf(2, "\n");

  h->setMarkAt<GcHeader::kCopied, true>();

  // Do copy
  GcHeader *newH = reinterpret_cast<GcHeader *>(heapCopyPtr());
  memcpy(newH, h, h->size);
  heapCopyPtr() += h->size;

  // Redirect loc and interior ptrs.
  newH->setMarkAt<GcHeader::kCopied, false>();
  h->copiedTo = newH;
  Object *newPtr = newH->toRawObject()->tagWith(ptrTag);
  *loc = newPtr;
  newPtr->gcScavenge(this);
}

void ThreadState::gcCollect() {
  //dprintf(2, "[GC] Collect\n");

  // Invariant: we are using simple semispace gc
  heapCopyPtr() = heapToSpace();

  // Scavenge C++ roots
  for (Handle *iter = handleHead()->next;
       iter != handleHead(); iter = iter->next) {
    gcScavenge(&iter->ptr);
  }

  gcScavengeSchemeStack();

  // Scavenge symbol intern table
  gcScavenge(&symbolInternTable());

  intptr_t tmpSpace = heapFromSpace();
  heapFromSpace()   = heapToSpace();
  heapToSpace()     = tmpSpace;
  heapPtr()         = heapCopyPtr();
  heapLimit()       = heapFromSpace() + heapSize();

  dprintf(2, "[gcCollect] (%ld/%ld)\n",
              heapSize() - (heapLimit() - heapPtr()),
              heapSize());

  if (heapLimit() - heapPtr() < (intptr_t) lastAllocReq()) {
    dprintf(2, "gcCollect: heap exhausted by req %ld\n", lastAllocReq());
  }
}

// @See Runtime::collectAndAlloc
void ThreadState::gcScavengeSchemeStack() {
  FrameDescr fd = lastFrameDescr();
  intptr_t stackPtr = lastStackPtr();
  intptr_t stackTop = firstStackPtr();

  if (stackPtr == stackTop) {
    return;
  }

  while (true) {
    for (intptr_t i = 0; i < fd.frameSize; ++i) {
      if (fd.isPtr(i)) {
        Object **loc = reinterpret_cast<Object **>(stackPtr + i * 8);
        gcScavenge(loc);
      }
    }

    stackPtr += (1 + fd.frameSize) * 8;
    if (stackPtr == stackTop) {
      break;
    }
    assert(stackPtr < stackTop);
    fd = *reinterpret_cast<FrameDescr *>(stackPtr - 16);
  }
}


