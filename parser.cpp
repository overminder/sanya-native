#include <sstream>
#include "object.hpp"
#include "parser.hpp"

Object *Parser::parseProg(bool *outOk) {
  bool ok;
  Handle head = Object::newNil(),
         tail = Object::newNil();

  while (true) {
    Handle x = parse(&ok);
    if (!ok) {
      break;
    }

    if (head->isNil()) {
      head = tail = Object::newPair(x, Object::newNil());
    }
    else {
      // XXX: need to separate lhs with rhs to avoid memory corruption
      // since lhs (raw()->cdr()) will be evaluated before rhs (t).
      Handle t = Object::newPair(x, Object::newNil());
      tail->raw()->cdr() = t.getPtr();
      tail = tail->raw()->cdr();
    }
  }
  ok = *outOk = !hasNext();
  return ok ? head.getPtr() : NULL;
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

  Handle head = Object::newNil(),
         tail = Object::newNil();

  while (hasNext()) {
    switch ((c = getNextSkipWS())) {
      case ']': case ')':
        assert(close == c);
        return head.getPtr();

      default:
        putBack();
        {
          Handle curr = parse(&ok);
          assert(ok);

          if (head->isNil()) {
            head = tail = Object::newPair(curr.getPtr(), Object::newNil());
          }
          else {
            Handle t = Object::newPair(curr.getPtr(), Object::newNil());
            tail->raw()->cdr() = t.getPtr();
            tail = tail->raw()->cdr();
          }
        }
    }
  }
  assert(0);
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
      goto done;
    }
  }
done:
  return Object::newFixnum(val);
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

  while (hasNext()) {
    c = getNext();
    if (isspace(c) || c == '[' || c == '(' ||
        c == ']' || c == ')') {
      putBack();
      break;
    }
    xs << c;
  }
  return Object::newSymbolFromC(xs.str().c_str());
}

