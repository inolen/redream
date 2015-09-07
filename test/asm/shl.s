test_shld_left:
  # REGISTER_IN r0 0x00008018
  # REGISTER_IN r1 16
  shld r1, r0
  rts
  nop
  # REGISTER_OUT r0 0x80180000

test_shld_right:
  # REGISTER_IN r0 0x80180000
  # REGISTER_IN r1 -16
  shld r1, r0
  rts
  nop
  # REGISTER_OUT r0 0x00008018

test_shld_right_overflow:
  # REGISTER_IN r0 0x80180000
  # REGISTER_IN r1 -32
  shld r1, r0
  rts
  nop
  # REGISTER_OUT r0 0x0

test_shal:
  # REGISTER_IN r0 0x80000001
  shal r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x2
  # REGISTER_OUT r1 1

test_shll:
  # REGISTER_IN r0 0x80000001
  shll r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x2
  # REGISTER_OUT r1 1

test_shlr:
  # REGISTER_IN r0 0x80000001
  shlr r0
  movt r1
  rts 
  nop
  # REGISTER_OUT r0 0x40000000
  # REGISTER_OUT r1 1

test_shll2:
  # REGISTER_IN r0 0x1
  shll2 r0
  rts
  nop
  # REGISTER_OUT 0x4

test_shlr2:
  # REGISTER_IN r0 0x4
  shlr2 r0
  rts
  nop
  # REGISTER_OUT 0x1

test_shll8:
  # REGISTER_IN r0 0x1
  shll8 r0
  rts
  nop
  # REGISTER_OUT 0x100

test_shlr8:
  # REGISTER_IN r0 0x100
  shlr8 r0
  rts
  nop
  # REGISTER_OUT 0x1

test_shll16:
  # REGISTER_IN r0 0x1
  shll16 r0
  rts
  nop
  # REGISTER_OUT 0x10000

test_shlr16:
  # REGISTER_IN r0 0x10000
  shlr16 r0
  rts
  nop
  # REGISTER_OUT 0x1
