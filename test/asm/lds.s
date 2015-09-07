test_lds_sts_mach:
  # REGISTER_IN r0 13
  lds r0, mach
  sts mach, r1
  rts
  nop
  # REGISTER_OUT r1 13

test_lds_sts_macl:
  # REGISTER_IN r0 13
  lds r0, macl
  sts macl, r1
  rts
  nop
  # REGISTER_OUT r1 13

test_lds_sts_pr:
  # REGISTER_IN r0 13
  sts pr, r2
  lds r0, pr
  sts pr, r1
  # restore
  lds r2, pr
  rts
  nop
  # REGISTER_OUT r1 13

test_lds_sts_fpscr:
  # REGISTER_IN r0 0xffd40001
  lds r0, fpscr
  sts fpscr, r1
  rts
  nop
  # REGISTER_OUT r1 0x00140001

test_lds_sts_fpul:
  # REGISTER_IN r0 13
  lds r0, fpul
  sts fpul, r1
  rts 
  nop
  # REGISTER_OUT r1 13
