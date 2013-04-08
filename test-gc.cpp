#include "gc.hpp"
#include "object.hpp"

int main() {
  Handle a = Object::newSymbolFromC("a");
  Handle b = Object::newSymbolFromC("b");
  Handle c = Object::newSymbolFromC("c");

  Handle ab = Object::newPair(a, b);
  Handle cab = Object::newPair(c, ab);

  cab->displayDetail(2);

  return 0;
}

