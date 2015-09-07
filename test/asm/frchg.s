test_frchg:
  # REGISTER_IN fr0 0x41500000
  sts fpscr, r1
  # swap fp banks and move fr0 to r3
  frchg
  sts fpscr, r2
  mov.l .L2, r0
  fmov.s fr0, @r0
  mov.l @r0, r3
  # swap again and move fr0 to r4
  frchg
  fmov.s fr0, @r0
  mov.l @r0, r4
  rts 
  nop
  # REGISTER_OUT r1 0x00040001
  # REGISTER_OUT r2 0x00240001
  # REGISTER_OUT r3 0x0
  # REGISTER_OUT r4 0x41500000

.align 4
.L1:
  .long 0x0
  .align 4
.L2:
  .long .L1
