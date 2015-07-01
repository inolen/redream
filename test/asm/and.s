# REGISTER_IN r0 0x00ffffff
# REGISTER_IN r1 0xffffff00

  .text
  .global start
start:
# AND     Rm,Rn
  and r0, r1
# AND     #imm,R0
  and #0xff, r0
  mov r0, r2
# AND.B   #imm,@(R0,GBR)
  mov.l .L2, r0
  ldc r0, GBR
  mov #4, r0
  and.b #0x3f, @(r0, GBR)
  # use mov.l instead of mov.b to avoid sign extension of 0xff
  mov.l @(4, GBR), r0
  rts 
  nop
  .align 4
.L1:
  .long 0x0
  .long 0x000000fc
  .align 4
.L2:
  .long .L1

# REGISTER_OUT r1 0xffff00
# REGISTER_OUT r2 0xff
# REGISTER_OUT r0 0x3c
