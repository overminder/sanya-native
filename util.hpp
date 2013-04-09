#ifndef UTIL_HPP
#define UTIL_HPP

#include <stddef.h>
#include <stdint.h>

class Object;
class Handle;

namespace Util {

template<int Bits>
intptr_t align(intptr_t orig) {
  uintptr_t mask = -1,
            unmasked;
  mask <<= Bits;
  mask = ~mask;
  if ((unmasked = orig & mask)) {
    orig += (1 << Bits) - unmasked;
  }
  return orig;
}

void logObj(const char *wat, Object *x, int fd = 2);
void logPtr(const char *wat, Object *x, int fd = 2);

template<int Bits>
bool isAligned(intptr_t orig) {
  return align<Bits>(orig) == orig;
}

// AssocList and growable array

enum EqFunc {
  kSymbolEq,
  kPtrEq
};

Object *newAssocList();
Object *assocLookup(const Handle &assoc, const Handle &key,
                    EqFunc eqf, bool *ok = NULL);
Object *assocLookupKey(const Handle &assoc, const Handle &key,
                       EqFunc eqf, bool *ok = NULL);
Object *assocInsert(const Handle &assoc, const Handle &key,
                    const Handle &val, EqFunc eqf);
intptr_t assocLength(const Handle &assoc);

Object *newGrowableArray();
void arrayAppend(const Handle &arr, const Handle &item);

// Can use negative index
Object *&arrayAt(const Handle &arr, intptr_t ix);

intptr_t arrayLength(const Handle &arr);

// Trim unused parts
Object *arrayToVector(const Handle &arr);

}

#endif
