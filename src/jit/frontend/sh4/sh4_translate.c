#include "jit/frontend/sh4/sh4_translate.h"
#include "core/core.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

static struct ir_value *load_sr(struct ir *ir) {
  struct ir_value *sr =
      ir_load_context(ir, offsetof(struct sh4_context, sr), VALUE_I32);

  /* inlined version of sh4_implode_sr */
  sr = ir_and(ir, sr, ir_alloc_i32(ir, ~(M_MASK | Q_MASK | S_MASK | T_MASK)));

  struct ir_value *sr_t =
      ir_load_context(ir, offsetof(struct sh4_context, sr_t), VALUE_I32);
  sr = ir_or(ir, sr, sr_t);

  struct ir_value *sr_s =
      ir_load_context(ir, offsetof(struct sh4_context, sr_s), VALUE_I32);
  sr = ir_or(ir, sr, ir_shli(ir, sr_s, S_BIT));

  struct ir_value *sr_m =
      ir_load_context(ir, offsetof(struct sh4_context, sr_m), VALUE_I32);
  sr = ir_or(ir, sr, ir_shli(ir, sr_m, M_BIT));

  struct ir_value *sr_qm =
      ir_load_context(ir, offsetof(struct sh4_context, sr_qm), VALUE_I32);
  struct ir_value *sr_q =
      ir_zext(ir, ir_cmp_eq(ir, ir_lshri(ir, sr_qm, 31), sr_m), VALUE_I32);
  sr = ir_or(ir, sr, ir_shli(ir, sr_q, Q_BIT));

  return sr;
}

static void store_sr(struct sh4_guest *guest, struct ir *ir,
                     struct ir_value *v) {
  CHECK_EQ(v->type, VALUE_I32);
  v = ir_and(ir, v, ir_alloc_i32(ir, SR_MASK));

  struct ir_value *sr_updated = ir_alloc_ptr(ir, guest->sr_updated);
  struct ir_value *data = ir_alloc_ptr(ir, guest->data);
  struct ir_value *old_sr = load_sr(ir);

  ir_store_context(ir, offsetof(struct sh4_context, sr), v);

  /* inline version of sh4_explode_sr */
  struct ir_value *sr_t = ir_and(ir, v, ir_alloc_i32(ir, T_MASK));
  ir_store_context(ir, offsetof(struct sh4_context, sr_t), sr_t);

  struct ir_value *sr_s =
      ir_lshri(ir, ir_and(ir, v, ir_alloc_i32(ir, S_MASK)), S_BIT);
  ir_store_context(ir, offsetof(struct sh4_context, sr_s), sr_s);

  struct ir_value *sr_m =
      ir_lshri(ir, ir_and(ir, v, ir_alloc_i32(ir, M_MASK)), M_BIT);
  ir_store_context(ir, offsetof(struct sh4_context, sr_m), sr_m);

  struct ir_value *sr_q =
      ir_lshri(ir, ir_and(ir, v, ir_alloc_i32(ir, Q_MASK)), Q_BIT);
  struct ir_value *sr_qm =
      ir_shli(ir, ir_zext(ir, ir_cmp_eq(ir, sr_q, sr_m), VALUE_I32), 31);
  ir_store_context(ir, offsetof(struct sh4_context, sr_qm), sr_qm);

  ir_call_2(ir, sr_updated, data, old_sr);
}

static struct ir_value *load_fpscr(struct ir *ir) {
  struct ir_value *fpscr =
      ir_load_context(ir, offsetof(struct sh4_context, fpscr), VALUE_I32);
  return fpscr;
}

static void store_fpscr(struct sh4_guest *guest, struct ir *ir,
                        struct ir_value *v) {
  CHECK_EQ(v->type, VALUE_I32);
  v = ir_and(ir, v, ir_alloc_i32(ir, FPSCR_MASK));

  struct ir_value *fpscr_updated = ir_alloc_ptr(ir, guest->fpscr_updated);
  struct ir_value *data = ir_alloc_ptr(ir, guest->data);
  struct ir_value *old_fpscr = load_fpscr(ir);
  ir_store_context(ir, offsetof(struct sh4_context, fpscr), v);
  ir_call_2(ir, fpscr_updated, data, old_fpscr);
}

/* clang-format off */
#define I8                           struct ir_value*
#define I16                          struct ir_value*
#define I32                          struct ir_value*
#define I64                          struct ir_value*
#define F32                          struct ir_value*
#define F64                          struct ir_value*
#define V128                         struct ir_value*

#define FPU_DOUBLE_PR                (flags & SH4_DOUBLE_PR)
#define FPU_DOUBLE_SZ                (flags & SH4_DOUBLE_SZ)

#define DELAY_INSTR()                *delay_point = ir_get_insert_point(ir);
#define NEXT_INSTR()               

#define LOAD_CTX_I8(m)               ir_load_context(ir, offsetof(struct sh4_context, m), VALUE_I8)
#define LOAD_CTX_I16(m)              ir_load_context(ir, offsetof(struct sh4_context, m), VALUE_I16)
#define LOAD_CTX_I32(m)              ir_load_context(ir, offsetof(struct sh4_context, m), VALUE_I32)
#define LOAD_CTX_I64(m)              ir_load_context(ir, offsetof(struct sh4_context, m), VALUE_I64)
#define LOAD_CTX_F32(m)              ir_load_context(ir, offsetof(struct sh4_context, m), VALUE_F32)
#define LOAD_CTX_F64(m)              ir_load_context(ir, offsetof(struct sh4_context, m), VALUE_F64)
#define LOAD_CTX_V128(m)             ir_load_context(ir, offsetof(struct sh4_context, m), VALUE_V128)
#define STORE_CTX_I32(m, v)          ir_store_context(ir, offsetof(struct sh4_context, m), v);
#define STORE_CTX_I64(m, v)          ir_store_context(ir, offsetof(struct sh4_context, m), v);
#define STORE_CTX_F32(m, v)          ir_store_context(ir, offsetof(struct sh4_context, m), v);
#define STORE_CTX_F64(m, v)          ir_store_context(ir, offsetof(struct sh4_context, m), v);
#define STORE_CTX_V128(m, v)         ir_store_context(ir, offsetof(struct sh4_context, m), v);
#define STORE_CTX_IMM_I32(m, v)      STORE_CTX_I32(m, ir_alloc_i32(ir, v))

#define LOAD_GPR_I8(n)               LOAD_CTX_I8(r[n])
#define LOAD_GPR_I16(n)              LOAD_CTX_I16(r[n])
#define LOAD_GPR_I32(n)              LOAD_CTX_I32(r[n])
#define STORE_GPR_I32(n, v)          STORE_CTX_I32(r[n], v);
#define STORE_GPR_IMM_I32(n, v)      STORE_CTX_IMM_I32(r[n], v)

#define LOAD_GPR_ALT_I32(n)          LOAD_CTX_I32(ralt[n])
#define STORE_GPR_ALT_I32(n, v)      STORE_CTX_I32(ralt[n], v)

/* see notes in sh4_context.h for info regarding the 32-bit FPR accesses being swizzled */
#define LOAD_FPR_I32(n)              LOAD_CTX_I32(fr[(n)^1])
#define LOAD_FPR_I64(n)              LOAD_CTX_I64(fr[n])
#define LOAD_FPR_F32(n)              LOAD_CTX_F32(fr[(n)^1])
#define LOAD_FPR_F64(n)              LOAD_CTX_F64(fr[n])
#define LOAD_FPR_V128(n)             LOAD_CTX_V128(fr[n])
#define STORE_FPR_I32(n, v)          STORE_CTX_I32(fr[(n)^1], v)
#define STORE_FPR_I64(n, v)          STORE_CTX_I64(fr[n], v)
#define STORE_FPR_F32(n, v)          STORE_CTX_F32(fr[(n)^1], v)
#define STORE_FPR_F64(n, v)          STORE_CTX_F64(fr[n], v)
#define STORE_FPR_V128(n, v)         STORE_CTX_V128(fr[n], v)
#define STORE_FPR_IMM_I32(n, v)      STORE_FPR_I32(n, ir_alloc_i32(ir, v))

#define LOAD_XFR_I32(n)              LOAD_CTX_I32(xf[(n)^1])
#define LOAD_XFR_I64(n)              LOAD_CTX_I64(xf[n])
#define LOAD_XFR_V128(n)             LOAD_CTX_V128(xf[n])
#define STORE_XFR_I32(n, v)          STORE_CTX_I32(xf[(n)^1], v)
#define STORE_XFR_I64(n, v)          STORE_CTX_I64(xf[n], v)

#define LOAD_PR_I32()                LOAD_CTX_I32(pr)
#define STORE_PR_I32(v)              STORE_CTX_I32(pr, v)
#define STORE_PR_IMM_I32(v)          STORE_CTX_IMM_I32(pr, v)

#define LOAD_SR_I32()                load_sr(ir)
#define STORE_SR_I32(v)              store_sr(guest, ir, v)
#define STORE_SR_IMM_I32             STORE_SR_I32(ir_alloc_i32(ir, v))

#define LOAD_T_I32()                 LOAD_CTX_I32(sr_t)
#define STORE_T_I8(v)                STORE_CTX_I32(sr_t, ir_zext(ir, v, VALUE_I32))
#define STORE_T_I32(v)               STORE_CTX_I32(sr_t, v)
#define STORE_T_IMM_I32(v)           STORE_CTX_IMM_I32(sr_t, v)

#define LOAD_S_I32()                 LOAD_CTX_I32(sr_s)
#define STORE_S_I32(v)               STORE_CTX_I32(sr_s, v)
#define STORE_S_IMM_I32(v)           STORE_CTX_IMM_I32(sr_s, v)

#define LOAD_M_I32()                 LOAD_CTX_I32(sr_m)
#define STORE_M_I32(v)               STORE_CTX_I32(sr_m, v)
#define STORE_M_IMM_I32(v)           STORE_CTX_IMM_I32(sr_m, v)

#define LOAD_QM_I32()                LOAD_CTX_I32(sr_qm)
#define STORE_QM_I32(v)              STORE_CTX_I32(sr_qm, v)
#define STORE_QM_IMM_I32(v)          STORE_CTX_IMM_I32(sr_qm, v)

#define LOAD_FPSCR_I32()             load_fpscr(ir)
#define STORE_FPSCR_I32(v)           store_fpscr(guest, ir, v)
#define STORE_FPSCSR_IMM_I32         STORE_FPSCSR_I32(ir_alloc_i32(ir, v))

#define LOAD_DBR_I32()               LOAD_CTX_I32(dbr)
#define STORE_DBR_I32(v)             STORE_CTX_I32(dbr, v)
#define STORE_DBR_IMM_I32            STORE_CTX_IMM_I32(dbr, v)

#define LOAD_GBR_I32()               LOAD_CTX_I32(gbr)
#define STORE_GBR_I32(v)             STORE_CTX_I32(gbr, v)
#define STORE_GBR_IMM_I32(v)         STORE_CTX_IMM_I32(gbr, v)

#define LOAD_VBR_I32()               LOAD_CTX_I32(vbr)
#define STORE_VBR_I32(v)             STORE_CTX_I32(vbr, v)
#define STORE_VBR_IMM_I32            STORE_CTX_IMM_I32(vbr, v)

#define LOAD_FPUL_I16()              LOAD_CTX_I16(fpul)
#define LOAD_FPUL_I32()              LOAD_CTX_I32(fpul)
#define LOAD_FPUL_F32()              LOAD_CTX_F32(fpul)
#define STORE_FPUL_I32(v)            STORE_CTX_I32(fpul, v)
#define STORE_FPUL_F32(v)            STORE_CTX_F32(fpul, v)
#define STORE_FPUL_IMM_I32(v)        STORE_CTX_IMM_I32(fpul, v)

#define LOAD_MACH_I32()              LOAD_CTX_I32(mach)
#define STORE_MACH_I32(v)            STORE_CTX_I32(mach, v)
#define STORE_MACH_IMM_I32(v)        STORE_CTX_IMM_I32(mach, v)

#define LOAD_MACL_I32()              LOAD_CTX_I32(macl)
#define STORE_MACL_I32(v)            STORE_CTX_I32(macl, v)
#define STORE_MACL_IMM_I32(v)        STORE_CTX_IMM_I32(macl, v)

#define LOAD_SGR_I32()               LOAD_CTX_I32(sgr)
#define STORE_SGR_I32(v)             STORE_CTX_I32(sgr, v)
#define STORE_SGR_IMM_I32(v)         STORE_CTX_IMM_I32(sgr, v)

#define LOAD_SPC_I32()               LOAD_CTX_I32(spc)
#define STORE_SPC_I32(v)             STORE_CTX_I32(spc, v)
#define STORE_SPC_IMM_I32            STORE_CTX_IMM_I32(spc, v)

#define LOAD_SSR_I32()               LOAD_CTX_I32(ssr)
#define STORE_SSR_I32(v)             STORE_CTX_I32(ssr, v)
#define STORE_SSR_IMM_I32(v)         STORE_CTX_IMM_I32(ssr, v)

#define LOAD_I8(ea)                  ir_load_guest(ir, ea, VALUE_I8)
#define LOAD_I16(ea)                 ir_load_guest(ir, ea, VALUE_I16)
#define LOAD_I32(ea)                 ir_load_guest(ir, ea, VALUE_I32)
#define LOAD_I64(ea)                 ir_load_guest(ir, ea, VALUE_I64)
#define LOAD_IMM_I8(ea)              LOAD_I8(ir_alloc_i32(ir, ea))
#define LOAD_IMM_I16(ea)             LOAD_I16(ir_alloc_i32(ir, ea))
#define LOAD_IMM_I32(ea)             LOAD_I32(ir_alloc_i32(ir, ea))
#define LOAD_IMM_I64(ea)             LOAD_I64(ir_alloc_i32(ir, ea))

#define STORE_I8(ea, v)              ir_store_guest(ir, ea, v)
#define STORE_I16                    STORE_I8
#define STORE_I32                    STORE_I8
#define STORE_I64                    STORE_I8

#define LOAD_HOST_F32(ea)            ir_load_host(ir, ea, VALUE_F32)
#define LOAD_HOST_F64(ea)            ir_load_host(ir, ea, VALUE_F64)

#define FTOI_F32_I32(v)              ir_ftoi(ir, v, VALUE_I32)
#define FTOI_F64_I32(v)              ir_ftoi(ir, v, VALUE_I32)

#define ITOF_F32(v)                  ir_itof(ir, v, VALUE_F32)
#define ITOF_F64(v)                  ir_itof(ir, v, VALUE_F64)

#define SEXT_I8_I32(v)               ir_sext(ir, v, VALUE_I32)
#define SEXT_I16_I32(v)              ir_sext(ir, v, VALUE_I32)
#define SEXT_I16_I64(v)              ir_sext(ir, v, VALUE_I64)
#define SEXT_I32_I64(v)              ir_sext(ir, v, VALUE_I64)

#define ZEXT_I8_I32(v)               ir_zext(ir, v, VALUE_I32)
#define ZEXT_I16_I32(v)              ir_zext(ir, v, VALUE_I32)
#define ZEXT_I16_I64(v)              ir_zext(ir, v, VALUE_I64)
#define ZEXT_I32_I64(v)              ir_zext(ir, v, VALUE_I64)

#define TRUNC_I64_I32(v)             ir_trunc(ir, v, VALUE_I32)
#define FEXT_F32_F64(v)              ir_fext(ir, v, VALUE_F64)
#define FTRUNC_F64_F32(v)            ir_ftrunc(ir, v, VALUE_F32)

#define SELECT_I8(c, a, b)           ir_select(ir, c, a, b)
#define SELECT_I16                   SELECT_I8
#define SELECT_I32                   SELECT_I8
#define SELECT_I64                   SELECT_I8

#define CMPEQ_I8(a, b)               ir_cmp_eq(ir, a, b)
#define CMPEQ_I16                    CMPEQ_I8
#define CMPEQ_I32                    CMPEQ_I8
#define CMPEQ_I64                    CMPEQ_I8
#define CMPEQ_IMM_I8(a, b)           CMPEQ_I8(a, ir_alloc_i8(ir, b))
#define CMPEQ_IMM_I16(a, b)          CMPEQ_I8(a, ir_alloc_i16(ir, b))
#define CMPEQ_IMM_I32(a, b)          CMPEQ_I8(a, ir_alloc_i32(ir, b))
#define CMPEQ_IMM_I64(a, b)          CMPEQ_I8(a, ir_alloc_i64(ir, b))

#define CMPSLT_I32(a, b)             ir_cmp_slt(ir, a, b)
#define CMPSLT_IMM_I32(a, b)         CMPSLT_I32(a, ir_alloc_i32(ir, b))
#define CMPSLE_I32(a, b)             ir_cmp_sle(ir, a, b)
#define CMPSLE_IMM_I32               CMPSLE_I32(a, ir_alloc_i32(ir, b))
#define CMPSGT_I32(a, b)             ir_cmp_sgt(ir, a, b)
#define CMPSGT_IMM_I32(a, b)         CMPSGT_I32(a, ir_alloc_i32(ir, b))
#define CMPSGE_I32(a, b)             ir_cmp_sge(ir, a, b)
#define CMPSGE_IMM_I32(a, b)         CMPSGE_I32(a, ir_alloc_i32(ir, b))

#define CMPULT_I32(a, b)             ir_cmp_ult(ir, a, b)
#define CMPULT_IMM_I32(a, b)         CMPULT_I32(a, ir_alloc_i32(ir, b))
#define CMPULE_I32(a, b)             ir_cmp_ule(ir, a, b)
#define CMPULE_IMM_I32(a, b)         CMPULE_I32(a, ir_alloc_i32(ir, b))
#define CMPUGT_I32(a, b)             ir_cmp_ugt(ir, a, b)
#define CMPUGT_IMM_I32(a, b)         CMPUGT_I32(a, ir_alloc_i32(ir, b))
#define CMPUGE_I32(a, b)             ir_cmp_uge(ir, a, b)
#define CMPUGE_IMM_I32(a, b)         CMPUGE_I32(a, ir_alloc_i32(ir, b))

#define FCMPEQ_F32(a, b)             ir_fcmp_eq(ir, a, b)
#define FCMPEQ_F64(a, b)             FCMPEQ_F32(a, b)

#define FCMPGT_F32(a, b)             ir_fcmp_gt(ir, a, b)
#define FCMPGT_F64(a, b)             FCMPGT_F32(a, b)

#define ADD_I8(a, b)                 ir_add(ir, a, b)
#define ADD_I16                      ADD_I8
#define ADD_I32                      ADD_I8
#define ADD_I64                      ADD_I8
#define ADD_IMM_I8(a, b)             ADD_I8(a, ir_alloc_i8(ir, b))
#define ADD_IMM_I16(a, b)            ADD_I8(a, ir_alloc_i16(ir, b))
#define ADD_IMM_I32(a, b)            ADD_I8(a, ir_alloc_i32(ir, b))
#define ADD_IMM_I64(a, b)            ADD_I8(a, ir_alloc_i64(ir, b))

#define SUB_I8(a, b)                 ir_sub(ir, a, b)
#define SUB_I16                      SUB_I8
#define SUB_I32                      SUB_I8
#define SUB_I64                      SUB_I8
#define SUB_IMM_I8(a, b)             SUB_I8(a, ir_alloc_i8(ir, b))
#define SUB_IMM_I16(a, b)            SUB_I8(a, ir_alloc_i16(ir, b))
#define SUB_IMM_I32(a, b)            SUB_I8(a, ir_alloc_i32(ir, b))
#define SUB_IMM_I64(a, b)            SUB_I8(a, ir_alloc_i64(ir, b))

#define SMUL_I8(a, b)                ir_smul(ir, a, b)
#define SMUL_I16                     SMUL_I8
#define SMUL_I32                     SMUL_I8
#define SMUL_I64                     SMUL_I8
#define SMUL_IMM_I8(a, b)            SMUL_I8(a, ir_alloc_i8(ir, b))
#define SMUL_IMM_I16(a, b)           SMUL_I8(a, ir_alloc_i16(ir, b))
#define SMUL_IMM_I32(a, b)           SMUL_I8(a, ir_alloc_i32(ir, b))
#define SMUL_IMM_I64(a, b)           SMUL_I8(a, ir_alloc_i64(ir, b))

#define UMUL_I8(a, b)                ir_umul(ir, a, b)
#define UMUL_I16                     UMUL_I8
#define UMUL_I32                     UMUL_I8
#define UMUL_I64                     UMUL_I8
#define UMUL_IMM_I8(a, b)            UMUL_I8(a, ir_alloc_i8(ir, b))
#define UMUL_IMM_I16(a, b)           UMUL_I8(a, ir_alloc_i16(ir, b))
#define UMUL_IMM_I32(a, b)           UMUL_I8(a, ir_alloc_i32(ir, b))
#define UMUL_IMM_I64(a, b)           UMUL_I8(a, ir_alloc_i64(ir, b))

#define NEG_I8(a)                    ir_neg(ir, a)
#define NEG_I16                      NEG_I8
#define NEG_I32                      NEG_I8
#define NEG_I64                      NEG_I8

#define FADD_F32(a, b)               ir_fadd(ir, a, b)
#define FADD_F64                     FADD_F32

#define FSUB_F32(a, b)               ir_fsub(ir, a, b)
#define FSUB_F64                     FSUB_F32

#define FMUL_F32(a, b)               ir_fmul(ir, a, b)
#define FMUL_F64                     FMUL_F32

#define FDIV_F32(a, b)               ir_fdiv(ir, a, b)
#define FDIV_F64                     FDIV_F32

#define FNEG_F32(a)                  ir_fneg(ir, a)
#define FNEG_F64                     FNEG_F32

#define FABS_F32(a)                  ir_fabs(ir, a)
#define FABS_F64                     FABS_F32

#define FSQRT_F32(a)                 ir_sqrt(ir, a)
#define FSQRT_F64                    FSQRT_F32

#define FRSQRT_F32(a)                FDIV_F32(ir_alloc_f32(ir, 1.0f), FSQRT_F32(a))

#define VBROADCAST_F32(a)            ir_vbroadcast(ir, a)
#define VADD_F32(a, b)               ir_vadd(ir, a, b, VALUE_F32)
#define VMUL_F32(a, b)               ir_vmul(ir, a, b, VALUE_F32)
#define VDOT_F32(a, b)               ir_vdot(ir, a, b, VALUE_F32)

#define AND_I8(a, b)                 ir_and(ir, a, b)
#define AND_I16                      AND_I8
#define AND_I32                      AND_I8
#define AND_I64                      AND_I8
#define AND_IMM_I8(a, b)             AND_I8(a, ir_alloc_i8(ir, b))
#define AND_IMM_I16(a, b)            AND_I8(a, ir_alloc_i16(ir, b))
#define AND_IMM_I32(a, b)            AND_I8(a, ir_alloc_i32(ir, b))
#define AND_IMM_I64(a, b)            AND_I8(a, ir_alloc_i64(ir, b))

#define OR_I8(a, b)                  ir_or(ir, a, b)
#define OR_I16                       OR_I8
#define OR_I32                       OR_I8
#define OR_I64                       OR_I8
#define OR_IMM_I8(a, b)              OR_I8(a, ir_alloc_i8(ir, b))
#define OR_IMM_I16(a, b)             OR_I8(a, ir_alloc_i16(ir, b))
#define OR_IMM_I32(a, b)             OR_I8(a, ir_alloc_i32(ir, b))
#define OR_IMM_I64(a, b)             OR_I8(a, ir_alloc_i64(ir, b))

#define XOR_I8(a, b)                 ir_xor(ir, a, b)
#define XOR_I16                      XOR_I8
#define XOR_I32                      XOR_I8
#define XOR_I64                      XOR_I8
#define XOR_IMM_I8(a, b)             XOR_I8(a, ir_alloc_i8(ir, b))
#define XOR_IMM_I16(a, b)            XOR_I8(a, ir_alloc_i16(ir, b))
#define XOR_IMM_I32(a, b)            XOR_I8(a, ir_alloc_i32(ir, b))
#define XOR_IMM_I64(a, b)            XOR_I8(a, ir_alloc_i64(ir, b))

#define NOT_I8(a)                    ir_not(ir, a)
#define NOT_I16                      NOT_I8
#define NOT_I32                      NOT_I8
#define NOT_I64                      NOT_I8

#define SHL_I8(v, n)                 ir_shl(ir, v, n)
#define SHL_I16                      SHL_I8
#define SHL_I32                      SHL_I8
#define SHL_I64                      SHL_I8
#define SHL_IMM_I8(v, n)             ir_shli(ir, v, n)
#define SHL_IMM_I16                  SHL_IMM_I8
#define SHL_IMM_I32                  SHL_IMM_I8
#define SHL_IMM_I64                  SHL_IMM_I8

#define ASHR_I8(v, n)                ir_ashr(ir, v, n)
#define ASHR_I16                     ASHR_I8
#define ASHR_I32                     ASHR_I8
#define ASHR_I64                     ASHR_I8
#define ASHR_IMM_I8(v, n)            ir_ashri(ir, v, n)
#define ASHR_IMM_I16                 ASHR_IMM_I8
#define ASHR_IMM_I32                 ASHR_IMM_I8
#define ASHR_IMM_I64                 ASHR_IMM_I8

#define LSHR_I8(v, n)                ir_lshr(ir, v, n)
#define LSHR_I16                     LSHR_I8
#define LSHR_I32                     LSHR_I8
#define LSHR_I64                     LSHR_I8
#define LSHR_IMM_I8(v, n)            ir_lshri(ir, v, n)
#define LSHR_IMM_I16                 LSHR_IMM_I8
#define LSHR_IMM_I32                 LSHR_IMM_I8
#define LSHR_IMM_I64                 LSHR_IMM_I8

#define ASHD_I32(v, n)               ir_ashd(ir, v, n)
#define LSHD_I32(v, n)               ir_lshd(ir, v, n)

#define BRANCH_I32(d)                ir_branch(ir, d)
#define BRANCH_IMM_I32(d)            BRANCH_I32(ir_alloc_i32(ir, d))
#define BRANCH_COND_IMM_I32(c, t, f) ir_branch_cond(ir, c, ir_alloc_i32(ir, t), ir_alloc_i32(ir, f))

#define INVALID_INSTR()              {                                                                                     \
                                        struct ir_value *invalid_instr = ir_alloc_i64(ir, (uint64_t)guest->invalid_instr); \
                                        struct ir_value *data = ir_alloc_i64(ir, (uint64_t)guest->data);                   \
                                        ir_call_1(ir, invalid_instr, data);                                                \
                                     }


#define LDTLB()                      {                                                                   \
                                        struct ir_value *ltlb = ir_alloc_i64(ir, (uint64_t)guest->ltlb); \
                                        struct ir_value *data = ir_alloc_i64(ir, (uint64_t)guest->data); \
                                        ir_call_1(ir, ltlb, data);                                       \
                                     }


#define PREF_COND(c, addr)           {                                                                   \
                                        struct ir_value *pref = ir_alloc_i64(ir, (uint64_t)guest->pref); \
                                        struct ir_value *data = ir_alloc_i64(ir, (uint64_t)guest->data); \
                                        ir_call_cond_2(ir, c, pref, data, addr);                         \
                                     }

#define SLEEP()                      {                                                                     \
                                        struct ir_value *sleep = ir_alloc_i64(ir, (uint64_t)guest->sleep); \
                                        struct ir_value *data = ir_alloc_i64(ir, (uint64_t)guest->data);   \
                                        ir_call_1(ir, sleep, data);                                        \
                                     }

#define DEBUG_LOG(a, b, c)           ir_debug_log(ir, a, b, c)
/* clang-format on */

#define INSTR(name)                                                      \
  void sh4_translate_##name(struct sh4_guest *guest, struct ir *ir,      \
                            uint32_t addr, union sh4_instr i, int flags, \
                            struct ir_insert_point *delay_point)
#include "jit/frontend/sh4/sh4_instr.h"
#undef INSTR

sh4_translate_cb sh4_translators[NUM_SH4_OPS] = {
/* don't fill in an entry for the instruction if it's explicitly flagged to
   fallback to the interpreter */
#define SH4_INSTR(name, desc, sig, cycles, flags) \
  !((flags)&SH4_FLAG_FALLBACK) ? &sh4_translate_##name : NULL,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
};

sh4_translate_cb sh4_get_translator(uint16_t instr) {
  return sh4_translators[sh4_get_op(instr)];
}
