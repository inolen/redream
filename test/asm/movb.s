  .little
  .text
  .global start
start:
# MOV.B   Rm,@Rn
# MOV.B   @Rm,Rn
  mov.l .L2, r0
  mov.b @r0, r2
  add r2, r2
  mov.b r2, @r0
  mov.b @r0, r2
# MOV.B   Rm,@-Rn
# MOV.B   @Rm+,Rn
  mov.l .L2, r0
  add #1, r0
  mov.b @r0+, r3
  add r3, r3
  mov.b r3, @-r0
  mov.b @r0, r3
# MOV.B   R0,@(disp4,Rm)
# MOV.B   @(disp4,Rm),R0
  mov.l .L2, r4
  mov.b @(2, r4), r0
  add r0, r0
  mov.b r0, @(2, r4)
  mov.b @(2, r4), r0
  mov r0, r4
# MOV.B   Rm,@(R0,Rn)
# MOV.B   @(R0,Rm),Rn
  mov.l .L2, r0
  mov #3, r1
  mov.b @(r0, r1), r5
  add r5, r5
  mov.b r5, @(r0, r1)
  mov.b @(r0, r1), r5
# MOV.B   R0,@(disp8,GBR)
# MOV.B   @(disp8,GBR),R0
  mov.l .L2, r0
  ldc r0, GBR
  mov.b @(4, GBR), r0
  add r0, r0
  mov.b r0, @(4, GBR)
  mov.b @(4, GBR), r0
  mov r0, r6
  rts
  nop
  .align 4
.L1:
  .byte -12
  .byte -13
  .byte -14
  .byte -15
  .byte -16
  .align 4
.L2:
  .long .L1

# REGISTER_OUT r2 -24
# REGISTER_OUT r3 -26
# REGISTER_OUT r4 -28
# REGISTER_OUT r5 -30
# REGISTER_OUT r6 -32
