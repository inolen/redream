#include <iomanip>
#include <sstream>
#include <beaengine/BeaEngine.h>
#include <xbyak/xbyak.h>
#include "core/core.h"
#include "emu/profiler.h"
#include "jit/backend/x64/x64_backend.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend;
using namespace dreavm::jit::backend::x64;
using namespace dreavm::jit::ir;

namespace dreavm {
namespace jit {
namespace backend {
namespace x64 {
const Register x64_registers[] = {{"rbx", ir::VALUE_INT_MASK},
                                  {"rbp", ir::VALUE_INT_MASK},
                                  {"r12", ir::VALUE_INT_MASK},
                                  {"r13", ir::VALUE_INT_MASK},
                                  {"r14", ir::VALUE_INT_MASK},
                                  {"r15", ir::VALUE_INT_MASK},
                                  {"xmm6", ir::VALUE_FLOAT_MASK},
                                  {"xmm7", ir::VALUE_FLOAT_MASK},
                                  {"xmm8", ir::VALUE_FLOAT_MASK},
                                  {"xmm9", ir::VALUE_FLOAT_MASK},
                                  {"xmm10", ir::VALUE_FLOAT_MASK},
                                  {"xmm11", ir::VALUE_FLOAT_MASK}};

const int x64_num_registers = sizeof(x64_registers) / sizeof(Register);
}
}
}
}

X64Backend::X64Backend(Memory &memory)
    : Backend(memory), emitter_(memory, 1024 * 1024 * 8) {}

X64Backend::~X64Backend() {}

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

void X64Backend::Reset() { emitter_.Reset(); }

RuntimeBlock *X64Backend::AssembleBlock(ir::IRBuilder &builder) {
  // allocate block structure at start of code buffer, making for nice data
  // locality
  RuntimeBlock *block = emitter_.getCurr<RuntimeBlock *>();
  try {
    emitter_.setSize(emitter_.getSize() + sizeof(RuntimeBlock));
  } catch (const Xbyak::Error &) {
    return nullptr;
  }

  // try to generate the x64 code. if the code buffer overflows let the backend
  // know so it can reset the cache and try again
  X64Fn fn;
  try {
    fn = emitter_.Emit(builder);
  } catch (const Xbyak::Error &e) {
    if (e == Xbyak::ERR_CODE_IS_TOO_BIG) {
      return nullptr;
    }
    LOG_FATAL("X64 codegen failure, %s", e.what());
  }

  // initialize block structure
  new (block) RuntimeBlock(reinterpret_cast<RuntimeBlockCall>(fn),
                           builder.guest_cycles());

  return block;
}

void X64Backend::DumpBlock(RuntimeBlock *block) {
  DISASM dsm;
  memset(&dsm, 0, sizeof(dsm));
  dsm.Archi = 64;
  dsm.EIP = (uintptr_t)block->call;
  dsm.SecurityBlock = 0;
  dsm.Options = NasmSyntax | PrefixedNumeral;

  while (true) {
    int len = Disasm(&dsm);
    if (len == OUT_OF_BLOCK) {
      LOG_INFO("Disasm engine is not allowed to read more memory");
      break;
    } else if (len == UNKNOWN_OPCODE) {
      LOG_INFO("Unknown opcode");
      break;
    }

    // format instruction binary
    static const int MAX_INSTR_LENGTH = 15;
    std::stringstream instr;
    for (int i = 0; i < MAX_INSTR_LENGTH; i++) {
      uint32_t v =
          i < len ? (uint32_t) * reinterpret_cast<uint8_t *>(dsm.EIP + i) : 0;
      instr << std::hex << std::setw(2) << std::setfill('0') << v;
    }

    // print out binary / mnemonic
    LOG_INFO("%s %s", instr.str().c_str(), dsm.CompleteInstr);

    if (dsm.Instruction.BranchType == RetType) {
      break;
    }

    dsm.EIP = dsm.EIP + len;
  }
}

void X64Backend::FreeBlock(RuntimeBlock *block) {
  // delete block;
}

bool X64Backend::HandleAccessFault(uintptr_t rip, uintptr_t fault_addr) {
  size_t original_size = emitter_.getSize();
  size_t offset = rip - reinterpret_cast<uint64_t>(emitter_.getCode());
  emitter_.setSize(offset);

  // nop out the mov
  uint8_t *ptr = reinterpret_cast<uint8_t *>(rip);
  while (*ptr != 0xeb) {
    emitter_.nop();
    ptr++;
  }

  // nop out the near jmp
  emitter_.nop();
  emitter_.nop();

  emitter_.setSize(original_size);

  return true;
}
