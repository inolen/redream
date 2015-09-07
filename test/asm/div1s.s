test_div1s_16_ndividend:
  # REGISTER_IN r2 0x2710
  # REGISTER_IN r3 0x8012
  # dividend is r3, divisor is r2
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
  # dividend is r3, divisor is r2
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
  # REGISTER_IN r2 0x2710
  # REGISTER_IN r3 0xf0000010
  # dividend is r3, divisor is r2
  mov r3, r0
  rotcl r0
  subc r1, r1
  xor r0, r0
  subc r0, r3
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
  addc r0, r3
  rts
  nop
  # REGISTER_OUT r3 0xffff9725

test_div1s_32_ndivisor:
  # REGISTER_IN r2 0xffffd8f0
  # REGISTER_IN r3 0x0ffffff0
  # dividend is r3, divisor is r2
  mov r3, r0
  rotcl r0
  subc r1, r1
  xor r0, r0
  subc r0, r3
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
  addc r0, r3
  rts
  nop
  # REGISTER_OUT r3 0xffff9725
