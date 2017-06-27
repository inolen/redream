test_movca:
  # REGISTER_IN r0 -6
  mov.l .L2, r1
  movca.l r0, @r1
  mov.l @r1, r2
  rts
  nop
  # REGISTER_OUT r2 -6

test_movllpc:
  # MOV.L   @(disp8,PC),Rn
  mov.l .L1, r2
  rts
  nop
  # REGISTER_OUT r2 -12

test_movls:
  # REGISTER_IN r0 -12
  mov.l .L2, r1
  mov.l r0, @r1
  mov.l @r1, r2
  rts
  nop
  # REGISTER_OUT r2 -12

test_movlm:
  mov.l .L2, r0
  mov.l @r0+, r1
  add r1, r1
  mov.l r1, @-r0
  mov.l @r0, r2
  rts
  nop
  # REGISTER_OUT r2 -24

test_movlm_rnisrm:
  # test that reg is decremented
  mov.l .L2, r0
  add #4, r0
  mov r0, r1
  mov r1, r2
  mov.l r1, @-r1
  sub r1, r0
  # test that reg was stored to .L2 before it was decremented
  mov.l .L1, r1
  mov #0xff, r3
  and r3, r2
  cmp/eq r2, r1
  movt r1
  rts
  nop
  # REGISTER_OUT r0 4
  # REGISTER_OUT r1 1

test_movlp:
  mov.l .L2, r0
  mov.w @r0+, r0
  rts
  nop
  # REGISTER_OUT r0, -12

test_movlsmd:
  mov.l .L2, r0
  mov.l @(4, r0), r1
  add r1, r1
  mov.l r1, @(4, r0)
  mov.l @(4, r0), r2
  rts
  nop
  # REGISTER_OUT r2 -26

test_movls0:
  mov.l .L2, r0
  mov #4, r1
  mov.l @(r0, r1), r2
  add r2, r2
  mov.l r2, @(r0, r1)
  mov.l @(r0, r1), r3
  rts
  nop
  # REGISTER_OUT r3 -26

test_movls0g:
  mov.l .L2, r0
  ldc r0, GBR
  mov.l @(4, GBR), r0
  add r0, r0
  mov.l r0, @(4, GBR)
  # overwrite r0 to make sure the next instruction is actually working
  mov #99, r0
  mov.l @(4, GBR), r0
  rts
  nop
  # REGISTER_OUT r0 -26

.align 4
.L1:
  .long -12
  .long -13

.align 4
.L2:
  .long .L1
