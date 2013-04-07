#ifndef RUNTIME_HPP
#define RUNTIME_HPP

#include <stdint.h>

class Object;

class Runtime {
 public:
  // Error handlers
  static void handleNotAClosure(Object *);
  static void handleArgCountMismatch(Object *, intptr_t);

  // Debug
  static void traceObject(Object *);

  // Library
  static void printNewLine(int fd);
};

#endif
