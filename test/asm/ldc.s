test_ldc_stc_sr:
  # write control value before swap
  mov #13, r2
  # swap banks
  mov.l .ALT_SR, r0
  ldc r0, sr
  # overwrite control value to test swap
  mov #99, r2
  # write sr to mem
  stc sr, r1
  mov.l .DATA_ADDR, r0
  mov.l r1, @r0
  # swap back again
  mov.l .DEFAULT_SR, r0
  ldc r0, sr
  # read sr from mem
  mov.l .DATA_ADDR, r0
  mov.l @r0, r3
  rts
  nop
  # REGISTER_OUT r2 13
  # REGISTER_OUT r3 0x500000f0

test_ldc_stc_rbank:
  # REGISTER_IN r0 13
  ldc r0, r1_bank
  mov #99, r1
  stc r1_bank, r1
  rts
  nop
  # REGISTER_OUT r1 13

test_ldc_stc_gbr:
  # REGISTER_IN r0 13
  ldc r0, gbr
  stc gbr, r1
  rts
  nop
  # REGISTER_OUT r1 13

test_ldc_stc_vbr:
  # REGISTER_IN r0 13
  ldc r0, vbr
  stc vbr, r1
  rts
  nop
  # REGISTER_OUT r1 13

test_ldc_stc_ssr:
  # REGISTER_IN r0 13
  ldc r0, ssr
  stc ssr, r1
  rts
  nop
  # REGISTER_OUT r1 13

test_ldc_stc_spc:
  # REGISTER_IN r0 13
  ldc r0, spc
  stc spc, r1
  rts
  nop
  # REGISTER_OUT r1 13

# TODO
# STC     SGR,Rn
#  mov r0, r15
#  trapa
#  stc sgr, r9

test_ldc_stc_dbr:
  # REGISTER_IN r0 13
  ldc r0, dbr
  stc dbr, r1
  rts
  nop
  # REGISTER_OUT r1 13

.align 4
.DATA:
  .long 0x0

.align 4
.DATA_ADDR:
  .long .DATA

.align 4
.ALT_SR:
  .long 0x500000f0

.align 4
.DEFAULT_SR:
  .long 0x700000f0
