#ifndef BACKEND_H
#define BACKEND_H

#include <map>
#include "jit/ir/ir_builder.h"

namespace re {

namespace hw {
class Memory;
}

namespace sys {
struct Exception;
}

namespace jit {

namespace backend {

struct Register {
  const char *name;
  int value_types;
  const void *data;
};

enum BlockFlags {
  // compile the block without fast memory access optimizations
  BF_SLOWMEM = 0x1,
};

typedef uint32_t (*BlockPointer)();

class Backend {
 public:
  Backend(hw::Memory &memory, void *guest_ctx)
      : memory_(memory), guest_ctx_(guest_ctx) {}
  virtual ~Backend() {}

  virtual const Register *registers() const = 0;
  virtual int num_registers() const = 0;

  virtual void Reset() = 0;

  virtual BlockPointer AssembleBlock(ir::IRBuilder &builder,
                                     int block_flags) = 0;
  virtual void DumpBlock(BlockPointer block) = 0;

  virtual bool HandleFastmemException(sys::Exception &ex) = 0;

 protected:
  hw::Memory &memory_;
  void *guest_ctx_;
};
}
}
}

#endif
