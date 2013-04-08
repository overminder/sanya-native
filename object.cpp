#include "object.hpp"
#include "gc.hpp"

void Object::printToFd(int fd) {
  RawObject *raw = unTag<RawObject>();
  switch (getTag()) {
  case RawObject::kPairTag:
    dprintf(fd, "<Pair @%p>", raw);
    break;

  case RawObject::kSymbolTag:
    dprintf(fd, "<Symbol %s>", raw->as<const char *>());
    break;

  case RawObject::kSingletonTag:
    if (this == newNil()) {
      dprintf(fd, "<Nil>");
    }
    else {
      dprintf(fd, "<Unknown-singleton %p>", this);
    }
    break;

  case RawObject::kFixnumTag:
    dprintf(fd, "<Fixnum %ld>", fromFixnum());
    break;

  case RawObject::kClosureTag:
    dprintf(fd, "<Closure ");
    raw->cloInfo()->funcName()->printToFd(fd);
    dprintf(fd, " @%p>", raw);
    break;

  case RawObject::kVectorTag:
    dprintf(fd, "<Vector %p>", raw);
    break;

  default:
    dprintf(fd, "<Unknown-ptr %p>", this);
    break;
  }
}

void Object::displayDetail(int fd) {
  RawObject *raw = unTag<RawObject>();
  switch (getTag()) {
  case RawObject::kPairTag:
    dprintf(fd, "(");
    displayListDetail(fd);
    dprintf(fd, ")");
    break;

  case RawObject::kSymbolTag:
    dprintf(fd, "%s", raw->as<const char *>());
    break;

  case RawObject::kSingletonTag:
    if (this == newNil()) {
      dprintf(fd, "()");
    }
    else if (this == newTrue()) {
      dprintf(fd, "#t");
    }
    else if (this == newFalse()) {
      dprintf(fd, "#f");
    }
    else {
      dprintf(fd, "<Unknown-singleton %p>", this);
    }
    break;

  case RawObject::kFixnumTag:
    dprintf(fd, "%ld", fromFixnum());
    break;

  case RawObject::kClosureTag:
    dprintf(fd, "<Closure ");
    raw->cloInfo()->funcName()->displayDetail(fd);
    dprintf(fd, ">");
    break;

  case RawObject::kVectorTag:
    dprintf(fd, "<Vector %p>", raw);
    break;

  default:
    dprintf(fd, "<Unknown-ptr %p>", this);
    break;
  }
}

void Object::displayListDetail(int fd) {
  raw()->car()->displayDetail(fd);
  Object *curr = raw()->cdr();

  while (curr->isPair()) {
    dprintf(fd, " ");
    curr->raw()->car()->displayDetail(fd);
    curr = curr->raw()->cdr();
  }
  
  if (curr != newNil()) {
    dprintf(fd, " . ");
    curr->displayDetail(fd);
  }
}

void Object::gcScavenge(ThreadState *ts) {
  switch (getTag()) {
    case RawObject::kPairTag:
      ts->gcScavenge(&raw()->car());
      ts->gcScavenge(&raw()->cdr());
      break;

    case RawObject::kSymbolTag:
      break;

    case RawObject::kSingletonTag:
    case RawObject::kFixnumTag:
      assert(0 && "Object::gcScavenge: not heap allocated");

    case RawObject::kClosureTag:
    {
      RawObject *info  = raw()->cloInfo();
      Object **payload = raw()->cloPayload();
      for (intptr_t i = 0; i < info->funcNumPayload(); ++i) {
        ts->gcScavenge(payload + i);
      }
      ts->gcScavenge(&info->funcName());
      ts->gcScavenge(&info->funcReloc());
      break;
    }
    case RawObject::kVectorTag:
    {
      Object **elems = &raw()->vectorElem();
      for (intptr_t i = 0; i < raw()->vectorSize(); ++i) {
        ts->gcScavenge(elems + i);
      }
      break;
    }
    default:
      assert(0 && "Object::gcScavenge: not a tagged object");
  }
}

