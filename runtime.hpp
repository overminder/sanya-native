#ifndef RUNTIME_HPP
#define RUNTIME_HPP

#include <stdint.h>

class Object;
class ThreadState;

class Runtime {
 public:
  // Error handlers
  static void handleNotAClosure(Object *);
  static void handleArgCountMismatch(Object *, intptr_t);

  // GC
  static void collectAndAlloc(size_t size, intptr_t stackPtr,
                              void *frameDescr, ThreadState *ts);

  // Debug
  static void traceObject(Object *);

  // Library
  static void printNewLine(int fd);
};

#endif
