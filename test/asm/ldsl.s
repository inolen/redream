test_ldsl_stsl_mach:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  mov.l r0, @r1
  lds.l @r1+, mach
  sts.l mach, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts 
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

test_ldsl_stsl_macl:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  mov.l r0, @r1
  lds.l @r1+, macl
  sts.l macl, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts 
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

test_ldsl_stsl_pr:
  # REGISTER_IN r0 13
  sts pr, r5
  mov.l .DATA_ADDR, r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  mov.l r0, @r1
  lds.l @r1+, pr
  sts.l pr, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  # restore pr
  lds r5, pr
  rts
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

test_ldsl_stsl_fpscr:
  # REGISTER_IN r0 0xffd40001
  mov.l .DATA_ADDR, r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  mov.l r0, @r1
  lds.l @r1+, fpscr
  sts.l fpscr, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts 
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 0x00140001

test_ldsl_stsl_fpul:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  mov.l r0, @r1
  lds.l @r1+, fpul
  sts.l fpul, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts 
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

.align 4
.DATA:
  .long 0
  .long 0

.align 4
.DATA_ADDR:
  .long .DATA
