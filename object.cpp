#include "object.hpp"

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

void Object::printNewLine(int fd) {
  dprintf(fd, "\n");
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
    dprintf(fd, ">", raw);
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

