# REGISTER_IN r1 0xffffffe0
# REGISTER_IN r2 0x80180000

  .text
  .global start
start:
  # SHAD    Rm,Rn (-
  shad r1, r2
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

# REGISTER_OUT r2 0xffffffff
