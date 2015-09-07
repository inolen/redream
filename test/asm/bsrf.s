test_bsrf:
  # REGISTER_IN r0 8
  sts.l pr, @-r15
  bsrf r0
  add #1, r1
  add #3, r1
  lds.l @r15+, pr
  rts
  nop
_addnine:
  add #9, r1
  rts
  nop
  # REGISTER_OUT r1 13
