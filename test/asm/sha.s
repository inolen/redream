test_shad:
  # REGISTER_IN r1 0xffffffe0
  # REGISTER_IN r2 0x80180000
  shad r1, r2
  rts
  nop
  # REGISTER_OUT r2 0xffffffff

# TODO ADD MORE TESTS
