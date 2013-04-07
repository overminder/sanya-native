#ifndef RUNTIME_HPP
#define RUNTIME_HPP

#include <stdint.h>

class Object;

class Runtime {
 public:
  static void handleNotAClosure(Object *);
  static void handleArgCountMismatch(Object *, intptr_t);
};

#endif
