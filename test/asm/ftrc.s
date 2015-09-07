test_ftrcd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0xc012cccccccccccd
  ftrc dr0, fpul
  sts fpul, r0
  rts
  nop
  # REGISTER_OUT r0 0xfffffffc

test_ftrcf:
  # REGISTER_IN fr0 0xc0966666
  ftrc fr0, fpul
  sts fpul, r0
  rts
  nop
  # REGISTER_OUT r0 0xfffffffc

