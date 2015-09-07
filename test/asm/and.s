test_and:
  # REGISTER_IN r0 0x00ffffff
  # REGISTER_IN r1 0xffffff00
  and r0, r1
  rts
  nop
  # REGISTER_OUT r1 0xffff00

test_and_imm:
  # REGISTER_IN r0 0x00ffffff
  and #0xf0, r0
  rts
  nop
  # REGISTER_OUT r0 0xf0

test_and_disp:
  mov.l .L2, r0
  ldc r0, GBR
  mov #4, r0
  and.b #0x3f, @(r0, GBR)
  mov.l @(4, GBR), r0
  rts 
  nop
  # REGISTER_OUT r0 0x3c

.align 4
.L1:
  .long 0x0
  .long 0x000000fc

.align 4
.L2:
  .long .L1
