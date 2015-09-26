test_div1u_32_16:
  # REGISTER_IN r0 0x00002710
  # REGISTER_IN r1 0x0ffffff0
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
  rts
  nop
  # REGISTER_OUT r1 0x68db

test_div1u_32:
  # REGISTER_IN r0 0x00002710
  # REGISTER_IN r1 0x00000000
  # REGISTER_IN r2 0x2a05f207
  # r1:r2 (64 bits) / r0 (32 bits) = r2 (32 bits), remainder in r1
  # t gets cleared to 0 here, and after 33 rotcl it should
  # be the same at the end as well
  div0u
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  div1 r0, r1
  rotcl r2
  movt r4
  rts
  nop
  # REGISTER_OUT r1 0x00000a97
  # REGISTER_OUT r2 0x00011367
  # REGISTER_OUT r4 0x0
