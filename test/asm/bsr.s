  .little
  .text
  .global start
start:
# BSR      label
  sts.l pr, @-r15
  bsr _addnine
  add #1, r0
  add #3, r0
  lds.l @r15+, pr
  rts
  nop
_dontgohere:
  add #2, r0
  rts
  nop
_addnine:
  add #9, r0
  rts
  nop

# REGISTER_OUT r0 13
