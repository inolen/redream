  .text
  .global start
start:
# ROTL    Rn (Rn MSB = 0)
  mov.l .L1, r2
  rotl r2
  stc SR, r0
  and #0x1, r0
  mov r0, r3
# ROTL    Rn (Rn MSB = 1)
  mov.l .L1+4, r4
  rotl r4
  stc SR, r0
  and #0x1, r0
  mov r0, r5
# ROTR    Rn (Rn LSB = 0)
  mov.l .L1+4, r6
  rotr r6
  stc SR, r0
  and #0x1, r0
  mov r0, r7
# ROTR    Rn (Rn LSB = 1)
  mov.l .L1, r8
  rotr r8
  stc SR, r0
  and #0x1, r0
  mov r0, r9
# ROTCL    Rn (MSB = 1, T = 0)
  # set T = 0
  stc SR, r0
  mov.l .L1+4, r1
  and r1, r0
  ldc r0, SR
  # rotate
  mov.l .L1+4, r10
  rotcl r10
  stc SR, r0
  and #0x1, r0
  mov r0, r11
# ROTCL    Rn (MSB = 0, T = 1)
  # set T = 1
  stc SR, r0
  or #1, r0
  ldc r0, SR
  # rotate
  mov.l .L1, r12
  rotcl r12
  stc SR, r0
  and #0x1, r0
  mov r0, r13
# ROTCR    Rn (LSB = 1, T = 0)
  # set T = 0
  stc SR, r0
  mov.l .L1+4, r1
  and r1, r0
  ldc r0, SR
  # rotate
  mov.l .L1, r14
  rotcr r14
  stc SR, r0
  and #0x1, r0
  mov r0, r15
# ROTCR    Rn (LSB = 0, T = 1)
  # set T = 1
  stc SR, r0
  or #1, r0
  ldc r0, SR
  # rotate
  mov.l .L1+4, r1
  rotcr r1
  stc SR, r0
  and #0x1, r0
  rts
  nop
  .align 4
.L1:
  .long 0x7fffffff
  .long 0xfffffffe
  .align 4
.L2:
  .long .L1

# REGISTER_OUT r2  0xfffffffe
# REGISTER_OUT r3  0
# REGISTER_OUT r4  0xfffffffd
# REGISTER_OUT r5  1
# REGISTER_OUT r6  0x7fffffff
# REGISTER_OUT r7  0
# REGISTER_OUT r8  0xbfffffff
# REGISTER_OUT r9  1
# REGISTER_OUT r10 0xfffffffc
# REGISTER_OUT r11 1
# REGISTER_OUT r12 0xffffffff
# REGISTER_OUT r13 0
# REGISTER_OUT r14 0x3fffffff
# REGISTER_OUT r15 1
# REGISTER_OUT r1  0xffffffff
# REGISTER_OUT r0  0
