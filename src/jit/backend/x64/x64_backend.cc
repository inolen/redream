#include <iomanip>
#include <sstream>
#include <xbyak/xbyak.h>
#include "core/memory.h"
#include "emu/profiler.h"
#include "hw/memory.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_disassembler.h"
#include "sys/exception_handler.h"

using namespace re;
using namespace re::hw;
using namespace re::jit;
using namespace re::jit::backend;
using namespace re::jit::backend::x64;
using namespace re::jit::ir;
using namespace re::sys;

namespace re {
namespace jit {
namespace backend {
namespace x64 {
// x64 register layout

// %rax %eax %ax %al      <-- temporary
// %rcx %ecx %cx %cl      <-- argument
// %rdx %edx %dx %dl      <-- argument
// %rbx %ebx %bx %bl      <-- available, callee saved
// %rsp %esp %sp %spl     <-- reserved
// %rbp %ebp %bp %bpl     <-- available, callee saved
// %rsi %esi %si %sil     <-- argument
// %rdi %edi %di %dil     <-- argument
// %r8 %r8d %r8w %r8b     <-- argument
// %r9 %r9d %r9w %r9b     <-- argument
// %r10 %r10d %r10w %r10b <-- available, not callee saved
// %r11 %r11d %r11w %r11b <-- available, not callee saved
// %r12 %r12d %r12w %r12b <-- available, callee saved
// %r13 %r13d %r13w %r13b <-- available, callee saved
// %r14 %r14d %r14w %r14b <-- available, callee saved
// %r15 %r15d %r15w %r15b <-- available, callee saved

// msvc calling convention uses rcx, rdx, r8 and r9 for arguments
// amd64 calling convention uses rdi, rsi, rdx, rcx, r8 and r9 for arguments
// both use the same xmm registers for floating point arguments
// our largest function call uses only 3 arguments, leaving rdi, rsi and r9
// available on msvc and rcx, r8 and r9 available on amd64

// rax is used as a scratch register, while rdi/r8, r9 and xmm1 are used for
// storing
// a constant in case the constant propagation pass didn't eliminate it

// rsi is left unused on msvc and rcx is left unused on amd64
const Register x64_registers[] = {
    {"rbx", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::rbx)},
    {"rbp", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::rbp)},
    {"r12", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::r12)},
    {"r13", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::r13)},
    {"r14", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::r14)},
    {"r15", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::r15)},
    {"xmm6", ir::VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm6)},
    {"xmm7", ir::VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm7)},
    {"xmm8", ir::VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm8)},
    {"xmm9", ir::VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm9)},
    {"xmm10", ir::VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm10)},
    {"xmm11", ir::VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm11)}};

const int x64_num_registers = sizeof(x64_registers) / sizeof(Register);

#if PLATFORM_WINDOWS
const int x64_arg0_idx = Xbyak::Operand::RCX;
const int x64_arg1_idx = Xbyak::Operand::RDX;
const int x64_arg2_idx = Xbyak::Operand::R8;
const int x64_tmp0_idx = Xbyak::Operand::RDI;
const int x64_tmp1_idx = Xbyak::Operand::R9;
#else
const int x64_arg0_idx = Xbyak::Operand::RDI;
const int x64_arg1_idx = Xbyak::Operand::RSI;
const int x64_arg2_idx = Xbyak::Operand::RDX;
const int x64_tmp0_idx = Xbyak::Operand::R8;
const int x64_tmp1_idx = Xbyak::Operand::R9;
#endif
}
}
}
}

// this will break down if running two instances of the x64 backend, but it's
// extremely useful when profiling to group JITd blocks of code with an actual
// symbol name
static const size_t x64_static_code_size = 4096;
static const size_t x64_code_size = 1024 * 1024 * 8;
static uint8_t x64_codegen[x64_code_size];

X64Backend::X64Backend(Memory &memory, void *guest_ctx)
    : Backend(memory, guest_ctx),
      static_emitter_(x64_codegen, x64_static_code_size),
      emitter_(x64_codegen + x64_static_code_size,
               x64_code_size - x64_static_code_size) {
  Xbyak::CodeArray::protect(x64_codegen, x64_code_size, true);
  AssembleThunks();
}

X64Backend::~X64Backend() {}

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

void X64Backend::Reset() { emitter_.Reset(); }

BlockPointer X64Backend::AssembleBlock(ir::IRBuilder &builder,
                                       int block_flags) {
  // try to generate the x64 code. if the code buffer overflows let the backend
  // know so it can reset the cache and try again
  BlockPointer fn;
  try {
    fn = emitter_.Emit(builder, memory_, guest_ctx_, block_flags);
  } catch (const Xbyak::Error &e) {
    if (e == Xbyak::ERR_CODE_IS_TOO_BIG) {
      return nullptr;
    }
    LOG_FATAL("X64 codegen failure, %s", e.what());
  }

  return fn;
}

bool X64Backend::HandleFastmemException(Exception &ex) {
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

  // push the return address (the next instruction after the current mov) to
  // the stack. also, adjust the stack for the return address, with an extra
  // 8 bytes to keep it aligned
  re::store(reinterpret_cast<uint8_t *>(ex.thread_state.rsp - 8),
            ex.thread_state.rip + mov.length);
  ex.thread_state.rsp -= STACK_SHADOW_SPACE + 8 + 8;
  CHECK(ex.thread_state.rsp % 16 == 0);

  if (mov.is_load) {
    // prep argument registers (memory object, guest_addr) for read function
    ex.thread_state.r[x64_arg0_idx] = reinterpret_cast<uint64_t>(&memory_);
    ex.thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);

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
    ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_[mov.reg]);
  } else {
    // prep argument registers (memory object, guest_addr, value) for write
    // function
    ex.thread_state.r[x64_arg0_idx] = reinterpret_cast<uint64_t>(&memory_);
    ex.thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);
    ex.thread_state.r[x64_arg2_idx] = ex.thread_state.r[mov.reg];

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
    ex.thread_state.rip = reinterpret_cast<uint64_t>(store_thunk_);
  }

  return true;
}

void X64Backend::AssembleThunks() {
  auto &e = static_emitter_;

  {
    for (int i = 0; i < 16; i++) {
      e.align(32);

      load_thunk_[i] = e.getCurr<SlowmemThunk>();

      Xbyak::Reg64 dst(i);
      e.call(e.rax);
      e.mov(dst, e.rax);
      e.mov(e.r10, reinterpret_cast<uint64_t>(guest_ctx_));
      e.mov(e.r11, reinterpret_cast<uint64_t>(memory_.protected_base()));
      e.add(e.rsp, STACK_SHADOW_SPACE + 8);
      e.ret();
    }
  }

  {
    e.align(32);

    store_thunk_ = e.getCurr<SlowmemThunk>();

    e.call(e.rax);
    e.mov(e.r10, reinterpret_cast<uint64_t>(guest_ctx_));
    e.mov(e.r11, reinterpret_cast<uint64_t>(memory_.protected_base()));
    e.add(e.rsp, STACK_SHADOW_SPACE + 8);
    e.ret();
  }
}
