  .little
  .text
  .global start
start:
# MOV.W   @(disp8,PC),Rn
  mov.w .L1, r2
# MOV.W   Rm,@Rn
# MOV.W   @Rm,Rn
  mov.l .L2, r0
  add #2, r0
  mov.w @r0, r3
  add r3, r3
  mov.w r3, @r0
  mov.w @r0, r3
# MOV.W   Rm,@-Rn
# MOV.W   @Rm+,Rn
  mov.l .L2, r0
  add #4, r0
  mov.w @r0+, r4
  add r4, r4
  mov.w r4, @-r0
  mov.w @r0, r4
# MOV.W   R0,@(disp4,Rm)
# MOV.W   @(disp4,Rm),R0
  mov.l .L2, r5
  mov.w @(6, r5), r0
  add r0, r0
  mov.w r0, @(6, r5)
  mov.w @(6, r5), r0
  mov r0, r5
# MOV.W   @(R0,Rm),Rn
# MOV.W   Rm,@(R0,Rn)
  mov.l .L2, r0
  mov #8, r1
  mov.w @(r0, r1), r6
  add r6, r6
  mov.w r6, @(r0, r1)
  mov.w @(r0, r1), r6
# MOV.W   R0,@(disp8,GBR)
# MOV.W   @(disp8,GBR),R0
  mov.l .L2, r0
  ldc r0, GBR
  mov.w @(10, GBR), r0
  add r0, r0
  mov.w r0, @(10, GBR)
  mov.w @(10, GBR), r0
  mov r0, r7
  rts
  nop
  .align 4
.L1:
  .short -24
  .short -13
  .short -14
  .short -15
  .short -16
  .short -17
  .align 4
.L2:
  .long .L1

# REGISTER_OUT r2 -24
# REGISTER_OUT r3 -26
# REGISTER_OUT r4 -28
# REGISTER_OUT r5 -30
# REGISTER_OUT r6 -32
# REGISTER_OUT r7 -34
