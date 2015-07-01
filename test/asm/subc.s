# REGISTER_IN r1 1
# REGISTER_IN r2 1
# REGISTER_IN r3 0
# REGISTER_IN r4 1
# REGISTER_IN r5 0
# REGISTER_IN r6 1

  .little
  .text
  .global start
start:
# r1 - r2 - T(0)
  subc r2, r1
  stc SR, r0
  and #0x1, r0
  mov r0, r2
# r3 - r4 - T(0)
  subc r4, r3
  stc SR, r0
  and #0x1, r0
  mov r0, r4
# r5 - r6 - T(1)
  subc r6, r5
  stc SR, r0
  and #0x1, r0
  mov r0, r6
  rts
  nop

# REGISTER_OUT r1 0x0
# REGISTER_OUT r2 0
# REGISTER_OUT r3 0xffffffff
# REGISTER_OUT r4 1
# REGISTER_OUT r5 0xfffffffe
# REGISTER_OUT r6 1
