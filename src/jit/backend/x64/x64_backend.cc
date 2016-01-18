#include <iomanip>
#include <sstream>
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

extern "C" void load_thunk_rax();
extern "C" void load_thunk_rcx();
extern "C" void load_thunk_rdx();
extern "C" void load_thunk_rbx();
extern "C" void load_thunk_rsp();
extern "C" void load_thunk_rbp();
extern "C" void load_thunk_rsi();
extern "C" void load_thunk_rdi();
extern "C" void load_thunk_r8();
extern "C" void load_thunk_r9();
extern "C" void load_thunk_r10();
extern "C" void load_thunk_r11();
extern "C" void load_thunk_r12();
extern "C" void load_thunk_r13();
extern "C" void load_thunk_r14();
extern "C" void load_thunk_r15();
extern "C" void store_thunk();

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

// this will break down if running two instances of the x64 backend, but it's
// extremely useful when profiling to group JITd blocks of code with an actual
// symbol name
static const size_t x64_codegen_size = 1024 * 1024 * 8;
static uint8_t x64_codegen[x64_codegen_size];

}
}
}
}

X64Backend::X64Backend(Memory &memory)
    : Backend(memory), emitter_(x64_codegen, x64_codegen_size) {}

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

  // push the original argument registers and the return address (the next
  // instruction after the current mov) to the stack
  ex.thread_state.rsp -= STACK_SHADOW_SPACE + 24 + 8;
  dvm::store(
      reinterpret_cast<uint8_t *>(ex.thread_state.rsp + STACK_SHADOW_SPACE),
      ex.thread_state.r[Xbyak::Operand::INT_ARG2]);
  dvm::store(
      reinterpret_cast<uint8_t *>(ex.thread_state.rsp + STACK_SHADOW_SPACE + 8),
      ex.thread_state.r[Xbyak::Operand::INT_ARG1]);
  dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp +
                                         STACK_SHADOW_SPACE + 16),
             ex.thread_state.r[Xbyak::Operand::INT_ARG0]);
  dvm::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp +
                                         STACK_SHADOW_SPACE + 24),
             ex.thread_state.rip + mov.length);

  if (mov.is_load) {
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
    switch (mov.reg) {
      case 0:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rax);
        break;
      case 1:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rcx);
        break;
      case 2:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rdx);
        break;
      case 3:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rbx);
        break;
      case 4:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rsp);
        break;
      case 5:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rbp);
        break;
      case 6:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rsi);
        break;
      case 7:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_rdi);
        break;
      case 8:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r8);
        break;
      case 9:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r9);
        break;
      case 10:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r10);
        break;
      case 11:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r11);
        break;
      case 12:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r12);
        break;
      case 13:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r13);
        break;
      case 14:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r14);
        break;
      case 15:
        ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_r15);
        break;
    }
  } else {
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

    // ensure stack is 16b aligned
    CHECK(ex.thread_state.rsp % 16 == 0);

    // resume execution in the thunk once the exception handler exits
    ex.thread_state.rip = reinterpret_cast<uint64_t>(store_thunk);
  }

  // tell the cache to invalidate this block, appending the slowmem flag for
  // the next compile. the slowmem flag tells the backend to handle all load
  // and store operations with the slower Memory read and write functions (as
  // opposed to a `mov reg, [mmap_base + guest_addr]` instruction) to avoid
  // triggering the exception handler
  *block_flags |= BF_INVALIDATE | BF_SLOWMEM;

  return true;
}
