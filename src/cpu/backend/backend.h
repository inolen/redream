#ifndef BACKEND_H
#define BACKEND_H

#include <memory>
#include "cpu/ir/ir_builder.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {

struct RuntimeBlock;

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
  virtual bool AssembleBlock(ir::IRBuilder &builder, RuntimeBlock *block) = 0;

 protected:
  emu::Memory &memory_;
};
}
}
}

#endif
