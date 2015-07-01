#        l r sum
# ---------------------------
#        0 0 0
#        0 0 1
#        0 1 0
# *OVER* 0 1 1 (subtracting a negative is the same as adding a positive)
# *OVER* 1 0 0 (subtracting a positive is the same as adding a negative)
#        1 0 1
#        1 1 0
#        1 1 1
  .little
  .text
  .global start
start:
# 0x7fffffff(0) - 0x7ffffffe(0) = 0x00000001(0)
  mov.l .L4, r0
  mov.l .L3, r1
  subv r0, r1
  stc SR, r0
  and #0x1, r0
  mov r0, r2
# 0x7ffffffe(0) - 0x7fffffff(0) = 0xffffffff(1)
  mov.l .L3, r0
  mov.l .L4, r3
  subv r0, r3
  stc SR, r0
  and #0x1, r0
  mov r0, r4
# 0x7ffffffe(0) - 0xffffffff(1) = 0x7fffffff(1)
  mov #-1, r0
  mov.l .L4, r5
  subv r0, r5
  stc SR, r0
  and #0x1, r0
  mov r0, r6
# 0x7fffffff(0) - 0xfffffffff(1) = 0x80000000(1), OVERFLOWED
  mov #-1, r0
  mov.l .L3, r7
  subv r0, r7
  stc SR, r0
  and #0x1, r0
  mov r0, r8
# 0x80000000(1) - 0x00000001(0) = 0x7fffffff(0), OVERFLOWED
  mov #0x1, r0
  mov.l .L2, r9
  subv r0, r9
  stc SR, r0
  and #0x1, r0
  mov r0, r10
# 0x80000001(1) - 0x00000001(0) = 0x80000000(1)
  mov #0x1, r0
  mov.l .L1, r11
  subv r0, r11
  stc SR, r0
  and #0x1, r0
  mov r0, r12
# 0x80000001(1) - 0x80000000(1) = 0x00000001(0)
  mov.l .L2, r0
  mov.l .L1, r13
  subv r0, r13
  stc SR, r0
  and #0x1, r0
  mov r0, r14
# 0x80000000(1) - 0x80000001(1) = 0xffffffff(1)
  mov.l .L1, r0
  mov.l .L2, r15
  subv r0, r15
  stc SR, r0
  and #0x1, r0

  rts
  nop
  .align 4
.L1:
  .long 0x80000001
  .align 4
.L2:
  .long 0x80000000
  .align 4
.L3:
  .long 0x7fffffff
  .align 4
.L4:
  .long 0x7ffffffe

# REGISTER_OUT r1  0x00000001
# REGISTER_OUT r2  0
# REGISTER_OUT r3  0xffffffff
# REGISTER_OUT r4  0
# REGISTER_OUT r5  0x7fffffff
# REGISTER_OUT r6  0
# REGISTER_OUT r7  0x80000000
# REGISTER_OUT r8  1
# REGISTER_OUT r9  0x7fffffff
# REGISTER_OUT r10 1
# REGISTER_OUT r11 0x80000000
# REGISTER_OUT r12 0
# REGISTER_OUT r13 0x00000001
# REGISTER_OUT r14 0
# REGISTER_OUT r15 0xffffffff
# REGISTER_OUT r0  0
