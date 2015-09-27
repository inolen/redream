#ifndef BACKEND_H
#define BACKEND_H

#include <memory>
#include "hw/memory.h"
#include "jit/ir/ir_builder.h"

namespace dreavm {
namespace jit {

class RuntimeBlock;

namespace backend {

struct Register {
  const char *name;
  int value_types;
};

class Backend {
 public:
  Backend(hw::Memory &memory) : memory_(memory) {}
  virtual ~Backend() {}

  virtual const Register *registers() const = 0;
  virtual int num_registers() const = 0;

  virtual void Reset() = 0;

  virtual RuntimeBlock *AssembleBlock(ir::IRBuilder &builder) = 0;
  virtual void FreeBlock(RuntimeBlock *block) = 0;

 protected:
  hw::Memory &memory_;
};
}
}
}

#endif
