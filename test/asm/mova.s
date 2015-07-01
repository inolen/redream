  .little
  .text
  .global start
start:
# MOVA   @(disp8,PC),R0
  mova .L1, r0
  mov.l @r0, r2
  rts
  nop
  .align 4
.L1:
  .long -24

# REGISTER_OUT r2 -24
