# REGISTER_IN r0 0x2710
# REGISTER_IN r1 0x0ffffff0
# REGISTER_IN r2 0x2710
# REGISTER_IN r3 0x00000001
# REGISTER_IN r4 0x2a05f200

  .text
  .global start
start:
# r1 (32 bits) / r0 (16 bits) = r1 (16 bits)
  shll16 r0
  div0u
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  div1 r0, r1
  rotcl r1
  extu.w r1, r1
# r3:r4 (64 bits) / r2 (32 bits) = R4 (32 bits)
  div0u
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  div1 r2, r3
  rotcl r4
  rts
  nop

# REGISTER_OUT r1 0x68db
# REGISTER_OUT r4 0x7a120
