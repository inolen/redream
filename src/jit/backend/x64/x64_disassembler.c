#include "jit/backend/x64/x64_disassembler.h"

int x64_decode_mov(const uint8_t *data, struct x64_mov *mov) {
  const uint8_t *start = data;

  /* test for operand size prefix */
  int has_opprefix = 0;

  if (*data == 0x66) {
    has_opprefix = 1;
    data++;
  }

  /* test for REX prefix
     http://wiki.osdev.org/X86-64_Instruction_Encoding#Encoding */
  uint8_t rex = 0;
  uint8_t rex_w = 0;
  uint8_t rex_r = 0;
  uint8_t rex_x = 0;
  uint8_t rex_b = 0;

  if ((*data & 0xf0) == 0x40) {
    rex = *data;
    rex_w = rex & 0b1000;
    rex_r = rex & 0b0100;
    rex_x = rex & 0b0010;
    rex_b = rex & 0b0001;
    data++;
  }

  /* test for MOV opcode
     http://x86.renejeschke.de/html/file_module_x86_id_176.html */
  int is_load = 0;
  int has_imm = 0;
  int operand_size = 0;

  /* MOV r8,r/m8
     MOV r16,r/m16
     MOV r32,r/m32
     MOV r64,r/m64 */
  if (*data == 0x8a || *data == 0x8b) {
    is_load = 1;
    has_imm = 0;
    operand_size = *data == 0x8a ? 1 : (has_opprefix ? 2 : (rex_w ? 8 : 4));
    data++;
  }
  /* MOV r/m8,r8
     MOV r/m16,r16
     MOV r/m32,r32
     MOV r/m64,r64 */
  else if (*data == 0x88 || *data == 0x89) {
    is_load = 0;
    has_imm = 0;
    operand_size = *data == 0x88 ? 1 : (has_opprefix ? 2 : (rex_w ? 8 : 4));
    data++;
  }
  /* MOV r8,imm8
     MOV r16,imm16
     MOV r32,imm32 */
  else if (*data == 0xb0 || *data == 0xb8) {
    is_load = 1;
    has_imm = 1;
    operand_size = *data == 0xb0 ? 1 : (has_opprefix ? 2 : 4);
    data++;
  }
  /* MOV r/m8,imm8
     MOV r/m16,imm16
     MOV r/m32,imm32 */
  else if (*data == 0xc6 || *data == 0xc7) {
    is_load = 0;
    has_imm = 1;
    operand_size = *data == 0xc6 ? 1 : (has_opprefix ? 2 : 4);
    data++;
  }
  /* not a supported MOV instruction */
  else {
    return 0;
  }

  /* process ModR/M byte */
  uint8_t modrm = *data;
  uint8_t modrm_mod = (modrm & 0b11000000) >> 6;
  uint8_t modrm_reg = (modrm & 0b00111000) >> 3;
  uint8_t modrm_rm = (modrm & 0b00000111);
  data++;

  mov->is_load = is_load;
  mov->is_indirect = (modrm_mod != 0b11);
  mov->has_imm = has_imm;
  mov->has_base = 0;
  mov->has_index = 0;
  mov->operand_size = operand_size;
  mov->reg = modrm_reg + (rex_r ? 8 : 0);
  mov->base = 0;
  mov->index = 0;
  mov->scale = 0;
  mov->disp = 0;
  mov->imm = 0;

  /* process optional SIB byte */
  if (modrm_rm == 0b100) {
    uint8_t sib = *data;
    uint8_t sib_scale = (sib & 0b11000000) >> 6;
    uint8_t sib_index = (sib & 0b00111000) >> 3;
    uint8_t sib_base = (sib & 0b00000111);
    data++;

    mov->has_base = (modrm_mod != 0b00 || sib_base != 0b101);
    mov->has_index = (sib_index != 0b100);
    mov->base = sib_base + (rex_b ? 8 : 0);
    mov->index = sib_index + (rex_x ? 8 : 0);
    mov->scale = sib_scale;
  } else {
    mov->has_base = 1;
    mov->base = modrm_rm + (rex_b ? 8 : 0);
  }

  /* process optional displacement */
  switch (modrm_mod) {
    case 0b00: {
      /* RIP-relative */
      if (modrm_rm == 0b101) {
        mov->disp = *(uint32_t *)data;
        data += 4;
      }
    } break;

    case 0b01: {
      mov->disp = *data;
      data++;
    } break;

    case 0b10: {
      mov->disp = *(uint32_t *)data;
      data += 4;
    } break;
  }

  /* process optional immediate */
  if (mov->has_imm) {
    switch (mov->operand_size) {
      case 1: {
        mov->imm = *data;
        data++;
      } break;

      case 2: {
        mov->imm = *(uint16_t *)data;
        data += 2;
      } break;

      case 4: {
        mov->imm = *(uint32_t *)data;
        data += 4;
      } break;

      case 8: {
        mov->imm = *(uint64_t *)data;
        data += 8;
      } break;
    }
  }

  /* calculate total instruction length */
  mov->length = (int)(data - start);

  return 1;
}
