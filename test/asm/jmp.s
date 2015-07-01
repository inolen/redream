# REGISTER_IN r1 0

  .little
  .text
  .global start
start:
  mov.l .L3, r0
  jmp @r0
  nop
  mov #1, r1
  .align 1
  .global _foobar
_foobar:
  rts
  add #13, r1
.L3:
  .align 2
  .long _foobar

# REGISTER_OUT r1 13
