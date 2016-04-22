#include <inttypes.h>
#include <iomanip>
#include <sstream>
#include <xbyak/xbyak.h>
#include "core/memory.h"
#include "core/profiler.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_disassembler.h"
#include "sys/exception_handler.h"

using namespace re;
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

// %rax %eax %ax %al      <-- both: temporary
// %rcx %ecx %cx %cl      <-- both: argument
// %rdx %edx %dx %dl      <-- both: argument
// %rbx %ebx %bx %bl      <-- both: available (callee saved)
// %rsp %esp %sp %spl     <-- both: reserved
// %rbp %ebp %bp %bpl     <-- both: available (callee saved)
// %rsi %esi %si %sil     <-- msvc: available (callee saved), amd64: argument
// %rdi %edi %di %dil     <-- msvc: available (callee saved), amd64: argument
// %r8 %r8d %r8w %r8b     <-- both: argument
// %r9 %r9d %r9w %r9b     <-- both: argument
// %r10 %r10d %r10w %r10b <-- both: available (not callee saved)
// %r11 %r11d %r11w %r11b <-- both: available (not callee saved)
// %r12 %r12d %r12w %r12b <-- both: available (callee saved)
// %r13 %r13d %r13w %r13b <-- both: available (callee saved)
// %r14 %r14d %r14w %r14b <-- both: available (callee saved)
// %r15 %r15d %r15w %r15b <-- both: available (callee saved)

// msvc calling convention uses rcx, rdx, r8, r9 for arguments
// amd64 calling convention uses rdi, rsi, rdx, rcx, r8, r9 for arguments
// both use the same xmm registers for floating point arguments
// our largest function call uses only 3 arguments
// msvc is left with rax, rdi, rsi, r9-r11,
// amd64 is left with rax, rcx, r8-r11 available on amd64

// rax is used as a scratch register
// r10, r11, xmm1 are used for constant not eliminated by const propagation
// r14, r15 are reserved for the context and memory pointers

const Register x64_registers[] = {
    {"rbx", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::rbx)},
    {"rbp", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::rbp)},
    {"r12", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::r12)},
    {"r13", ir::VALUE_INT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::r13)},
    // {"r14", ir::VALUE_INT_MASK,
    //  reinterpret_cast<const void *>(&Xbyak::util::r14)},
    // {"r15", ir::VALUE_INT_MASK,
    //  reinterpret_cast<const void *>(&Xbyak::util::r15)},
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
    {"xmm11", ir::VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm11)},
    {"xmm12", ir::VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm12)},
    {"xmm13", ir::VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm13)},
    {"xmm14", ir::VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm14)},
    {"xmm15", ir::VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm15)}};

const int x64_num_registers = sizeof(x64_registers) / sizeof(Register);

#if PLATFORM_WINDOWS
const int x64_arg0_idx = Xbyak::Operand::RCX;
const int x64_arg1_idx = Xbyak::Operand::RDX;
const int x64_arg2_idx = Xbyak::Operand::R8;
#else
const int x64_arg0_idx = Xbyak::Operand::RDI;
const int x64_arg1_idx = Xbyak::Operand::RSI;
const int x64_arg2_idx = Xbyak::Operand::RDX;
#endif
const int x64_tmp0_idx = Xbyak::Operand::R10;
const int x64_tmp1_idx = Xbyak::Operand::R11;
}
}
}
}

// this will break down if running two instances of the x64 backend, but it's
// extremely useful when profiling to group JITd blocks of code with an actual
// symbol name
static const size_t x64_code_size = 1024 * 1024 * 8;
static uint8_t x64_codegen[x64_code_size];

X64Backend::X64Backend(const MemoryInterface &memif)
    : Backend(memif), emitter_(memif, x64_codegen, x64_code_size) {
  CHECK_EQ(cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle_), CS_ERR_OK);

  Xbyak::CodeArray::protect(x64_codegen, x64_code_size, true);

  Reset();
}

X64Backend::~X64Backend() { cs_close(&capstone_handle_); }

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

void X64Backend::Reset() {
  emitter_.Reset();

  EmitThunks();
}

const uint8_t *X64Backend::AssembleCode(ir::IRBuilder &builder, int *size) {
  // try to generate the x64 code. if the code buffer overflows let the backend
  // know so it can reset the cache and try again
  const uint8_t *fn = nullptr;

  try {
    fn = emitter_.Emit(builder, size);
  } catch (const Xbyak::Error &e) {
    if (e != Xbyak::ERR_CODE_IS_TOO_BIG) {
      LOG_FATAL("X64 codegen failure, %s", e.what());
    }
  }

  return fn;
}

void X64Backend::DumpCode(const uint8_t *host_addr, int size) {
  cs_insn *insns;
  size_t count = cs_disasm(capstone_handle_, host_addr, size, 0, 0, &insns);
  CHECK(count);

  for (size_t i = 0; i < count; i++) {
    cs_insn &insn = insns[i];
    LOG_INFO("0x%" PRIx64 ":\t%s\t\t%s", insn.address, insn.mnemonic,
             insn.op_str);
  }

  cs_free(insns, count);
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
  const uint8_t *protected_start =
      reinterpret_cast<const uint8_t *>(memif_.mem_base);
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
    ex.thread_state.r[x64_arg0_idx] =
        reinterpret_cast<uint64_t>(memif_.mem_self);
    ex.thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);

    // prep function call address for thunk
    switch (mov.operand_size) {
      case 1:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.r8);
        break;
      case 2:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.r16);
        break;
      case 4:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.r32);
        break;
      case 8:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.r64);
        break;
    }

    // resume execution in the thunk once the exception handler exits
    ex.thread_state.rip = reinterpret_cast<uint64_t>(load_thunk_[mov.reg]);
  } else {
    // prep argument registers (memory object, guest_addr, value) for write
    // function
    ex.thread_state.r[x64_arg0_idx] =
        reinterpret_cast<uint64_t>(memif_.mem_self);
    ex.thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);
    ex.thread_state.r[x64_arg2_idx] = ex.thread_state.r[mov.reg];

    // prep function call address for thunk
    switch (mov.operand_size) {
      case 1:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.w8);
        break;
      case 2:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.w16);
        break;
      case 4:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.w32);
        break;
      case 8:
        ex.thread_state.rax = reinterpret_cast<uint64_t>(memif_.w64);
        break;
    }

    // resume execution in the thunk once the exception handler exits
    ex.thread_state.rip = reinterpret_cast<uint64_t>(store_thunk_);
  }

  return true;
}

void X64Backend::EmitThunks() {
  auto &e = emitter_;

  {
    for (int i = 0; i < 16; i++) {
      e.align(32);

      load_thunk_[i] = e.getCurr<SlowmemThunk>();

      Xbyak::Reg64 dst(i);
      e.call(e.rax);
      e.mov(dst, e.rax);
      e.add(e.rsp, STACK_SHADOW_SPACE + 8);
      e.ret();
    }
  }

  {
    e.align(32);

    store_thunk_ = e.getCurr<SlowmemThunk>();

    e.call(e.rax);
    e.add(e.rsp, STACK_SHADOW_SPACE + 8);
    e.ret();
  }
}
