  .little
  .text
  .global start
start:
# MOV.L   @(disp8,PC),Rn
  mov.l .L1, r2
# MOV.L   Rm,@Rn
# MOV.L   @Rm,Rn
  mov.l .L2, r0
  add #4, r0
  mov.l @r0, r3
  add r3, r3
  mov.l r3, @r0
  mov.l @r0, r3
# MOV.L   Rm,@-Rn
# MOV.L   @Rm+,Rn
  mov.l .L2, r0
  add #8, r0
  mov.l @r0+, r4
  add r4, r4
  mov.l r4, @-r0
  mov.l @r0, r4
# MOV.L   R0,@(disp4,Rm)
# MOV.L   @(disp4,Rm),R0
  mov.l .L2, r5
  mov.l @(12, r5), r0
  add r0, r0
  mov.l r0, @(12, r5)
  mov.l @(12, r5), r0
  mov r0, r5
# MOV.L   @(R0,Rm),Rn
# MOV.L   Rm,@(R0,Rn)
  mov.l .L2, r0
  mov #16, r1
  mov.l @(r0, r1), r6
  add r6, r6
  mov.l r6, @(r0, r1)
  mov.l @(r0, r1), r6
# MOV.L   R0,@(disp8,GBR)
# MOV.L   @(disp8,GBR),R0
  mov.l .L2, r0
  ldc r0, GBR
  mov.l @(20, GBR), r0
  add r0, r0
  mov.l r0, @(20, GBR)
  mov.l @(20, GBR), r0
  mov r0, r7
  rts
  nop
  .align 4
.L1:
  .long -24
  .long -13
  .long -14
  .long -15
  .long -16
  .long -17
  .align 4
.L2:
  .long .L1

# REGISTER_OUT r2 -24
# REGISTER_OUT r3 -26
# REGISTER_OUT r4 -28
# REGISTER_OUT r5 -30
# REGISTER_OUT r6 -32
# REGISTER_OUT r7 -34
