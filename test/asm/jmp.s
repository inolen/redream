test_jmp:
  # REGISTER_IN r1 0
  mov.l .L1, r0
  jmp @r0
  nop
  rts
  nop
_foobar:
  rts
  mov #13, r1
  # REGISTER_OUT r1 13

.align 4
.L1:
  .long _foobar
