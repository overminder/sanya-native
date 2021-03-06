#include "gc.hpp"
#include "object.hpp"
#include "util.hpp"
#include "runtime.hpp"

#define KB 1024
#define MB (KB * KB)

ThreadState *ThreadState::global_ = NULL;

ThreadState *ThreadState::create() {
  void *raw = malloc(kLastOffset * sizeof(void *));
  ThreadState *ts = reinterpret_cast<ThreadState *>(raw);

  // Mocking compiled code info
  ts->lastFrameDescr() = FrameDescr();
  ts->firstStackPtr()  = 0;
  ts->lastStackPtr()   = 0;

  // Init gc
  ts->heapSize()      = 256 * KB;
#ifndef kSanyaGCDebug
  ts->heapBase()      = (intptr_t) malloc(ts->heapSize() * 2);
  ts->heapPtr()       = ts->heapBase();
  ts->heapLimit()     = ts->heapBase() + ts->heapSize();
  ts->heapFromSpace() = ts->heapBase();
  ts->heapToSpace()   = ts->heapBase() + ts->heapSize();
#else
  ts->heapBase()      = (intptr_t) malloc(ts->heapSize());
  ts->heapPtr()       = ts->heapBase();
  ts->heapLimit()     = ts->heapBase() + ts->heapSize();
  ts->heapFromSpace() = ts->heapBase();
#endif

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
  h->mark = 0;
  h->setMarkAt<GcHeader::kCopied, false>();
  h->size = size;
  h->copiedTo = NULL;
  return reinterpret_cast<void *>(h->toRawObject());
}

void ThreadState::destroy() {
  free(handleHead());
  free(reinterpret_cast<void *>(heapBase()));
  free(this);
}

void ThreadState::display(int fd) {
  dprintf(fd, "[ThreadState] Hp = %ld, HpLim = %ld\n",
          heapPtr(), heapLimit());
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
  //dprintf(2, "[GcScav] [%p] %ld (%p)\n", loc, (intptr_t) ptr, ptr);
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
#ifndef kSanyaGCDebug
  heapCopyPtr() = heapToSpace();
#else
  heapToSpace() = (intptr_t) malloc(heapSize());
  heapCopyPtr() = heapToSpace();
#endif

  // Scavenge C++ roots
  for (Handle *iter = handleHead()->next;
       iter != handleHead(); iter = iter->next) {
    gcScavenge(&iter->ptr);
  }

  gcScavengeSchemeStack();

  // Scavenge symbol intern table
  gcScavenge(&symbolInternTable());

#ifndef kSanyaGCDebug
  intptr_t tmpSpace = heapFromSpace();
  heapFromSpace()   = heapToSpace();
  heapToSpace()     = tmpSpace;
#else
  free((void *) heapBase());
  heapBase() = heapFromSpace() = heapToSpace();
#endif
  heapPtr()         = heapCopyPtr();
  heapLimit()       = heapFromSpace() + heapSize();

  if (Option::global().kLogInfo) {
    dprintf(2, "[gcCollect] (%ld/%ld)\n",
                heapSize() - (heapLimit() - heapPtr()),
                heapSize());
  }

  if (heapLimit() - heapPtr() < (intptr_t) lastAllocReq()) {
    dprintf(2, "gcCollect: heap exhausted by req %ld\n", lastAllocReq());
    exit(1);
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

  //dprintf(2, "[ScavScm] lastScmSp = %p, retaddr = %p\n",
  //        (void **) stackPtr,
  //        ((void **) stackPtr)[-1]);

  while (true) {
    for (intptr_t i = 0; i < fd.frameSize; ++i) {
      if (fd.isPtr(i)) {
        Object **loc = reinterpret_cast<Object **>(stackPtr + i * 8);
        //Util::logObj("ScavengeScm Before", *loc);

        //if ((*loc)->isHeapAllocated()) {
        //  GcHeader *h = GcHeader::fromRawObject((*loc)->raw());
        //  dprintf(2, "[GcHeader] size = %d, copied = %d\n",
        //          h->size, h->markAt<GcHeader::kCopied>());
        //}

        gcScavenge(loc);
        //Util::logObj("ScavengeScm After", *loc);
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


