#include "gc.hpp"
#include "object.hpp"

#define KB 1024
#define MB (KB * KB)

ThreadState *ThreadState::global_ = NULL;

ThreadState *ThreadState::create() {
  void *raw = malloc(kLastOffset * sizeof(void *));
  ThreadState *ts = reinterpret_cast<ThreadState *>(raw);

  // Mocking compiled code info
  ts->lastFrameDescr() = NULL;
  ts->firstStackPtr()  = 0;
  ts->lastStackPtr()   = 0;

  // Init gc
  ts->heapSize()      = 256 * KB;
  ts->heapBase()      = reinterpret_cast<intptr_t>(malloc(ts->heapSize() * 2));
  ts->heapPtr()       = ts->heapBase();
  ts->heapLimit()     = ts->heapBase() + ts->heapSize();
  ts->heapFromSpace() = ts->heapBase();
  ts->heapToSpace()   = ts->heapBase() + ts->heapSize();

  // Create linkedlist head
  ts->handleHead() = reinterpret_cast<Handle *>(malloc(sizeof(Handle)));
  ts->handleHead()->initFromThreadState(ts);

  return ts;
}

void ThreadState::destroy() {
  free(handleHead());
  free(reinterpret_cast<void *>(heapBase()));
  free(this);
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

  dprintf(2, "[GC] Scavenge %p: ", ptr->raw());
  ptr->displayDetail(2);
  dprintf(2, "\n");

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
  dprintf(2, "[GC] Collect\n");

  // Invariant: we are using simple semispace gc
  heapCopyPtr() = heapToSpace();

  for (Handle *iter = handleHead()->next;
       iter != handleHead(); iter = iter->next) {
    gcScavenge(&iter->ptr);
  }

  intptr_t tmpSpace = heapFromSpace();
  heapFromSpace()   = heapToSpace();
  heapToSpace()     = tmpSpace;
  heapPtr()         = heapCopyPtr();
  heapLimit()       = heapFromSpace() + heapSize();

  if (heapLimit() - heapPtr() < (intptr_t) lastAllocReq()) {
    dprintf(2, "gcCollect: heap exhausted by req %ld\n", lastAllocReq());
  }
}


