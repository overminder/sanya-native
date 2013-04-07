#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

class Object;

template <typename This>
class Base {
 public:
  template<typename T>
  constexpr T as() { return reinterpret_cast<T>(this); }

  template<typename T>
  static constexpr This *from(T wat) { return reinterpret_cast<This *>(wat); }

  template<int Bits>
  static intptr_t align(intptr_t orig) {
    uintptr_t mask = -1,
              unmasked;
    mask <<= Bits;
    mask = ~mask;
    if ((unmasked = orig & mask)) {
      orig += (1 << Bits) - unmasked;
    }
    return orig;
  }
};

// Untagged
class RawObject : public Base<RawObject> {
 public:
  enum Tag {
    kPairTag                    = 0x1,
    kSymbolTag                  = 0x2,
    kSingletonTag               = 0x3,
    kFixnumTag                  = 0x4,
    kClosureTag                 = 0x5,
    kVectorTag                  = 0x6
  };

  enum {
    kTagShift                   = 0x4,

    kCarOffset                  = 0x0,
    kCdrOffset                  = 0x8,

    kNilUpper                   = 0x0,
    kTrueUpper                  = 0x1,
    kFalseUpper                 = 0x2,

    kFuncArityOffset            = 0x0,
    kFuncNameOffset             = 0x8,
    kFuncRelocOffset            = 0x10,
    kFuncNumPayloadOffset       = 0x18,
    kFuncCodeOffset             = 0x20, // variable-sized

    kCloInfoOffset              = 0x0,
    kCloPayloadOffset           = 0x8,  // variable-sized

    kVectorSizeOffset           = 0x0,
    kVectorElemOffset           = 0x8   // variable-sized
  };

  template<int offset, typename T> 
  constexpr T &at() {
    return *reinterpret_cast<T *>(as<intptr_t>() + offset);
  }

  template<typename T> 
  constexpr T &at(intptr_t offset) {
    return *reinterpret_cast<T *>(as<intptr_t>() + offset);
  }

  template<uint8_t tagVal>
  constexpr Object *tag() {
    return reinterpret_cast<Object *>(as<intptr_t>() + tagVal);
  }

#define MK_TAG_AS(name) \
  Object *tagAs ## name() { return tag<k ## name ## Tag>();  }

#define TAG_LIST(V) \
  V(Pair) V(Symbol) V(Fixnum) V(Singleton) V(Closure) V(Vector)
TAG_LIST(MK_TAG_AS)
#undef MK_TAG_AS

#define MK_ATTR(name, offset, type) \
  type &name() { return at<offset ## Offset, type>(); }

#define ATTR_LIST(V)                                     \
  V(car,            kCar,            Object *)                  \
  V(cdr,            kCdr,            Object *)                  \
  V(funcArity,      kFuncArity,      intptr_t)                  \
  V(funcName,       kFuncName,       Object *)                  \
  V(funcReloc,      kFuncReloc,      Object *)                  \
  V(funcCode,       kFuncCode,       char)                      \
  V(funcNumPayload, kFuncNumPayload, intptr_t)                  \
  V(vectorSize,     kVectorSize,     intptr_t)                  \
  V(vectorElem,     kVectorElem,     Object *)                  \
  V(cloInfo,        kCloInfo,        RawObject *)               \
  V(cloPayload_,    kCloPayload,     Object *)                  \
  // Append

  ATTR_LIST(MK_ATTR);

  Object *&vectorAt(intptr_t i) {
    return (&vectorElem())[i];
  }

  template <typename T>
  T funcCodeAs() {
    return reinterpret_cast<T>(&funcCode());
  }

  Object **cloPayload() {
    return &cloPayload_();
  }

#undef ATTR_LIST
#undef MK_ATTR

  typedef void (*NullaryFn) ();
};

// Tagged
class Object : public Base<Object> {
 public:
  static Object *newPair(Object *car, Object *cdr) {
    RawObject *pair = alloc<RawObject>(16);
    pair->car() = car;
    pair->cdr() = cdr;
    return pair->tagAsPair();
  }

  static Object *newFixnum(intptr_t val) {
    RawObject *raw = RawObject::from(val << RawObject::kTagShift);
    return raw->tagAsFixnum();
  }

  static Object *newSymbolFromC(const char *src) {
    RawObject *raw;
    size_t len = strlen(src);
    raw = alloc<RawObject>(align<RawObject::kTagShift>(len + 1));
    memcpy(raw, src, len + 1);
    return raw->tagAsSymbol();
  }

#define SINGLETONS(V) \
  V(Nil) V(True) V(False)
#define MK_SINGLETON(name) \
  static Object *new ## name() { \
    return RawObject::from(RawObject::k ## name ## Upper << \
                           RawObject::kTagShift)->tagAsSingleton(); \
  }
SINGLETONS(MK_SINGLETON)
#undef MK_SINGLETON
#undef SINGLETONS

  static RawObject *newFunction(void *raw, intptr_t arity,
                                Object *name, Object *reloc,
                                intptr_t numPayload) {
    // keep in sync with the codegen.
    RawObject *func = RawObject::from(raw);
    func->funcArity() = arity;
    func->funcName() = name;
    func->funcReloc() = reloc;
    func->funcNumPayload() = numPayload;
    return func;
  }

  static Object *newClosure(RawObject *info) {
    size_t size;

    if (info) {
      size = sizeof(Object *) * (1 + info->funcNumPayload());
    }
    else {
      // No info, should be a supercombinator
      size = sizeof(Object *);
    }
   
    RawObject *clo = alloc<RawObject>(size);
    clo->cloInfo() = info;
    return clo->tagAsClosure();
  }

  static Object *newVector(intptr_t size, Object *fill) {
    RawObject *vector = alloc<RawObject>(sizeof(Object *) * (1 + size));
    vector->vectorSize() = size;
    for (intptr_t i = 0; i < size; ++i) {
      vector->vectorAt(i) = fill;
    }
    return vector->tagAsVector();
  }

#define CHECK_TAG(name) \
  bool is ## name() { return getTag() == RawObject::k ## name ## Tag; }
  TAG_LIST(CHECK_TAG)
#undef CHECK_TAG

  template <typename T>
  static T *alloc(size_t size) {
    return reinterpret_cast<T *>(allocRaw(size));
  }

  RawObject::Tag getTag() {
    return (RawObject::Tag) (as<intptr_t>() & 0xf);
  }

  template <typename T>
  T *unTag() {
    return reinterpret_cast<T *>(as<intptr_t>() & ~0xfUL);
  }

  RawObject *raw() {
    return unTag<RawObject>();
  }

  const char *rawSymbol() {
    return unTag<char>();
  }

  intptr_t fromFixnum() {
    return as<intptr_t>() >> RawObject::kTagShift;
  }

  static void *allocRaw(size_t size) {
    // XXX gc here
    return malloc(size);
  }

  // Library functions
  void printToFd(int fd);
  static void printNewLine(int fd);
  void displayDetail(int fd);
  void displayListDetail(int fd);
};

#endif
