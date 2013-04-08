#ifndef PARSER_HPP
#define PARSER_HPP

#include <assert.h>
#include <ctype.h>
#include <vector>
#include <string>

class Object;

class Parser {
 public:
  Parser(const std::string &input)
    : input(input)
    , ix(0) { }
  
  Object *parseProg(bool *);
  Object *parse(bool *);
  Object *parseList(char);
  Object *parseFixnum(char);
  Object *parseAtom(char);

  void putBack() {
    --ix;
  }

  bool hasNext() {
    return ix < (intptr_t) input.length();
  }

  char getNext() {
    assert(hasNext());
    return input[ix++];
  }

  char getNextSkipWS(bool *ok = NULL) {
    while (hasNext()) {
      char c = input[ix++];
      if (isspace(c)) {
        continue;
      }

      if (ok) *ok = true;
      return c;
    }
    if (ok) *ok = false;
    return 0;
  }

 private:
  const std::string &input;
  intptr_t ix;
};

#endif
