// static int sh4_dbg_num_registers() {
//   return 59;
// }

// static void sh4_dbg_step() {
//   // invalidate the block for the current pc
//   sh4->code_cache->RemoveBlocks(sh4->ctx.pc);

//   // recompile it with only one instruction and run it
//   uint32_t guest_addr = sh4->ctx.pc;
//   uint8_t *host_addr = space->Translate(guest_addr);
//   int flags = GetCompileFlags() | SH4_SINGLE_INSTR;

//   code_pointer_t code = sh4->code_cache->CompileCode(guest_addr, host_addr,
//   flags);
//   sh4->ctx.pc = code();

//   // let the debugger know we've stopped
//   dc->debugger->Trap();
// }

// static void sh4_dbg_add_breakpoint(int type, uint32_t addr) {
//   // save off the original instruction
//   uint16_t instr = space->R16(addr);
//   breakpoints_.insert(std::make_pair(addr, instr));

//   // write out an invalid instruction
//   space->W16(addr, 0);

//   sh4->code_cache->RemoveBlocks(addr);
// }

// static void sh4_dbg_remove_breakpoint(int type, uint32_t addr) {
//   // recover the original instruction
//   auto it = breakpoints_.find(addr);
//   CHECK_NE(it, breakpoints_.end());
//   uint16_t instr = it->second;
//   breakpoints_.erase(it);

//   // overwrite the invalid instruction with the original
//   space->W16(addr, instr);

//   sh4->code_cache->RemoveBlocks(addr);
// }

// static void sh4_dbg_read_memory(uint32_t addr, uint8_t *buffer, int size) {
//   space->Memcpy(buffer, addr, size);
// }

// void sh4_dbg_read_register(int n, uint64_t *value, int *size) {
//   if (n < 16) {
//     *value = sh4->ctx.r[n];
//   } else if (n == 16) {
//     *value = sh4->ctx.pc;
//   } else if (n == 17) {
//     *value = sh4->ctx.pr;
//   } else if (n == 18) {
//     *value = sh4->ctx.gbr;
//   } else if (n == 19) {
//     *value = sh4->ctx.vbr;
//   } else if (n == 20) {
//     *value = sh4->ctx.mach;
//   } else if (n == 21) {
//     *value = sh4->ctx.macl;
//   } else if (n == 22) {
//     *value = sh4->ctx.sr;
//   } else if (n == 23) {
//     *value = sh4->ctx.fpul;
//   } else if (n == 24) {
//     *value = sh4->ctx.fpscr;
//   } else if (n < 41) {
//     *value = sh4->ctx.fr[n - 25];
//   } else if (n == 41) {
//     *value = sh4->ctx.ssr;
//   } else if (n == 42) {
//     *value = sh4->ctx.spc;
//   } else if (n < 51) {
//     uint32_t *b0 = (sh4->ctx.sr & RB) ? sh4->ctx.ralt : sh4->ctx.r;
//     *value = b0[n - 43];
//   } else if (n < 59) {
//     uint32_t *b1 = (sh4->ctx.sr & RB) ? sh4->ctx.r : sh4->ctx.ralt;
//     *value = b1[n - 51];
//   }

//   *size = 4;
// }
