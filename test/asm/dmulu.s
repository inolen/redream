# REGISTER_IN r0 0xfffffffe
# REGISTER_IN r1 0x00005555

  .text
  .global start
start:
  dmulu.l r0, r1
  sts MACH, r0
  sts MACL, r1
  rts 
  nop

# REGISTER_OUT r0 0x00005554
# REGISTER_OUT r1 0xffff5556
