#ifndef BACKEND_H
#define BACKEND_H

#include "jit/ir/ir_builder.h"

namespace re {

namespace sys {
struct Exception;
}

namespace jit {
namespace backend {

struct MemoryInterface {
  void *ctx_base;
  void *mem_base;
  void *mem_self;
  uint8_t (*r8)(void *, uint32_t);
  uint16_t (*r16)(void *, uint32_t);
  uint32_t (*r32)(void *, uint32_t);
  uint64_t (*r64)(void *, uint32_t);
  void (*w8)(void *, uint32_t, uint8_t);
  void (*w16)(void *, uint32_t, uint16_t);
  void (*w32)(void *, uint32_t, uint32_t);
  void (*w64)(void *, uint32_t, uint64_t);
};

struct Register {
  const char *name;
  int value_types;
  const void *data;
};

typedef uint32_t (*CodePointer)();

class Backend {
 public:
  Backend(const MemoryInterface &memif) : memif_(memif) {}
  virtual ~Backend() {}

  virtual const Register *registers() const = 0;
  virtual int num_registers() const = 0;

  virtual void Reset() = 0;

  virtual CodePointer AssembleCode(ir::IRBuilder &builder) = 0;
  virtual void DumpBlock(CodePointer block) = 0;

  virtual bool HandleFastmemException(sys::Exception &ex) = 0;

 protected:
  MemoryInterface memif_;
};
}
}
}

#endif
