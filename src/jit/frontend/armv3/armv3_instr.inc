ARMV3_INSTR(INVALID, "invalid",                                        00000000000000000000000000000000, 0, FLAG_SET_PC)

// branches
ARMV3_INSTR(B,       "b{cond} {expr}",                                 xxxx1010xxxxxxxxxxxxxxxxxxxxxxxx, 1, FLAG_SET_PC)
ARMV3_INSTR(BL,      "bl{cond} {expr}",                                xxxx1011xxxxxxxxxxxxxxxxxxxxxxxx, 1, FLAG_SET_PC)

// data processing
ARMV3_INSTR(AND,     "and{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0000xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(EOR,     "eor{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0001xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(SUB,     "sub{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0010xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(RSB,     "rsb{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0011xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(ADD,     "add{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0100xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(ADC,     "adc{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0101xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(SBC,     "sbc{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0110xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(RSC,     "rsc{cond}{s} {rd}, {rn}, {expr}",                xxxx00x0111xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(TST,     "tst{cond} {rn}, {expr}",                         xxxx00x10001xxxx0000xxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(TEQ,     "teq{cond} {rn}, {expr}",                         xxxx00x10011xxxx0000xxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(CMP,     "cmp{cond} {rn}, {expr}",                         xxxx00x10101xxxx0000xxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(CMN,     "cmn{cond} {rn}, {expr}",                         xxxx00x10111xxxx0000xxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(ORR,     "orr{cond}{s} {rd}, {rn}, {expr}",                xxxx00x1100xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(MOV,     "mov{cond}{s} {rd}, {expr}",                      xxxx00x1101x0000xxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(BIC,     "bic{cond}{s} {rd}, {rn}, {expr}",                xxxx00x1110xxxxxxxxxxxxxxxxxxxxx, 1, FLAG_DATA)
ARMV3_INSTR(MVN,     "mvn{cond}{s} {rd}, {expr}",                      xxxx00x1111x0000xxxxxxxxxxxxxxxx, 1, FLAG_DATA)

// psr transfers
ARMV3_INSTR(MRS,     "mrs{cond} {rd}, {psr}",                          xxxx00010x001111xxxx000000000000, 1, FLAG_PSR)
ARMV3_INSTR(MSR,     "msr{cond} {psr}, {expr}",                        xxxx00x10x10xxxx111100000000xxxx, 1, FLAG_PSR)

// multiply and multiply-accumulate
ARMV3_INSTR(MUL,     "mul{cond}{s} {rd}, {rm}, {rs}",                  xxxx0000000xxxxxxxxxxxxx1001xxxx, 1, FLAG_MUL)
ARMV3_INSTR(MLA,     "mla{cond}{s} {rd}, {rm}, {rs}, {rn}",            xxxx0000001xxxxxxxxxxxxx1001xxxx, 1, FLAG_MUL)

// single data transfer
ARMV3_INSTR(LDR,     "ldr{cond}{b}{t} {rd}, {addr}",                   xxxx01xxxxx1xxxxxxxxxxxxxxxxxxxx, 1, FLAG_XFR)
ARMV3_INSTR(STR,     "str{cond}{b}{t} {rd}, {addr}",                   xxxx01xxxxx0xxxxxxxxxxxxxxxxxxxx, 1, FLAG_XFR)

// block data transfer
ARMV3_INSTR(LDM,     "ldm{cond}{stack} {rn}{!}, {rlist}{^}",           xxxx100xxxx1xxxxxxxxxxxxxxxxxxxx, 1, FLAG_BLK)
ARMV3_INSTR(STM,     "stm{cond}{stack} {rn}{!}, {rlist}{^}",           xxxx100xxxx0xxxxxxxxxxxxxxxxxxxx, 1, FLAG_BLK)

// single data swap
ARMV3_INSTR(SWP,     "swp{cond}{b} {rd}, {rm}, [{rn}]",                xxxx00010x00xxxxxxxx00001001xxxx, 1, FLAG_SWP)

// software interrupt
ARMV3_INSTR(SWI,     "swi{cond} {imm}",                                xxxx1111xxxxxxxxxxxxxxxxxxxxxxxx, 1, FLAG_SWI)

//// coprocessor data operations
//ARMV3_INSTR(CDP,     "cdp{cond} p#, <expr1>, cd, cn, cm{, <expr2>}",   xxxx1110xxxxxxxxxxxxyyyyzzz0xxxx, 1, 0)

//// coprocessor data transfers
//ARMV3_INSTR(LDC,     "ldc{cond}{l} p#, cd, {addr}",                    xxxx110PUNW1xxxxxxxxyyyyxxxxxxxx, 1, 0)
//ARMV3_INSTR(STC,     "stc{cond}{l} p#, cd, {addr}",                    xxxx110PUNW0xxxxxxxxyyyyxxxxxxxx, 1, 0)

//// coprocessor register transfers
//ARMV3_INSTR(MRC,     "mrc{cond} p#, <expr1>, {rd}, cn, cm{, <expr2>}", xxxx1110xxx1xxxxxxxxyyyyzzz1xxxx, 1, 0)
//ARMV3_INSTR(MCR,     "mcr{cond} p#, <expr1>, {rd}, cn, cm{, <expr2>}", xxxx1110xxx0xxxxxxxxyyyyzzz1xxxx, 1, 0)
