# REGISTER_IN r0 0xfffffff0

  .little
  .text
  .global start
start:
  swap.b r0, r1
  swap.w r0, r2
  mov r2, r3
  xtrct r1, r3
  rts
  nop

# REGISTER_OUT r1 0xfffff0ff
# REGISTER_OUT r2 0xfff0ffff
# REGISTER_OUT r3 0xf0fffff0
