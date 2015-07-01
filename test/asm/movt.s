# REGISTER_IN r0 3

  .little
  .text
  .global start
start:
  cmp/eq #3, r0
  movt r1
  cmp/eq #5, r0
  movt r2
  rts
  nop

# REGISTER_OUT r1 1
# REGISTER_OUT r2 0
