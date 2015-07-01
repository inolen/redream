  .text
  .global start
start:
  # SHLD    Rm,Rn (-2145910784 >> 20) == -2047
  mov.l .CONST, r1
  mov.l .CONST+8, r2
  shld r1, r2
  # SHLD    Rm,Rn (-2145910784 >> 32)
  mov.l .CONST+4, r1
  mov.l .CONST+8, r3
  shld r1, r3
  # SHLD    Rm,Rn (-2047 << 20) == -2145910784
  mov.l .CONST+12, r1
  mov.l .CONST+16, r4
  shld r1, r4
  # SHLL    Rn
  mov.l .CONST+20, r5
  shll r5
  stc SR, r0
  and #0x1, r0
  mov r0, r6
  # SHLR    Rn
  mov.l .CONST+20, r7
  shlr r7
  stc SR, r0
  and #0x1, r0
  mov r0, r8
  rts 
  nop
  .align 4
.CONST:
  .long 0xffffffec /* -20 */
  .long 0xffffffe0 /* -32 */
  .long 0x80180000 /* -2145910784 */
  .long 0x00000014 /* 20 */
  .long 0xfffff801 /* -2047 */
  .long 0x80000001 /* -2147483647 */

# REGISTER_OUT r2 0x00000801
# REGISTER_OUT r3 0x0
# REGISTER_OUT r4 0x80100000
# REGISTER_OUT r5 0x00000002
# REGISTER_OUT r6 1
# REGISTER_OUT r7 0x40000000
# REGISTER_OUT r8 1
