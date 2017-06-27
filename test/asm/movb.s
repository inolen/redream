test_movbs:
  # REGISTER_IN r0 -12
  mov.l .L2, r1
  mov.b r0, @r1
  mov.b @r1, r2
  rts
  nop
  # REGISTER_OUT r2 -12

test_movbm:
  mov.l .L2, r0
  mov.b @r0+, r1
  add r1, r1
  mov.b r1, @-r0
  mov.b @r0, r2
  rts
  nop
  # REGISTER_OUT r2 -24

test_movbm_rnisrm:
  # test that reg is decremented
  mov.l .L2, r1
  add #1, r1
  mov r1, r2
  mov r1, r0
  mov.b r2, @-r2
  sub r2, r1
  # test that reg was stored to .L2 before it was decremented
  mov.l .L2, r2
  mov.b @r2, r2
  and #0xff, r0
  cmp/eq r2, r0
  movt r0
  rts
  nop
  # REGISTER_OUT r0 1
  # REGISTER_OUT r1 1

test_movbp:
  mov.l .L2, r0
  mov.b @r0+, r0
  rts
  nop
  # REGISTER_OUT r0, -12

test_movbs0d:
  mov.l .L2, r1
  mov.b @(1, r1), r0
  add r0, r0
  mov.b r0, @(1, r1)
  # overwrite r0 to make sure the next instruction is actually working
  mov #99, r0
  mov.b @(1, r1), r0
  rts
  nop
  # REGISTER_OUT r0 -26

test_movbs0:
  mov.l .L2, r0
  mov #1, r1
  mov.b @(r0, r1), r2
  add r2, r2
  mov.b r2, @(r0, r1)
  mov.b @(r0, r1), r3
  rts
  nop
  # REGISTER_OUT r3 -26

test_movbs0g:
  mov.l .L2, r0
  ldc r0, GBR
  mov.b @(1, GBR), r0
  add r0, r0
  mov.b r0, @(1, GBR)
  # overwrite r0 to make sure the next instruction is actually working
  mov #99, r0
  mov.b @(1, GBR), r0
  rts
  nop
  # REGISTER_OUT r0 -26

.align 4
.L1:
  .byte -12
  .byte -13

.align 4
.L2:
  .long .L1
