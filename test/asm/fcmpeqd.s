# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr0   0x4014000000000000
# REGISTER_IN dr2   0xc018000000000000
# REGISTER_IN dr4   0xc018000000000000

  .text
  .global start
start:
# FCMP/EQ DRm,DRn
  fcmp/eq dr0, dr2
  movt r0
  fcmp/eq dr2, dr4
  movt r1
  rts 
  nop

# REGISTER_OUT r0 0
# REGISTER_OUT r1 1
