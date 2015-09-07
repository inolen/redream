test_or:
  # REGISTER_IN r0 0xffffff00
  # REGISTER_IN r1 0x00ffffff
  or r0, r1
  rts
  nop
  # REGISTER_OUT r1 0xffffffff

test_or_imm:
  # REGISTER_IN r0 0xffffff00
  or #0xf, r0
  rts
  nop
  # REGISTER_OUT r0 0xffffff0f

test_or_disp:
  mov.l .L2, r0
  ldc r0, GBR
  mov #4, r0
  or.b #0x22, @(r0, GBR)
  mov.l @(4, GBR), r0
  rts 
  nop
  # REGISTER_OUT r0 0x33

.align 4
.L1:
  .long 0x0
  .long 0x00000011

.align 4
.L2:
  .long .L1
