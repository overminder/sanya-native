#ifndef UTIL_HPP
#define UTIL_HPP

#include <stdint.h>

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

template<int Bits>
bool isAligned(intptr_t orig) {
  return align<Bits>(orig) == orig;
}

}

#endif
