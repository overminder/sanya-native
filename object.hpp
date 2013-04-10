#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "util.hpp"
#include "gc.hpp"

class Object;
class ThreadState;

template <typename This>
class Base {
 public:
  template<typename T>
  constexpr T as() { return reinterpret_cast<T>(this); }

  template<typename T>
  static constexpr This *from(T wat) {
    return reinterpret_cast<This *>(wat);
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
    kVectorTag                  = 0x6,
    kForeignPtrTag              = 0x7
  };

  enum {
    kTagShift                   = 0x4,
    kTagMask                    = 0xf,

    kSizeOfPair                 = 0x10,
    kCarOffset                  = 0x0,
    kCdrOffset                  = 0x8,

    kNilUpper                   = 0x0,
    kTrueUpper                  = 0x1,
    kFalseUpper                 = 0x2,
    kVoidUpper                  = 0x3,

    kFuncArityOffset            = 0x0,
    kFuncNameOffset             = 0x8,
    kFuncConstOffsetOffset      = 0x10,
    kFuncNumPayloadOffset       = 0x18,
    kFuncSizeOffset             = 0x1c, // including meta data
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

  Object *tagWith(Tag wat) {
    return reinterpret_cast<Object *>(as<intptr_t>() + wat);
  }

#define MK_TAG_AS(name) \
  Object *tagAs ## name() { return tag<k ## name ## Tag>();  }

#define TAG_LIST(V) \
  V(Pair) V(Symbol) V(Fixnum) V(Singleton) \
  V(Closure) V(Vector) V(ForeignPtr)
TAG_LIST(MK_TAG_AS)
#undef MK_TAG_AS

#define MK_ATTR(name, offset, type) \
  type &name() { return at<offset ## Offset, type>(); }

#define ATTR_LIST(V)                                              \
  V(car,             kCar,             Object *)                  \
  V(cdr,             kCdr,             Object *)                  \
  V(funcArity,       kFuncArity,       intptr_t)                  \
  V(funcName,        kFuncName,        Object *)                  \
  V(funcConstOffset, kFuncConstOffset, Object *)                  \
  V(funcCode,        kFuncCode,        char)                      \
  V(funcNumPayload,  kFuncNumPayload,  int32_t)                   \
  V(funcSize,        kFuncSize,        int32_t)                   \
  V(vectorSize,      kVectorSize,      intptr_t)                  \
  V(vectorElem,      kVectorElem,      Object *)                  \
  V(cloInfo,         kCloInfo,         RawObject *)               \
  V(cloPayload_,     kCloPayload,      Object *)                  \
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
  static Object *newPair(const Handle &car, const Handle &cdr) {
    RawObject *pair = alloc<RawObject>(RawObject::kSizeOfPair);
    pair->car() = car.getPtr();
    pair->cdr() = cdr.getPtr();
    //dprintf(2, "[Object::newPair] %p\n", pair);
    return pair->tagAsPair();
  }

  static Object *newFixnum(intptr_t val) {
    RawObject *raw = RawObject::from(val << RawObject::kTagShift);
    return raw->tagAsFixnum();
  }

  static Object *internSymbol(const char *src) {
    Handle tmp = newSymbolFromC(src);
    bool ok;
    Object *got = Util::assocLookupKey(
        ThreadState::global().symbolInternTable(),
        tmp, Util::kSymbolEq, &ok);
    if (ok) {
      return got;
    }
    else {
      //ThreadState::global().symbolInternTable() = 

      Object *newInternTable =
        Util::assocInsert(ThreadState::global().symbolInternTable(),
            tmp, Object::newNil(), Util::kPtrEq);
      ThreadState::global().symbolInternTable() = newInternTable;
      return tmp.getPtr();
    }
  }

  static Object *newSymbolFromC(const char *src) {
    RawObject *raw;
    size_t len = strlen(src);
    raw = alloc<RawObject>(Util::align<RawObject::kTagShift>(len + 1));
    memcpy(raw, src, len + 1);
    return raw->tagAsSymbol();
  }

#define SINGLETONS(V) \
  V(Nil) V(True) V(False) V(Void)
#define MK_SINGLETON(name) \
  static Object *new ## name() { \
    return RawObject::from(RawObject::k ## name ## Upper << \
                           RawObject::kTagShift)->tagAsSingleton(); \
  }
SINGLETONS(MK_SINGLETON)
#undef MK_SINGLETON

#define CHECK_SINGLETON(name) \
  bool is ## name() { return this == new ## name(); }
SINGLETONS(CHECK_SINGLETON)
#undef SINGLETONS

  static Object *newBool(bool wat) {
    return wat ? newTrue() : newFalse();
  }

  static RawObject *newFunction(void *raw, intptr_t arity,
                                Object *name, Object *constOffsets,
                                intptr_t numPayload) {
    // keep in sync with the codegen.
    RawObject *func = RawObject::from(raw);
    func->funcArity() = arity;
    func->funcName() = name;
    func->funcConstOffset() = constOffsets;
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

    size = Util::align<4>(size);
   
    RawObject *clo = alloc<RawObject>(size);
    clo->cloInfo() = info;
    return clo->tagAsClosure();
  }

  static Object *newVector(intptr_t size, Object *fill) {
    size_t actualSize = Util::align<4>(sizeof(Object *) * (1 + size));
    RawObject *vector = alloc<RawObject>(actualSize);
    vector->vectorSize() = size;
    for (intptr_t i = 0; i < size; ++i) {
      vector->vectorAt(i) = fill;
    }
    return vector->tagAsVector();
  }

  // Not allocated from the scheme heap
  template <typename T>
  static Object *newForeignPtr(T *wat) {
    return RawObject::from(wat)->tagAsForeignPtr();
  }

#define CHECK_TAG(name) \
  bool is ## name() { return getTag() == RawObject::k ## name ## Tag; }
  TAG_LIST(CHECK_TAG)
#undef CHECK_TAG

  // Does not check for proper list.
  bool isList() {
    return isPair() || isNil();
  }

  template <typename T>
  static T *alloc(size_t size) {
    return reinterpret_cast<T *>(allocRaw(size));
  }

  bool isHeapAllocated() {
    switch (getTag()) {
    case RawObject::kPairTag:
    case RawObject::kSymbolTag:
      return true;

    case RawObject::kSingletonTag:
    case RawObject::kFixnumTag:
      return false;

    case RawObject::kClosureTag:
    case RawObject::kVectorTag:
      return true;

    default:
      Util::logPtr("Object::isHeapAllocated: not a tagged object", this);
      assert(0);
    }
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
    return ThreadState::global().gcAlloc(size);
  }

  // Library functions
  void printToFd(int fd);
  static void printNewLine(int fd);
  void displayDetail(int fd);
  void displayListDetail(int fd);

  // Gc support
  void gcScavenge(ThreadState *);
};

#endif
