test_dmuls:
  # REGISTER_IN r0 0xfffffffe
  # REGISTER_IN r1 0x00005555
  dmuls.l r0, r1
  sts MACH, r0
  sts MACL, r1
  rts 
  nop
  # REGISTER_OUT r0 0xffffffff
  # REGISTER_OUT r1 0xffff5556

test_dmulu:
  # REGISTER_IN r0 0xfffffffe
  # REGISTER_IN r1 0x00005555
  dmulu.l r0, r1
  sts MACH, r0
  sts MACL, r1
  rts 
  nop
  # REGISTER_OUT r0 0x00005554
  # REGISTER_OUT r1 0xffff5556

