#ifndef BACKEND_H
#define BACKEND_H

#include "jit/ir/ir_builder.h"

struct address_space_s;
struct re_exception_s;

namespace re {
namespace jit {
namespace backend {

struct MemoryInterface {
  void *ctx_base;
  void *mem_base;
  struct address_space_s *mem_self;
  uint8_t (*r8)(struct address_space_s *, uint32_t);
  uint16_t (*r16)(struct address_space_s *, uint32_t);
  uint32_t (*r32)(struct address_space_s *, uint32_t);
  uint64_t (*r64)(struct address_space_s *, uint32_t);
  void (*w8)(struct address_space_s *, uint32_t, uint8_t);
  void (*w16)(struct address_space_s *, uint32_t, uint16_t);
  void (*w32)(struct address_space_s *, uint32_t, uint32_t);
  void (*w64)(struct address_space_s *, uint32_t, uint64_t);
};

struct Register {
  const char *name;
  int value_types;
  const void *data;
};

class Backend {
 public:
  Backend(const MemoryInterface &memif) : memif_(memif) {}
  virtual ~Backend() {}

  virtual const Register *registers() const = 0;
  virtual int num_registers() const = 0;

  virtual void Reset() = 0;

  virtual const uint8_t *AssembleCode(ir::IRBuilder &builder, int *size) = 0;
  virtual void DumpCode(const uint8_t *host_addr, int size) = 0;

  virtual bool HandleFastmemException(struct re_exception_s *ex) = 0;

 protected:
  MemoryInterface memif_;
};
}
}
}

#endif
