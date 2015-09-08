#ifndef BACKEND_H
#define BACKEND_H

#include <memory>
#include "cpu/ir/ir_builder.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {

class RuntimeBlock;

namespace backend {

struct Register {
  const char *name;
  int value_types;
};

class Backend {
 public:
  Backend(emu::Memory &memory) : memory_(memory) {}
  virtual ~Backend() {}

  virtual const Register *registers() const = 0;
  virtual int num_registers() const = 0;

  virtual void Reset() = 0;
  virtual std::unique_ptr<RuntimeBlock> AssembleBlock(
      ir::IRBuilder &builder) = 0;

 protected:
  emu::Memory &memory_;
};
}
}
}

#endif
