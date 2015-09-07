test_tasb_zero:
  mov.l .L2, r1
  tas.b @r1
  movt r2
  mov.l @r1, r3
  rts
  nop
  # REGISTER_OUT r2  1
  # REGISTER_OUT r3  128

test_tasb_nonzero:
  # REGISTER_IN r2 1
  mov.l .L2, r1
  mov.l r2, @r1
  tas.b @r1
  movt r2
  mov.l @r1, r3
  rts
  nop
  # REGISTER_OUT r2  0
  # REGISTER_OUT r3  129

test_tst_zero:
  # REGISTER_IN r0 0x0000ffff
  # REGISTER_IN r1 0xffff0000
  tst r1, r0
  movt r2
  rts
  nop
  # REGISTER_OUT r2  1

test_tst_nonzero:
  # REGISTER_IN r0 0xffff0000
  # REGISTER_IN r1 0xffff0000
  tst r1, r0
  movt r2
  rts
  nop
  # REGISTER_OUT r2  0

test_tst_imm_zero:
  mov #0xf0, r0
  tst #0x0f, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r1 1

test_tst_imm_nonzero:
  mov #0xff, r0
  tst #0xff, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r1 0

test_tst_disp_zero:
  mov.l .L2, r0
  ldc r0, GBR
  mov #8, r0
  tst.b #0xff, @(r0, GBR)
  movt r1
  rts
  nop
  # REGISTER_OUT r1 1

test_tst_disp_nonzero:
  mov.l .L2, r0
  ldc r0, GBR
  mov #4, r0
  tst.b #0xff, @(r0, GBR)
  movt r1
  rts 
  nop
  # REGISTER_OU r1 0

.align 4
.L1:
  .long 0x0
  .long 0x0000ffff
  .long 0xffff0000

.align 4
.L2:
  .long .L1
