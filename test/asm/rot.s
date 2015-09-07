test_rotl_msb0:
  # REGISTER_IN r0 0x7fffffff
  rotl r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xfffffffe
  # REGISTER_OUT r1 0

test_rotl_msb1:
  # REGISTER_IN r0 0xfffffffe
  rotl r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xfffffffd
  # REGISTER_OUT r1 1

test_rotcl_t0_msb1:
  # REGISTER_IN r0 0xfffffffe
  rotcl r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xfffffffc
  # REGISTER_OUT r1 1

test_rotcl_t1_msb0:
  # REGISTER_IN r0 0x7fffffff
  sett
  rotcl r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xffffffff
  # REGISTER_OUT r1 0

test_rotr_lsb0:
  # REGISTER_IN r0 0xfffffffe
  rotr r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x7fffffff
  # REGISTER_OUT r1 0

test_rotr_lsb1:
  # REGISTER_IN r0 0x7fffffff
  rotr r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xbfffffff
  # REGISTER_OUT r1 1

test_rotcr_t0_lsb1:
  # REGISTER_IN r0 0x7fffffff
  rotcr r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x3fffffff
  # REGISTER_OUT r1 1

test_rotcr_t1_lsb0:
  # ROTCR    Rn (LSB = 0, T = 1)
  # REGISTER_IN r0 0xfffffffe
  sett
  rotcr r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xffffffff
  # REGISTER_OUT r1 0
