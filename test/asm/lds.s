# REGISTER_IN r0 13
# REGISTER_IN r1 0xffd40001

  .text
  .global start
start:
# LDS     Rm,MACH
# STS     MACH,Rn
  lds r0, mach
  sts mach, r2
# LDS     Rm,MACL
# STS     MACL,Rn
  lds r0, macl
  sts macl, r3
# LDS     Rm,PR
# STS     PR,Rn
  sts pr, r5
  lds r0, pr
  sts pr, r4
  # restore
  lds r5, pr
# LDS     Rm,FPSCR
# STS     FPSCR,Rn
  lds r1, fpscr
  sts fpscr, r5
# LDS     Rm,FPUL
# STS     FPUL,Rn
  lds r0, fpul
  sts fpul, r6
  rts 
  nop

# REGISTER_OUT r2 13
# REGISTER_OUT r3 13
# REGISTER_OUT r4 13
# REGISTER_OUT r5 0x00140001
# REGISTER_OUT r6 13
