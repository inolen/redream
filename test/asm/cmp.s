  .text
  .global start
start:
# CMP/EQ #imm,R0
  mov #0, r2
  mov #13, r0
  cmp/eq #13, r0  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r2
  mov #13, r0
  cmp/eq #17, r0  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r2
# CMP/EQ  Rm,Rn
  mov #0, r3
  mov #13, r1
  mov #13, r0
  cmp/eq r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r3
  mov #17, r1
  mov #13, r0
  cmp/eq r0, r1  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r3
# CMP/HS  Rm,Rn
  mov #0, r4
  mov #-1, r1
  mov #13, r0
  cmp/hs r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r4
  mov #13, r1
  mov #13, r0
  cmp/hs r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r4
  mov #14, r1
  mov #13, r0
  cmp/hs r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r4
# CMP/GE  Rm,Rn
  mov #0, r5
  mov #-1, r1
  mov #13, r0
  cmp/ge r0, r1  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r5
  mov #13, r1
  mov #13, r0
  cmp/ge r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r5
  mov #14, r1
  mov #13, r0
  cmp/ge r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r5
# CMP/HI  Rm,Rn
  mov #0, r6
  mov #-1, r1
  mov #13, r0
  cmp/hi r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r6
  mov #13, r1
  mov #13, r0
  cmp/hi r0, r1  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r6
  mov #14, r1
  mov #13, r0
  cmp/hi r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r6
# CMP/GT  Rm,Rn
  mov #0, r7
  mov #-1, r1
  mov #13, r0
  cmp/gt r0, r1  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r7
  mov #13, r1
  mov #13, r0
  cmp/gt r0, r1  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r7
  mov #14, r1
  mov #13, r0
  cmp/gt r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r7
# CMP/PZ  Rn
  mov #0, r8
  mov #-13, r0
  cmp/pz r0  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r8
  mov #0, r0
  cmp/pz r0  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r8
  mov #13, r0
  cmp/pz r0  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r8
# CMP/PL  Rn
  mov #0, r9
  mov #-13, r0
  cmp/pl r0  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r9
  mov #0, r0
  cmp/pl r0  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r9
  mov #13, r0
  cmp/pl r0  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r9
# CMP/STR  Rm,Rn
  mov #0, r10
  mov #-1, r1
  mov #0, r0
  cmp/str r0, r1  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r10
  mov #-1, r1
  mov.l .L1, r0
  cmp/str r0, r1  /* false */
  stc SR, r0
  and #0x1, r0
  add r0, r10
  mov #-1, r1
  mov.l .L2, r0
  cmp/str r0, r1  /* true */
  stc SR, r0
  and #0x1, r0
  add r0, r10
  rts 
  nop
  .align 4
.L1:
  .long 0x00f00000
  .align 4
.L2:
  .long 0x00ff0000

# REGISTER_OUT r2  1
# REGISTER_OUT r3  1
# REGISTER_OUT r4  3
# REGISTER_OUT r5  2
# REGISTER_OUT r6  2
# REGISTER_OUT r7  1
# REGISTER_OUT r8  2
# REGISTER_OUT r9  1
# REGISTER_OUT r10 1
