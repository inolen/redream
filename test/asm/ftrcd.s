# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr0   0xc012cccccccccccd

  .text
  .global start
start:
# FTRC DRm,FPUL
  ftrc dr0, fpul
  sts fpul, r0
  rts
  nop

# REGISTER_OUT r0 0xfffffffc
