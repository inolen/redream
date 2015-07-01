# REGISTER_IN r1 0

  .little
  .text
  .global start
start:
  sts.l pr, @-r15
  mov.l .L3, r0
  jsr @r0
  add #1, r1
  add #3, r1
  lds.l @r15+, pr
  rts
  nop
  .align 1
  .global _foobar
_foobar:
  rts
  add #9, r1
.L3:
  .align 2
  .long _foobar

# REGISTER_OUT r1 13
