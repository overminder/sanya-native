#include "util.hpp"
#include "object.hpp"

namespace Util {

void logPtr(const char *wat, Object *x, int fd) {
  dprintf(fd, "[%s] %p\n", wat, x);
}

void logObj(const char *wat, Object *x, int fd) {
  dprintf(fd, "[%s] ", wat);
  x->displayDetail(fd);
  dprintf(fd, "\n");
}

Object *newAssocList() {
  // ((key . val) (key2 . val2))
  return Object::newNil();
}

intptr_t assocLength(const Handle &assoc) {
  Handle iter = assoc;
  intptr_t i = 0;
  while (!iter->isNil()) {
    iter = iter->raw()->cdr();
    ++i;
  }
  return i;
}

static Object *assocLookupEntry(const Handle &assoc, const Handle &key,
                                EqFunc eqf) {
  for (Handle iter = assoc; !iter->isNil(); iter = iter->raw()->cdr()) {
    Handle entry = iter->raw()->car();
    if (eqf == kSymbolEq) {
      if (strcmp(key->rawSymbol(), entry->raw()->car()->rawSymbol()) == 0) {
        return entry.getPtr();
      }
    }
    else if (eqf == kPtrEq) {
      if (key.getPtr() == entry->raw()->car()) {
        return entry.getPtr();
      }
    }
  }
  return NULL;
}

Object *assocLookup(const Handle &assoc, const Handle &key,
                    EqFunc eqf, bool *ok) {
  Handle entry = assocLookupEntry(assoc, key, eqf);
  if (entry.getPtr() == NULL) {
    if (ok) *ok = false;
    return NULL;
  }
  else {
    if (ok) *ok = true;
    return entry->raw()->cdr();
  }
}

Object *assocLookupKey(const Handle &assoc, const Handle &key,
                       EqFunc eqf, bool *ok) {
  Handle entry = assocLookupEntry(assoc, key, eqf);
  if (entry.getPtr() == NULL) {
    if (ok) *ok = false;
    return NULL;
  }
  else {
    if (ok) *ok = true;
    return entry->raw()->car();
  }
}

Object *assocInsert(const Handle &assoc, const Handle &key,
                    const Handle &val, EqFunc eqf) {
  Handle entry = assocLookupEntry(assoc, key, eqf);
  if (entry.getPtr() == NULL) {
    Handle newEntry = Object::newPair(key, val);
    return Object::newPair(newEntry, assoc);
  }
  else {
    entry->raw()->cdr() = val.getPtr();
    return assoc.getPtr();
  }
}

Object *newGrowableArray() {
  // ((# 1 2 3 () ()) . 3)
  Handle vec = Object::newVector(0, NULL);
  return Object::newPair(vec, Object::newFixnum(0));
}

void arrayAppend(const Handle &arr, const Handle &item) {
  Handle vec = arr->raw()->car();
  intptr_t size = vec->raw()->vectorSize();
  intptr_t nextIx = arr->raw()->cdr()->fromFixnum();
  if (nextIx < size) {
    vec->raw()->vectorAt(nextIx) = item.getPtr();
    arr->raw()->cdr() = Object::newFixnum(nextIx + 1);
  }
  else {
    // Resize
    intptr_t newSize = size * 2 + 1;
    Handle newVec = Object::newVector(newSize, Object::newNil());
    for (intptr_t i = 0; i < size; ++i) {
      newVec->raw()->vectorAt(i) = vec->raw()->vectorAt(i);
    }
    arr->raw()->car() = newVec.getPtr();

    // Try again
    arrayAppend(arr, item);
  }
}

Object *&arrayAt(const Handle &arr, intptr_t ix) {
  intptr_t len = arr->raw()->cdr()->fromFixnum();
  if (ix < 0) {
    ix += len;
    if (ix < 0) {
      ix = 0;
    }
  }

  return arr->raw()->car()->raw()->vectorAt(ix);
}

intptr_t arrayLength(const Handle &arr) {
  return arr->raw()->cdr()->fromFixnum();
}

Object *arrayToVector(const Handle &arr) {
  intptr_t len = Util::arrayLength(arr);
  Handle vec = Object::newVector(len, Object::newNil());
  for (intptr_t i = 0; i < len; ++i) {
    vec->raw()->vectorAt(i) = Util::arrayAt(arr, i);
  }
  return vec;
}

}
