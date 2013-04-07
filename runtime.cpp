#include <stdio.h>
#include <stdlib.h>

#include "runtime.hpp"
#include "object.hpp"

void Runtime::handleNotAClosure(Object *wat) {
  dprintf(2, "Not a closure: ");
  wat->displayDetail(2);
  dprintf(2, "\n");
  exit(1);
}

void Runtime::handleArgCountMismatch(Object *wat, intptr_t argc) {
  dprintf(2, "Argument count mismatch: ");
  wat->displayDetail(2);
  dprintf(2, " need %ld, but got %ld\n",
          wat->raw()->cloInfo()->funcArity(), argc);
  exit(1);
}

