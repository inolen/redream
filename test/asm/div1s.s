# REGISTER_IN r2 0x2710
# REGISTER_IN r3 0x8012
# REGISTER_IN r4 0xd8f0
# REGISTER_IN r5 0x7fee
# REGISTER_IN r6 0x2710
# REGISTER_IN r7 0xf0000010
# REGISTER_IN r8 0xffffd8f0
# REGISTER_IN r9 0x0ffffff0

  .text
  .global start
start:
# r3 (negative, 16 bits) / r2 (16 bits) = r3 (16 bits)
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
# r5 (16 bits) / r4 (negative, 16 bits) = r5 (16 bits)
  shll16 r4
  exts.w r5, r5
  xor r0, r0
  mov r5, r1
  rotcl r1
  subc r0, r5
  div0s r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  div1 r4, r5
  exts.w r5, r5
  rotcl r5
  addc r0, r5
  exts.w r5, r5
# r7 (negative, 32 bits) / r6 (32 bits) = r7 (32 bits)
  mov r7, r0
  rotcl r0
  subc r1, r1
  xor r0, r0
  subc r0, r7
  div0s r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  div1 r6, r1
  rotcl r7
  addc r0, r7
# r9 (32 bits) / r8 (negative, 32 bits) = r9 (32 bits)
  mov r9, r0
  rotcl r0
  subc r1, r1
  xor r0, r0
  subc r0, r9
  div0s r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  div1 r8, r1
  rotcl r9
  addc r0, r9
  rts
  nop

# REGISTER_OUT r3 0xfffffffd
# REGISTER_OUT r5 0xfffffffd
# REGISTER_OUT r7 0xffff9725
# REGISTER_OUT r9 0xffff9725
