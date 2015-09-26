test_div1s_16_ndividend:
  # REGISTER_IN r2 0x2710
  # REGISTER_IN r3 0x8012
  # r3 (16 bits) / r2 (16 bits) = r3 (16 bits)
  shll16 r2
  exts.w r3, r3
  xor r0, r0
  mov r3, r1
  rotcl r1
  subc r0, r3
  div0s r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  exts.w r3, r3
  rotcl r3
  addc r0, r3
  exts.w r3, r3
  rts
  nop
  # REGISTER_OUT r3 0xfffffffd

test_div1s_16_ndivisor:
  # REGISTER_IN r2 0xd8f0
  # REGISTER_IN r3 0x7fee
  # r3 (16 bits) / r2 (16 bits) = r3 (16 bits)
  shll16 r2
  exts.w r3, r3
  xor r0, r0
  mov r3, r1
  rotcl r1
  subc r0, r3
  div0s r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  div1 r2, r3
  exts.w r3, r3
  rotcl r3
  addc r0, r3
  exts.w r3, r3
  rts
  nop
  # REGISTER_OUT r3 0xfffffffd

test_div1s_32_ndividend:
  # REGISTER_IN r2 0x00002710
  # REGISTER_IN r1 0xffffffff
  # REGISTER_IN r3 0xf0000010
  # r1:r3 (64 bits) / r2 (32 bits) = r3 (32 bits), remainder in r1
  # t gets set to 1 here (r1 msb ^ r2 msb), and after 33 rotcl it should
  # be the same at the end as well
  div0s r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  movt r4
  add r4, r3
  rts
  nop
  # REGISTER_OUT r1 0xffffeac0
  # REGISTER_OUT r3 0xffff9725
  # REGISTER_OUT r4 0x1


test_div1s_32_ndivisor:
  # REGISTER_IN r2 0xffffd8f0
  # REGISTER_IN r1 0x00000000
  # REGISTER_IN r3 0x0ffffff0
  # r1:r3 (64 bits) / r2 (32 bits) = r3 (32 bits), remainder in r1
  # t gets set to 1 here (r1 msb ^ r2 msb), and after 33 rotcl it should
  # be the same at the end as well
  div0s r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  movt r4
  add r4, r3
  rts
  nop
  # REGISTER_OUT r1 0x00001540
  # REGISTER_OUT r3 0xffff9725
  # REGISTER_OUT r4 0x1


test_div1s_32:
  # REGISTER_IN r2 0x00002710
  # REGISTER_IN r1 0x0000000
  # REGISTER_IN r3 0x0ffffff0
  # r1:r3 (64 bits) / r2 (32 bits) = r3 (32 bits), remainder in r1
  # t gets set to 0 here (r1 msb ^ r2 msb), and after 33 rotcl it should
  # be the same at the end as well
  div0s r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  div1 r2, r1
  rotcl r3
  movt r4
  add r4, r3
  rts
  nop
  # REGISTER_OUT r1 0x00001540
  # REGISTER_OUT r3 0x000068db
  # REGISTER_OUT r4 0x0
