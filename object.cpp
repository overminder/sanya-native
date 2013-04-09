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
    if (raw->cloInfo()) {
      dprintf(fd, "<Closure ");
      raw->cloInfo()->funcName()->displayDetail(fd);
      dprintf(fd, ">");
    }
    else {
      // info table is null during compilation
      dprintf(fd, "<Semi-Closure %p>", raw);
    }
    break;

  case RawObject::kVectorTag:
  {
    dprintf(fd, "(#");
    for (intptr_t i = 0, len = raw->vectorSize(); i < len; ++i) {
      dprintf(fd, " ");
      raw->vectorAt(i)->displayDetail(fd);
    }
    dprintf(fd, ")");
    break;
  }

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
      RawObject *info = raw()->cloInfo();
      if (!info) {
        // info is NULL? happens when a supercombinator
        // is just created in the codegen.
        break;
      }
      Object **payload = raw()->cloPayload();
      for (intptr_t i = 0; i < info->funcNumPayload(); ++i) {
        ts->gcScavenge(payload + i);
      }
      ts->gcScavenge(&info->funcName());
      ts->gcScavenge(&info->funcConstOffset());

      // Scavenge const ptrs in code
      // @See codegen2.cpp
      //Util::logObj("scavenge code", info->funcConstOffset());
      intptr_t len = info->funcConstOffset()->raw()->vectorSize();
      for (intptr_t i = 0; i < len; ++i) {
        intptr_t offset = info->funcConstOffset()->
                          raw()->vectorAt(i)->fromFixnum();
        intptr_t ptrLoc = info->funcCodeAs<intptr_t>() + offset;
        //Util::logObj("scavenge const", *(Object **) ptrLoc);
        ts->gcScavenge(reinterpret_cast<Object **>(ptrLoc));
      }
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

