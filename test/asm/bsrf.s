# REGISTER_IN r0 14

  .little
  .text
  .global start
start:
# BSRF     Rn
  sts.l pr, @-r15
  bsrf r0
  add #1, r1
  add #3, r1
  lds.l @r15+, pr
  rts
  nop
_dontgohere:
  add #2, r1
  rts
  nop
_addnine:
  add #9, r1
  rts
  nop

# REGISTER_OUT r1 13
