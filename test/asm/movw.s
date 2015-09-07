test_movwlpc:
  # MOV.W   @(disp8,PC),Rn
  mov.w .L1, r2
  rts
  nop
  # REGISTER_OUT r2 -12

test_movws:
  # REGISTER_IN r0 -12
  mov.l .L2, r1
  mov.w r0, @r1
  mov.w @r1, r2
  rts
  nop
  # REGISTER_OUT r2 -12

test_movwm:
  mov.l .L2, r0
  mov.w @r0+, r1
  add r1, r1
  mov.w r1, @-r0
  mov.w @r0, r2
  rts
  nop
  # REGISTER_OUT r2 -24

test_movws0d:
  mov.l .L2, r1
  mov.w @(2, r1), r0
  add r0, r0
  mov.w r0, @(2, r1)
  # overwrite r0 to make sure the next instruction is actually working
  mov #99, r0
  mov.w @(2, r1), r0
  rts
  nop
  # REGISTER_OUT r0 -26

test_movws0:
  mov.l .L2, r0
  mov #2, r1
  mov.w @(r0, r1), r2
  add r2, r2
  mov.w r2, @(r0, r1)
  mov.w @(r0, r1), r3
  rts
  nop
  # REGISTER_OUT r3 -26

test_movws0g:
  mov.l .L2, r0
  ldc r0, GBR
  mov.w @(2, GBR), r0
  add r0, r0
  mov.w r0, @(2, GBR)
  # overwrite r0 to make sure the next instruction is actually working
  mov #99, r0
  mov.w @(2, GBR), r0
  rts
  nop
  # REGISTER_OUT r0 -26

.align 4
.L1:
  .short -12
  .short -13

.align 4
.L2:
  .long .L1
