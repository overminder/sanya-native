#include <sstream>
#include "object.hpp"
#include "parser.hpp"

static Object *vectorToScheme(const std::vector<Object *> &vec) {
  Object *xs = Object::newNil();
  for (int i = vec.size() - 1; i >= 0; --i) {
    xs = Object::newPair(vec[i], xs);
  }
  return xs;
}

Object *Parser::parse(bool *ok) {
  assert(hasNext());
  char c;

  while (true) {
    switch ((c = getNextSkipWS(ok))) {
    case '(': case '[':
      return parseList(c);

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return parseFixnum(c);

    default:
      if (!*ok) {
        return NULL;
      }
      return parseAtom(c);
    }
  }
}

Object *Parser::parseList(char open) {
  char c;
  char close = open == '(' ? ')' : ']';
  bool ok;
  std::vector<Object *> xs;

  while (true) {
    switch ((c = getNextSkipWS())) {
      case ']': case ')':
        assert(close == c);
        return vectorToScheme(xs);

      default:
        putBack();
        xs.push_back(parse(&ok));
        assert(ok);
    }
  }
}

Object *Parser::parseFixnum(char open) {
  intptr_t val;
  std::stringstream xs;
  xs << open;

  while (hasNext()) {
    char c = getNext();
    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      xs << c;
      continue;

    default:
      putBack();
      xs >> val;
      return Object::newFixnum(val);
    }
  }
}

Object *Parser::parseAtom(char open) {
  char c;
  std::stringstream xs;
  switch (open) {
  case '#':
    c = getNext();
    assert(!isspace(c));
    if (c == 't') {
      return Object::newTrue();
    }
    else if (c == 'f') {
      return Object::newFalse();
    }
    putBack();
  }

  xs << open;

  while (hasNext() && !isspace(c = getNext())) {
    xs << c;
  }
  return Object::newSymbolFromC(xs.str().c_str());
}

