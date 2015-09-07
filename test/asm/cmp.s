test_cmpeq_imm:
  # REGISTER_IN r0 13
  cmp/eq #13, r0
  movt r1
  cmp/eq #17, r0
  movt r2
  rts
  nop
  # REGISTER_OUT r1 1
  # REGISTER_OUT r2 0

test_cmpeq:
  # REGISTER_IN r0 13
  # REGISTER_IN r1 17
  cmp/eq r0, r0
  movt r2
  cmp/eq r0, r2
  movt r4
  rts
  nop
  # REGISTER_OUT r2 1
  # REGISTER_OUT r3 0

test_cmphs:
  # REGISTER_IN r0 -1
  # REGISTER_IN r1 13
  cmp/hs r1, r0
  movt r2
  cmp/hs r1, r1
  movt r3
  cmp/hs r0, r1
  movt r4
  rts
  nop
  # REGISTER_OUT r2 1
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 0

test_cmpge:
  # REGISTER_IN r0 -1
  # REGISTER_IN r1 13
  cmp/ge r1, r0
  movt r2
  cmp/ge r1, r1
  movt r3
  cmp/ge r0, r1
  movt r4
  rts
  nop
  # REGISTER_OUT r2 0
  # REGISTER_OUT r3 1
  # REGISTER_OUT r4 1

test_cmphi:
  # REGISTER_IN r0 -1
  # REGISTER_IN r1 13
  cmp/hi r1, r0
  movt r2
  cmp/hi r1, r1
  movt r3
  cmp/hi r0, r1
  movt r4
  rts
  nop
  # REGISTER_OUT r2 1
  # REGISTER_OUT r3 0
  # REGISTER_OUT r4 0

test_cmpgt:
  # REGISTER_IN r0 -1
  # REGISTER_IN r1 13
  cmp/gt r1, r0
  movt r2
  cmp/gt r1, r1
  movt r3
  cmp/gt r0, r1
  movt r4
  rts
  nop
  # REGISTER_OUT r2 0
  # REGISTER_OUT r3 0
  # REGISTER_OUT r4 1

test_cmppz:
  # REGISTER_IN r0 -1
  # REGISTER_IN r1 0
  # REGISTER_IN r2 1
  cmp/pz r0
  movt r3
  cmp/pz r1
  movt r4
  cmp/pz r2
  movt r5
  rts
  nop
  # REGISTER_OUT r3 0
  # REGISTER_OUT r4 1
  # REGISTER_OUT r5 1

test_cmppl:
  # REGISTER_IN r0 -1
  # REGISTER_IN r1 0
  # REGISTER_IN r2 1
  cmp/pl r0
  movt r3
  cmp/pl r1
  movt r4
  cmp/pl r2
  movt r5
  rts
  nop
  # REGISTER_OUT r3 0
  # REGISTER_OUT r4 0
  # REGISTER_OUT r5 1

test_cmpstr:
  # REGISTER_IN r0 0x00000000
  # REGISTER_IN r1 0xffffffff
  # REGISTER_IN r2 0x00f00000
  # REGISTER_IN r3 0x00ff0000
  cmp/str r0, r1
  movt r4
  cmp/str r2, r1
  movt r5
  cmp/str r3, r1
  movt r6
  rts
  nop
  # REGISTER_OUT r4 0
  # REGISTER_OUT r5 0
  # REGISTER_OUT r6 1
