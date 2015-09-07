test_mova:
  mova .L1, r0
  mov.l @r0, r1
  rts
  nop

.align 4
.L1:
  .long -24

# REGISTER_OUT r1 -24
