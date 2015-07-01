  .text
  .global start
start:
# TAS.B   @Rn, (Rn)=0
  mov.l .L2, r1
  tas.b @r1
  stc SR, r0
  and #0x1, r0
  mov r0, r2
# TAS.B   @Rn, (Rn)=1
  tas.b @r1
  stc SR, r0
  and #0x1, r0
  mov r0, r3
  mov.l @r1, r4
# TST     Rm,Rn, Rn & Rm = 0
  mov.l .L1+4, r0
  mov.l .L1+8, r1
  tst r1, r0
  stc SR, r0
  and #0x1, r0
  mov r0, r5
# TST     Rm,Rn, Rn & Rm = 0xffff0000
  mov.l .L1+8, r0
  mov.l .L1+8, r1
  tst r1, r0
  stc SR, r0
  and #0x1, r0
  mov r0, r6
# TST     #imm,R0, R0 & imm = 0
  mov.l .L1+8, r0
  tst #0xff, r0
  stc SR, r0
  and #0x1, r0
  mov r0, r7
# TST     #imm,R0, R0 & imm = 0x000000ff
  mov.l .L1+4, r0
  tst #0xff, r0
  stc SR, r0
  and #0x1, r0
  mov r0, r8
# TST.B   #imm,@(R0,GBR), (R0 + GBR) & imm = 0
  mov.l .L2, r0
  ldc r0, GBR
  mov #8, r0
  tst.b #0xff, @(r0, GBR)
  stc SR, r0
  and #0x1, r0
  mov r0, r9
# TST.B   #imm,@(R0,GBR), (R0 + GBR) & imm = 0x000000ff
  mov.l .L2, r0
  ldc r0, GBR
  mov #4, r0
  tst.b #0xff, @(r0, GBR)
  stc SR, r0
  and #0x1, r0
  mov r0, r10
  rts 
  nop
  .align 4
.L1:
  .long 0x0
  .long 0x0000ffff
  .long 0xffff0000
  .align 4
.L2:
  .long .L1

# REGISTER_OUT r2  1
# REGISTER_OUT r3  0
# REGISTER_OUT r4  128
# REGISTER_OUT r5  1
# REGISTER_OUT r6  0
# REGISTER_OUT r7  1
# REGISTER_OUT r8  0
# REGISTER_OUT r9  1
# REGISTER_OU r10 0
