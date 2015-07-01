# REGISTER_IN fr0 0xc0966666

  .text
  .global start
start:
# FTRC FRm,FPUL
  ftrc fr0, fpul
  sts fpul, r0
  rts
  nop

# REGISTER_OUT r0 0xfffffffc
