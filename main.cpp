#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "util.hpp"
#include "object.hpp"
#include "parser.hpp"
#include "codegen2.hpp"
#include "runtime.hpp"

extern "C" {
  extern Object *Scheme_asmEntry(
      Object *, void *, intptr_t, intptr_t, ThreadState *);
}

Object *callScheme_0(Object *clo) {
  assert(clo->isClosure());
  auto info = clo->raw()->cloInfo();
  assert(info->funcArity() == 0);
  auto entry = info->funcCodeAs<void *>();
  ThreadState *ts = &ThreadState::global();
  Util::logObj("CallScheme", clo);
  return Scheme_asmEntry(clo, entry, ts->heapPtr(), ts->heapLimit(), ts);
}

void readAll(FILE *f, std::string *xs) {
  int c;
  while ((c = fgetc(f)) != EOF) {
    char realC = c;
    (*xs) += realC;
  }
}

Object *getMainClo(int argc, char **argv) {
  FILE *fin;
  CGModule cg;
  Handle ast;

  {
    std::string input;

    if (argc == 2) {
      fin = fopen(argv[1], "r");
      if (!fin) {
        perror(argv[1]);
        exit(1);
      }
    }
    else {
      fin = stdin;
    }
    readAll(fin, &input);
    if (fin != stdin) {
      fclose(fin);
    }

    Parser parser(input);

    bool parseOk;
    ast = parser.parseProg(&parseOk);
    assert(parseOk);
  }

  //ast->displayDetail(2);

  Object *mainClo = cg.genModule(ast);
  return mainClo;
  //ThreadState::global().display(2);

  //callScheme_0(mainClo)->displayDetail(1);
  //Runtime::printNewLine(1);
}

int main(int argc, char **argv) {
  //ThreadState::global().display(2);
  callScheme_0(getMainClo(argc, argv));
  ThreadState::global().destroy();
  return 0;
}

