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
  static void handleUserError(Object *, ThreadState *);

  // GC
  static void collectAndAlloc(ThreadState *ts);

  // Debug
  static void traceObject(Object *);
  static intptr_t endOfCode(intptr_t);

  // Library
  static void printNewLine(int fd);
};

struct Option {
  static Option &global();
  static void init();

  bool kTailCallOpt;
  bool kInitialized;
};

#endif
