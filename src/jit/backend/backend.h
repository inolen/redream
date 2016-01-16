#ifndef BACKEND_H
#define BACKEND_H

#include <map>
#include "hw/memory.h"
#include "jit/ir/ir_builder.h"
#include "jit/source_map.h"

namespace dvm {
namespace sys {
struct Exception;
}

namespace jit {

namespace backend {

struct Register {
  const char *name;
  int value_types;
};

enum BlockFlags {
  // used by exception handlers to let the backend know a block needs to be
  // invalidated
  BF_INVALIDATE = 0x1,
  // compile the block without fast memory access optimizations
  BF_SLOWMEM = 0x2,
};

typedef uint32_t (*BlockPointer)();

class Backend {
 public:
  Backend(hw::Memory &memory) : memory_(memory) {}
  virtual ~Backend() {}

  virtual const Register *registers() const = 0;
  virtual int num_registers() const = 0;

  virtual void Reset() = 0;

  virtual BlockPointer AssembleBlock(ir::IRBuilder &builder,
                                     SourceMap &source_map, void *guest_ctx,
                                     int block_flags) = 0;
  virtual bool HandleException(BlockPointer block, int *block_flags,
                               sys::Exception &ex) = 0;

 protected:
  hw::Memory &memory_;
};
}
}
}

#endif
