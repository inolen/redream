  .text
  .global start
start:
  sts fpscr, r0
  fschg
  sts fpscr, r1
  rts 
  nop

# REGISTER_OUT r0 0x00040001
# REGISTER_OUT r1 0x00140001
