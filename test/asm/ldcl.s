test_ldcl_stcl_sr:
  # write control value before swap
  mov #13, r2
  # swap banks
  mov.l .ALT_SR, r0
  ldc.l @r0+, sr
  # overwrite control value to test swap
  mov #99, r2
  # write sr to mem
  mov.l .DATA_ADDR, r1
  add #4, r1
  stc.l sr, @-r1
  # swap back again
  mov.l .DEFAULT_SR, r3
  ldc.l @r3+, sr
  # read result from mem
  mov.l .DATA_ADDR, r0
  mov.l @r0, r3
  # r0 in alt bank should have been post-incremented by 4
  mov.l .ALT_SR, r1
  stc r0_bank, r4
  # r1 in alt bank should have been pre-decremented by 4
  mov.l .DATA_ADDR, r1
  stc r1_bank, r5
  sub r1, r5
  rts
  nop
  # REGISTER_OUT r2 13
  # REGISTER_OUT r3 0x500000f0
  # REGISTER_OUT r4 4
  # REGISTER_OUT r5 0

test_ldcl_stcl_rbank:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l r0, @r1
  ldc.l @r1+, r3_bank
  mov #99, r3
  mov.l .DATA_ADDR, r2
  add #8, r2
  stc.l r3_bank, @-r2
  cmp/eq r1, r2
  movt r4
  mov.l @r2, r5
  rts
  nop
  # REGISTER_OUT r4 1
  # REGISTER_OUT r5 13

test_ldcl_stcl_gbr:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l r0, @r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  ldc.l @r1+, gbr
  stc.l gbr, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

test_ldcl_stcl_vbr:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l r0, @r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  ldc.l @r1+, vbr
  stc.l vbr, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

test_ldcl_stcl_ssr:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l r0, @r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  ldc.l @r1+, ssr
  stc.l ssr, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

test_ldcl_stcl_spc:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l r0, @r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  ldc.l @r1+, spc
  stc.l spc, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

# TODO
# STC.L   SGR,@-Rn

test_ldcl_stcl_dbr:
  # REGISTER_IN r0 13
  mov.l .DATA_ADDR, r1
  mov.l r0, @r1
  mov.l .DATA_ADDR, r2
  add #8, r2
  ldc.l @r1+, dbr
  stc.l dbr, @-r2
  cmp/eq r2, r1
  movt r3
  mov.l @r2, r4
  rts
  nop
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 13

.align 4
.DATA:
  .long 0x500000f0
  .long 0x700000f0
  .long 0x0
  .long 0x0

.align 4
.DATA_ADDR:
  .long .DATA+8

.align 4
.ALT_SR:
  .long .DATA

.align 4
.DEFAULT_SR:
  .long .DATA+4
