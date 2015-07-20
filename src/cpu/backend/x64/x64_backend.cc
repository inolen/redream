#include "cpu/backend/x64/x64_backend.h"
#include "cpu/backend/x64/x64_block.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

static Register x64_registers[] = {{"rax", VALUE_INT_MASK},
                                   {"rbx", VALUE_INT_MASK},
                                   {"rcx", VALUE_INT_MASK},
                                   {"rdx", VALUE_INT_MASK},
                                   {"rsi", VALUE_INT_MASK},
                                   {"rdi", VALUE_INT_MASK},
                                   {"rbp", VALUE_INT_MASK},
                                   {"rsp", VALUE_INT_MASK},
                                   {"r8", VALUE_INT_MASK},
                                   {"r9", VALUE_INT_MASK},
                                   {"r10", VALUE_INT_MASK},
                                   {"r11", VALUE_INT_MASK},
                                   {"r12", VALUE_INT_MASK},
                                   {"r13", VALUE_INT_MASK},
                                   {"r14", VALUE_INT_MASK},
                                   {"r15", VALUE_INT_MASK},
                                   {"mm0", VALUE_FLOAT_MASK},
                                   {"mm1", VALUE_FLOAT_MASK},
                                   {"mm2", VALUE_FLOAT_MASK},
                                   {"mm3", VALUE_FLOAT_MASK},
                                   {"mm4", VALUE_FLOAT_MASK},
                                   {"mm5", VALUE_FLOAT_MASK},
                                   {"mm6", VALUE_FLOAT_MASK},
                                   {"mm7", VALUE_FLOAT_MASK}};

static const Xbyak::Reg *reg_map[] = {
    &Xbyak::util::rax, &Xbyak::util::rbx, &Xbyak::util::rcx, &Xbyak::util::rdx,
    &Xbyak::util::rsi, &Xbyak::util::rdi, &Xbyak::util::rbp, &Xbyak::util::rsp,
    &Xbyak::util::r8,  &Xbyak::util::r9,  &Xbyak::util::r10, &Xbyak::util::r11,
    &Xbyak::util::r12, &Xbyak::util::r13, &Xbyak::util::r14, &Xbyak::util::r15,
    &Xbyak::util::mm0, &Xbyak::util::mm1, &Xbyak::util::mm2, &Xbyak::util::mm3,
    &Xbyak::util::mm4, &Xbyak::util::mm5, &Xbyak::util::mm6, &Xbyak::util::mm7};

X64Backend::X64Backend(emu::Memory &memory) : Backend(memory) {}

X64Backend::~X64Backend() {}

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

bool X64Backend::Init() { return true; }

std::unique_ptr<RuntimeBlock> X64Backend::AssembleBlock(IRBuilder &builder) {
  int guest_cycles = 0;

  // 0. LOAD_CONTEXT 40 %0
  // 1. LOAD_CONTEXT 36 %1
  // 2. ADD %0 %1 %2 <--- ideally %0 and %2 should re-use the same register
  // 3. STORE_CONTEXT 40 %2
  // 4. LOAD_CONTEXT 16 %3
  // 5. BRANCH %3

  // RuntimeContext * is at RCX on Windows, RDI on OSX

  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      if (instr->op() == OP_LOAD_CONTEXT) {
        if (instr->arg0()->value<int32_t>() == 40) {
          gen_.mov(*reg_map[instr->result()->reg()], gen_.dword[gen_.rdi + 40]);
        } else if (instr->arg0()->value<int32_t>() == 36) {
          gen_.mov(*reg_map[instr->result()->reg()], gen_.dword[gen_.rdi + 36]);
        }
      } else if (instr->op() == OP_ADD) {
        gen_.add(*reg_map[instr->arg0()->reg()],
                 *reg_map[instr->arg1()->reg()]);
      } else if (instr->op() == OP_STORE_CONTEXT) {
        gen_.mov(gen_.dword[gen_.rdi + 40], *reg_map[instr->arg1()->reg()]);
      } else if (instr->op() == OP_BRANCH) {
        gen_.ret();
      }
    }
  }

  X64Fn fn = gen_.getCode<X64Fn>();
  return std::unique_ptr<RuntimeBlock>(new X64Block(guest_cycles, fn));
}
