#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "object.hpp"
#include "parser.hpp"
#include "codegen.hpp"
#include "runtime.hpp"

typedef Object *(SchemeFn_0)(Object *);
typedef Object *(SchemeFn_1)(Object *, Object *);

Object *callScheme_0(Object *clo) {
  assert(clo->isClosure());
  auto info = clo->raw()->cloInfo();
  assert(info->funcArity() == 0);
  auto entry = info->funcCodeAs<SchemeFn_0 *>();
  return entry(clo);
}

Object *callScheme_1(Object *clo, Object *arg1) {
  assert(clo->isClosure());
  auto info = clo->raw()->cloInfo();
  assert(info->funcArity() == 1);
  auto entry = info->funcCodeAs<SchemeFn_1 *>();
  return entry(clo, arg1);
}

void readAll(FILE *f, std::string *xs) {
  int c;
  while ((c = fgetc(f)) != EOF) {
    char realC = c;
    (*xs) += realC;
  }
}

int main(int argc, char **argv) {
  FILE *fin;
  CGModule cg;
  std::string input;

  if (argc == 2) {
    fin = fopen(argv[1], "r");
    if (!fin) {
      perror(argv[1]);
      return 1;
    }
  }
  else {
    fin = stdin;
  }
  readAll(fin, &input);

  Parser parser(input);

  bool parseOk;
  Object *ast = parser.parseProg(&parseOk);
  assert(parseOk);

  //ast->displayDetail(2);

  Object *mainClo = cg.genModule(ast);

  callScheme_0(mainClo)->displayDetail(1);
  Runtime::printNewLine(1);

  return 0;
}

