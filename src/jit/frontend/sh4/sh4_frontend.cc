#include "core/memory.h"
#include "jit/frontend/sh4/sh4_analyzer.h"
#include "jit/frontend/sh4/sh4_builder.h"
#include "jit/frontend/sh4/sh4_frontend.h"

using namespace re::jit;
using namespace re::jit::frontend::sh4;
using namespace re::jit::ir;

SH4Frontend::SH4Frontend() : arena_(4096) {}

IRBuilder &SH4Frontend::TranslateCode(uint32_t guest_addr, uint8_t *guest_ptr,
                                      int flags, int *size) {
  // get the block size
  SH4Analyzer::AnalyzeBlock(guest_addr, guest_ptr, flags, size);

  // emit IR for the SH4 code
  arena_.Reset();
  SH4Builder *builder = arena_.Alloc<SH4Builder>();
  new (builder) SH4Builder(arena_);
  builder->Emit(guest_addr, guest_ptr, *size, flags);

  return *builder;
}

void SH4Frontend::DumpCode(uint32_t guest_addr, uint8_t *guest_ptr, int size) {
  char buffer[128];

  int i = 0;

  while (i < size) {
    Instr instr;
    instr.addr = guest_addr + i;
    instr.opcode = re::load<uint16_t>(guest_ptr + i);
    SH4Disassembler::Disasm(&instr);

    SH4Disassembler::Format(instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    i += 2;

    if (instr.flags & OP_FLAG_DELAYED) {
      Instr delay;
      delay.addr = guest_addr + i;
      delay.opcode = re::load<uint16_t>(guest_ptr + i);
      SH4Disassembler::Disasm(&delay);

      SH4Disassembler::Format(delay, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      i += 2;
    }
  }
}
