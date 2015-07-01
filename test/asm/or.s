# REGISTER_IN r0 0xffffff00
# REGISTER_IN r1 0x00ffffff

  .text
  .global start
start:
# OR     Rm,Rn
  or r0, r1
# OR     #imm,R0
  or #0xff, r0
  mov r0, r2
# OR.B   #imm,@(R0,GBR)
  mov.l .L2, r0
  ldc r0, GBR
  mov #4, r0
  or.b #0x3f, @(r0, GBR)
  # mov.l instead of mov.b to avoid sign extension of 0xff
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

# REGISTER_OUT r2 0xffffffff
