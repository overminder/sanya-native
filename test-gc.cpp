#include "gc.hpp"
#include "object.hpp"
#include "util.hpp"
#include "codegen2.hpp"

int main() {
  Handle a = Object::internSymbol("a");
  Handle a2 = Object::internSymbol("a");
  Handle b = Object::internSymbol("b");
  Handle c = Object::internSymbol("c");

  Module mod;

  intptr_t a_i, b_i, c_i;
  a_i = mod.addName(a, a);
  b_i = mod.addName(b, b);
  c_i = mod.addName(c, c);

  printf("%ld %ld %ld\n", a_i, b_i, c_i);

  mod.getRoot()->displayDetail(2);

  return 0;
}

