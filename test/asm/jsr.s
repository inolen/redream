test_jsr:
  # REGISTER_IN r1 0
  sts.l pr, @-r15
  mov.l .L1, r0
  jsr @r0
  add #1, r1
  add #3, r1
  lds.l @r15+, pr
  rts
  nop
_foobar:
  rts
  add #9, r1
  # REGISTER_OUT r1 13

.align 4
.L1:
  .long _foobar
