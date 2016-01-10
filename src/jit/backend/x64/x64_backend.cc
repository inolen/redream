#include <iomanip>
#include <sstream>
#include <beaengine/BeaEngine.h>
#include <xbyak/xbyak.h>
#include "core/core.h"
#include "emu/profiler.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_disassembler.h"
#include "sys/exception_handler.h"

using namespace dvm;
using namespace dvm::hw;
using namespace dvm::jit;
using namespace dvm::jit::backend;
using namespace dvm::jit::backend::x64;
using namespace dvm::jit::ir;
using namespace dvm::sys;

namespace dvm {
namespace jit {
namespace backend {
namespace x64 {
const Register x64_registers[] = {
    {"rbx", ir::VALUE_INT_MASK},     {"rbp", ir::VALUE_INT_MASK},
    {"r12", ir::VALUE_INT_MASK},     {"r13", ir::VALUE_INT_MASK},
    {"r14", ir::VALUE_INT_MASK},     {"r15", ir::VALUE_INT_MASK},
    {"xmm6", ir::VALUE_FLOAT_MASK},  {"xmm7", ir::VALUE_FLOAT_MASK},
    {"xmm8", ir::VALUE_FLOAT_MASK},  {"xmm9", ir::VALUE_FLOAT_MASK},
    {"xmm10", ir::VALUE_FLOAT_MASK}, {"xmm11", ir::VALUE_FLOAT_MASK}};

const int x64_num_registers = sizeof(x64_registers) / sizeof(Register);
}
}
}
}

X64Backend::X64Backend(Memory &memory)
    : Backend(memory), emitter_(1024 * 1024 * 8) {}

X64Backend::~X64Backend() {}

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

void X64Backend::Reset() { emitter_.Reset(); }

BlockPointer X64Backend::AssembleBlock(ir::IRBuilder &builder,
                                       SourceMap &source_map, void *guest_ctx,
                                       int block_flags) {
  // try to generate the x64 code. if the code buffer overflows let the backend
  // know so it can reset the cache and try again
  BlockPointer fn;
  try {
    fn = emitter_.Emit(builder, source_map, memory_, guest_ctx, block_flags);
  } catch (const Xbyak::Error &e) {
    if (e == Xbyak::ERR_CODE_IS_TOO_BIG) {
      return nullptr;
    }
    LOG_FATAL("X64 codegen failure, %s", e.what());
  }

  return fn;
}

void X64Backend::DumpBlock(BlockPointer block) {
  DISASM dsm;
  memset(&dsm, 0, sizeof(dsm));
  dsm.Archi = 64;
  dsm.EIP = (uintptr_t)block;
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

// the memory thunks exist as a mechanism to service dynamic memory handlers
// AFTER exiting the exception handler. invoking the handlers from inside the
// exception handler imposes many restrictions:
// https://www.securecoding.cert.org/confluence/display/c/SIG30-C.+Call+only+
// asynchronous-safe+functions+within+signal+handlers
//
// instead, the handler preps the stack and registers for the handler, sets rip
// to the thunk address, and once the execption handler exits the thunk will be
// invoked, the handler will be called, and the stack will be cleaned back up

// clang-format off
static void (*memory_load_thunks[16])();
static void (*memory_store_thunks[16])();

#define STRINGIFY(x)            #x
#define EXPAND_AND_STRINGIFY(x) STRINGIFY(x)

#define DECL_MEMORY_THUNK(reg)                                               \
  static __attribute__ ((naked)) void memory_load_thunk_##reg() {            \
    asm("call *%rax\n\t"                                                     \
        "mov %rax, %" #reg "\n\t"                                            \
        "pop %" EXPAND_AND_STRINGIFY(INT_ARG1) "\n\t"                        \
        "pop %" EXPAND_AND_STRINGIFY(INT_ARG0) "\n\t"                        \
        "ret\n\t");                                                          \
  }                                                                          \
  static __attribute__ ((naked)) void memory_store_thunk_##reg() {           \
    asm("call *%rax\n\t"                                                     \
        "pop %" EXPAND_AND_STRINGIFY(INT_ARG2) "\n\t"                        \
        "pop %" EXPAND_AND_STRINGIFY(INT_ARG1) "\n\t"                        \
        "pop %" EXPAND_AND_STRINGIFY(INT_ARG0) "\n\t"                        \
        "ret\n\t");                                                          \
  }                                                                          \
  static struct _memory_thunk_##reg##_init {                                 \
    _memory_thunk_##reg##_init() {                                           \
      memory_load_thunks[Xbyak::Operand::reg] = &memory_load_thunk_##reg;    \
      memory_store_thunks[Xbyak::Operand::reg] = &memory_store_thunk_##reg;  \
    }                                                                        \
  } memory_thunk_##reg##_init
// clang-format on

DECL_MEMORY_THUNK(RAX);
DECL_MEMORY_THUNK(RCX);
DECL_MEMORY_THUNK(RDX);
DECL_MEMORY_THUNK(RBX);
DECL_MEMORY_THUNK(RSP);
DECL_MEMORY_THUNK(RBP);
DECL_MEMORY_THUNK(RSI);
DECL_MEMORY_THUNK(RDI);
DECL_MEMORY_THUNK(R8);
DECL_MEMORY_THUNK(R9);
DECL_MEMORY_THUNK(R10);
DECL_MEMORY_THUNK(R11);
DECL_MEMORY_THUNK(R12);
DECL_MEMORY_THUNK(R13);
DECL_MEMORY_THUNK(R14);
DECL_MEMORY_THUNK(R15);

bool X64Backend::HandleException(BlockPointer block, int *block_flags,
                                 Exception &ex) {
  const uint8_t *data = reinterpret_cast<const uint8_t *>(ex.thread_state.rip);

  // it's assumed a mov has triggered the exception
  X64Mov mov;
  if (!X64Disassembler::DecodeMov(data, &mov)) {
    return false;
  }

  // figure out the guest address that was being accessed
  const uint8_t *fault_addr = reinterpret_cast<const uint8_t *>(ex.fault_addr);
  const uint8_t *protected_start = memory_.protected_base();
  uint32_t guest_addr = static_cast<uint32_t>(fault_addr - protected_start);

  // instead of handling the dynamic callback from inside of the exception
  // handler, force rip to the beginning of a thunk which will invoke the
  // callback once the exception handler has exited. this frees the callbacks
  // from any restrictions imposed by an exception handler, and also prevents
  // a possible recursive exceptions
  if (mov.is_load) {
    // push the original argument registers and the return address (the next
    // instruction after the current mov) to the stack
    ex.thread_state.rsp -= 24;
    dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp),
               ex.thread_state.r[Xbyak::Operand::INT_ARG1]);
    dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp + 8),
               ex.thread_state.r[Xbyak::Operand::INT_ARG0]);
    dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp + 16),
               ex.thread_state.rip + mov.length);

    // prep argument registers (memory object, guest_addr) for read function
    ex.thread_state.r[Xbyak::Operand::INT_ARG0] =
        reinterpret_cast<uint64_t>(&memory_);
    ex.thread_state.r[Xbyak::Operand::INT_ARG1] =
        static_cast<uint64_t>(guest_addr);

    // prep function call address for thunk
    switch (mov.operand_size) {
      case 1:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<uint8_t (*)(Memory *, uint32_t)>(&Memory::R8));
        break;
      case 2:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<uint16_t (*)(Memory *, uint32_t)>(&Memory::R16));
        break;
      case 4:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<uint32_t (*)(Memory *, uint32_t)>(&Memory::R32));
        break;
      case 8:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<uint64_t (*)(Memory *, uint32_t)>(&Memory::R64));
        break;
    }

    // resume execution in the thunk once the exception handler exits
    ex.thread_state.rip =
        reinterpret_cast<uint64_t>(memory_load_thunks[mov.reg]);
  } else {
    ex.thread_state.rsp -= 32;
    dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp),
               ex.thread_state.r[Xbyak::Operand::INT_ARG2]);
    dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp + 8),
               ex.thread_state.r[Xbyak::Operand::INT_ARG1]);
    dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp + 16),
               ex.thread_state.r[Xbyak::Operand::INT_ARG0]);
    dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp + 24),
               ex.thread_state.rip + mov.length);

    // prep argument registers (memory object, guest_addr, value) for write
    // function
    ex.thread_state.r[Xbyak::Operand::INT_ARG0] =
        reinterpret_cast<uint64_t>(&memory_);
    ex.thread_state.r[Xbyak::Operand::INT_ARG1] =
        static_cast<uint64_t>(guest_addr);
    ex.thread_state.r[Xbyak::Operand::INT_ARG2] =
        *(&ex.thread_state.r[mov.reg]);

    // prep function call address for thunk
    switch (mov.operand_size) {
      case 1:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<void (*)(Memory *, uint32_t, uint8_t)>(&Memory::W8));
        break;
      case 2:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<void (*)(Memory *, uint32_t, uint16_t)>(&Memory::W16));
        break;
      case 4:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<void (*)(Memory *, uint32_t, uint32_t)>(&Memory::W32));
        break;
      case 8:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(
            static_cast<void (*)(Memory *, uint32_t, uint64_t)>(&Memory::W64));
        break;
    }

    // resume execution in the thunk once the exception handler exits
    ex.thread_state.rip =
        reinterpret_cast<uint64_t>(memory_store_thunks[mov.reg]);
  }

  // tell the cache to invalidate this block, appending the slowmem flag for
  // the next compile. the slowmem flag tells the backend to handle all load
  // and store operations with the slower Memory read and write functions (as
  // opposed to a `mov reg, [mmap_base + guest_addr]` instruction) to avoid
  // triggering the exception handler
  *block_flags |= BF_INVALIDATE | BF_SLOWMEM;

  return true;
}
