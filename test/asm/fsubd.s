# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr0   0xc01c000000000000
# REGISTER_IN dr2   0xc010000000000000

  .text
  .global start
start:
# FSUB DRm,DRn
  fsub dr0, dr2
  rts 
  nop

# REGISTER_OUT dr2 0x4008000000000000
