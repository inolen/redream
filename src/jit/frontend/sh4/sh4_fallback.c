#include "jit/frontend/sh4/sh4_fallback.h"
#include "core/core.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/jit.h"

static uint32_t load_sr(struct sh4_context *ctx) {
  sh4_implode_sr(ctx);
  return ctx->sr;
}

static void store_sr(struct sh4_guest *guest, struct sh4_context *ctx,
                     uint32_t new_sr) {
  uint32_t old_sr = load_sr(ctx);
  ctx->sr = new_sr & SR_MASK;
  sh4_explode_sr(ctx);
  guest->sr_updated(guest->data, old_sr);
}

static uint32_t load_fpscr(struct sh4_context *ctx) {
  return ctx->fpscr;
}

static void store_fpscr(struct sh4_guest *guest, struct sh4_context *ctx,
                        uint32_t new_fpscr) {
  uint32_t old_fpscr = load_fpscr(ctx);
  ctx->fpscr = new_fpscr & FPSCR_MASK;
  guest->fpscr_updated(guest->data, old_fpscr);
}

static inline int32_t vadd_f32_el(int32_t a, int32_t b) {
  float r = *(float *)&a + *(float *)&b;
  return *(int32_t *)&r;
}

static inline int32_t vmul_f32_el(int32_t a, int32_t b) {
  float r = *(float *)&a * *(float *)&b;
  return *(int32_t *)&r;
}

static inline float vdot_f32(int32_t *a, int32_t *b) {
  return *(float *)&a[0] * *(float *)&b[0] + *(float *)&a[1] * *(float *)&b[1] +
         *(float *)&a[2] * *(float *)&b[2] + *(float *)&a[3] * *(float *)&b[3];
}

/* clang-format off */
typedef int32_t int128_t[4];

#define I8                           int8_t
#define I16                          int16_t
#define I32                          int32_t
#define I64                          int64_t
#define F32                          float
#define F64                          double
#define V128                         int128_t

#define CTX                          ((struct sh4_context *)guest->ctx)
#define FPU_DOUBLE_PR                (CTX->fpscr & PR_MASK)
#define FPU_DOUBLE_SZ                (CTX->fpscr & SZ_MASK)

#define DELAY_INSTR()                {                                                                   \
                                       uint32_t delay_addr = addr + 2;                                   \
                                       uint16_t delay_data = guest->r16(guest->mem, delay_addr);       \
                                       const struct jit_opdef *def = sh4_get_opdef(delay_data);          \
                                       def->fallback((struct jit_guest *)guest, delay_addr, delay_data); \
                                     }
#define NEXT_INSTR()                 (CTX->pc = addr + 2)

#define LOAD_GPR_I8(n)               ((int8_t)CTX->r[n])
#define LOAD_GPR_I16(n)              ((int16_t)CTX->r[n])
#define LOAD_GPR_I32(n)              ((int32_t)CTX->r[n])
#define STORE_GPR_I32(n, v)          (CTX->r[n] = (v))
#define STORE_GPR_IMM_I32(n, v)      STORE_GPR_I32(n, v)

#define LOAD_GPR_ALT_I32(n)          ((int32_t)CTX->ralt[n])
#define STORE_GPR_ALT_I32(n, v)      (CTX->ralt[n] = (v))

#define LOAD_FPR_I32(n)              ((int32_t)CTX->fr[(n)^1])
#define LOAD_FPR_I64(n)              (*(int64_t *)&CTX->fr[n])
#define LOAD_FPR_F32(n)              (*(float *)&CTX->fr[(n)^1])
#define LOAD_FPR_F64(n)              (*(double *)&CTX->fr[n])
#define LOAD_FPR_V128(n)             {CTX->fr[(n)+0],CTX->fr[(n)+1],CTX->fr[(n)+2],CTX->fr[(n)+3]}
#define STORE_FPR_I32(n, v)          (CTX->fr[(n)^1] = (v))
#define STORE_FPR_I64(n, v)          (*(int64_t *)&CTX->fr[n] = (v))
#define STORE_FPR_F32(n, v)          (*(float *)&CTX->fr[(n)^1] = (v))
#define STORE_FPR_F64(n, v)          (*(double *)&CTX->fr[n] = (v))
#define STORE_FPR_V128(n, v)         memcpy(&CTX->fr[n], v, sizeof(v))
#define STORE_FPR_IMM_I32(n, v)      STORE_FPR_I32(n, v)

#define LOAD_XFR_I32(n)              ((int32_t)CTX->xf[(n)^1])
#define LOAD_XFR_I64(n)              (*(int64_t *)&CTX->xf[n])
#define LOAD_XFR_V128(n)             {CTX->xf[(n)+0],CTX->xf[(n)+1],CTX->xf[(n)+2],CTX->xf[(n)+3]}
#define STORE_XFR_I32(n, v)          (CTX->xf[(n)^1] = (v))
#define STORE_XFR_I64(n, v)          (*(int64_t *)&CTX->xf[n] = (v))

#define LOAD_PR_I32()                (CTX->pr)
#define STORE_PR_I32(v)              (CTX->pr = v)
#define STORE_PR_IMM_I32(v)          STORE_PR_I32(v)

#define LOAD_SR_I32()                load_sr(CTX)
#define STORE_SR_I32(v)              store_sr(guest, CTX, v)
#define STORE_SR_IMM_I32(v)          STORE_SR_I32(v)

#define LOAD_T_I32()                 (CTX->sr_t)
#define STORE_T_I8(v)                (CTX->sr_t = v)
#define STORE_T_I32(v)               STORE_T_I8(v)
#define STORE_T_IMM_I32(v)           STORE_T_I8(v)

#define LOAD_S_I32()                 (CTX->sr_s)
#define STORE_S_I32(v)               (CTX->sr_s = v)
#define STORE_S_IMM_I32(v)           STORE_S_I32(v)

#define LOAD_M_I32()                 (CTX->sr_m)
#define STORE_M_I32(v)               (CTX->sr_m = v)
#define STORE_M_IMM_I32(v)           STORE_M_I32(v)

#define LOAD_QM_I32()                (CTX->sr_qm)
#define STORE_QM_I32(v)              (CTX->sr_qm = v)
#define STORE_QM_IMM_I32(v)          STORE_QM_I32(v)

#define LOAD_FPSCR_I32()             load_fpscr(CTX)
#define STORE_FPSCR_I32(v)           store_fpscr(guest, CTX, v)
#define STORE_FPSCR_IMM_I32(v)       STORE_FPSCR_I32(v)

#define LOAD_DBR_I32()               (CTX->dbr)
#define STORE_DBR_I32(v)             (CTX->dbr = v)
#define STORE_DBR_IMM_I32(v)         STORE_DBR_I32(v)

#define LOAD_GBR_I32()               (CTX->gbr)
#define STORE_GBR_I32(v)             (CTX->gbr = v)
#define STORE_GBR_IMM_I32(v)         STORE_GBR_I32(v)

#define LOAD_VBR_I32()               (CTX->vbr)
#define STORE_VBR_I32(v)             (CTX->vbr = v)
#define STORE_VBR_IMM_I32(v)         STORE_VBR_I32(v)

#define LOAD_FPUL_I16()              (*(uint16_t *)&CTX->fpul)
#define LOAD_FPUL_I32()              (CTX->fpul)
#define LOAD_FPUL_F32()              (*(float *)&CTX->fpul)
#define STORE_FPUL_I32(v)            (CTX->fpul = v)
#define STORE_FPUL_F32(v)            (*(float *)&CTX->fpul = v)
#define STORE_FPUL_IMM_I32(v)        STORE_FPUL_I32(v)

#define LOAD_MACH_I32()              (CTX->mach)
#define STORE_MACH_I32(v)            (CTX->mach = v)
#define STORE_MACH_IMM_I32(v)        STORE_MACH_I32(v)

#define LOAD_MACL_I32()              (CTX->macl)
#define STORE_MACL_I32(v)            (CTX->macl = v)
#define STORE_MACL_IMM_I32(v)        STORE_MACL_I32(v)

#define LOAD_SGR_I32()               (CTX->sgr)
#define STORE_SGR_I32(v)             (CTX->sgr = v)
#define STORE_SGR_IMM_I32(v)         STORE_SGR_I32(v)

#define LOAD_SPC_I32()               (CTX->spc)
#define STORE_SPC_I32(v)             (CTX->spc = v)
#define STORE_SPC_IMM_I32(v)         STORE_SPC_I32(v)

#define LOAD_SSR_I32()               (CTX->ssr)
#define STORE_SSR_I32(v)             (CTX->ssr = v)
#define STORE_SSR_IMM_I32(v)         STORE_SSR_I32(v)

#define LOAD_I8(addr)                guest->r8(guest->mem, addr)
#define LOAD_I16(addr)               guest->r16(guest->mem, addr)
#define LOAD_I32(addr)               guest->r32(guest->mem, addr)
#define LOAD_I64(addr)               guest->r64(guest->mem, addr)
#define LOAD_IMM_I8(addr)            LOAD_I8(addr)
#define LOAD_IMM_I16(addr)           LOAD_I16(addr)
#define LOAD_IMM_I32(addr)           LOAD_I32(addr)
#define LOAD_IMM_I64(addr)           LOAD_I64(addr)

#define STORE_I8(addr, v)            guest->w8(guest->mem, addr, v)
#define STORE_I16(addr, v)           guest->w16(guest->mem, addr, v)
#define STORE_I32(addr, v)           guest->w32(guest->mem, addr, v)
#define STORE_I64(addr, v)           guest->w64(guest->mem, addr, v)

#define LOAD_HOST_F32(addr)          (*(float *)(uintptr_t)addr)
#define LOAD_HOST_F64(addr)          (*(double *)(uintptr_t)addr)

#define FTOI_F32_I32(v)              ((v) > (float)INT32_MAX ? INT32_MAX : (v) < (float)INT32_MIN ? INT32_MIN : (int32_t)(v))
#define FTOI_F64_I32(v)              ((v) > (double)INT32_MAX ? INT32_MAX : (v) < (double)INT32_MIN ? INT32_MIN : (int32_t)(v))

#define ITOF_F32(v)                  ((float)(v))
#define ITOF_F64(v)                  ((double)(v))

#define SEXT_I8_I32(v)               ((int32_t)(int8_t)(v))
#define SEXT_I16_I32(v)              ((int32_t)(int16_t)(v))
#define SEXT_I16_I64(v)              ((int64_t)(int16_t)(v))
#define SEXT_I32_I64(v)              ((int64_t)(int32_t)(v))

#define ZEXT_I8_I32(v)               ((uint32_t)(uint8_t)(v))
#define ZEXT_I16_I32(v)              ((uint32_t)(uint16_t)(v))
#define ZEXT_I16_I64(v)              ((uint64_t)(uint16_t)(v))
#define ZEXT_I32_I64(v)              ((uint64_t)(uint32_t)(v))

#define TRUNC_I64_I32(a)             ((uint32_t)(a))
#define FEXT_F32_F64(a)              ((double)(a))
#define FTRUNC_F64_F32(a)            ((float)(a))

#define SELECT_I8(c, a, b)           ((c) ? (a) : (b))
#define SELECT_I16                   SELECT_I8
#define SELECT_I32                   SELECT_I8
#define SELECT_I64                   SELECT_I8

#define CMPEQ_I8(a, b)               ((a) == (b))
#define CMPEQ_I16                    CMPEQ_I8
#define CMPEQ_I32                    CMPEQ_I8
#define CMPEQ_I64                    CMPEQ_I8
#define CMPEQ_IMM_I8                 CMPEQ_I8
#define CMPEQ_IMM_I16                CMPEQ_I8
#define CMPEQ_IMM_I32                CMPEQ_I8
#define CMPEQ_IMM_I64                CMPEQ_I8

#define CMPSLT_I32(a, b)             ((a) < (b))
#define CMPSLT_IMM_I32               CMPSLT_I32
#define CMPSLE_I32(a, b)             ((a) <= (b))
#define CMPSLE_IMM_I32               CMPSLE_I32
#define CMPSGT_I32(a, b)             ((a) > (b))
#define CMPSGT_IMM_I32               CMPSGT_I32
#define CMPSGE_I32(a, b)             ((a) >= (b))
#define CMPSGE_IMM_I32               CMPSGE_I32

#define CMPULT_I32(a, b)             ((uint32_t)(a) < (uint32_t)(b))
#define CMPULT_IMM_I32               CMPULT_I32
#define CMPULE_I32(a, b)             ((uint32_t)(a) <= (uint32_t)(b))
#define CMPULE_IMM_I32               CMPULE_I32
#define CMPUGT_I32(a, b)             ((uint32_t)(a) > (uint32_t)(b))
#define CMPUGT_IMM_I32               CMPUGT_I32
#define CMPUGE_I32(a, b)             ((uint32_t)(a) >= (uint32_t)(b))
#define CMPUGE_IMM_I32               CMPUGE_I32

#define FCMPEQ_F32(a, b)             ((a) == (b))
#define FCMPEQ_F64(a, b)             FCMPEQ_F32(a, b)

#define FCMPGT_F32(a, b)             ((a) > (b))
#define FCMPGT_F64(a, b)             FCMPGT_F32(a, b)

#define ADD_I8(a, b)                 ((a) + (b))
#define ADD_I16                      ADD_I8
#define ADD_I32                      ADD_I8
#define ADD_I64                      ADD_I8
#define ADD_IMM_I8                   ADD_I8
#define ADD_IMM_I16                  ADD_I8
#define ADD_IMM_I32                  ADD_I8
#define ADD_IMM_I64                  ADD_I8

#define SUB_I8(a, b)                 ((a) - (b))
#define SUB_I16                      SUB_I8
#define SUB_I32                      SUB_I8
#define SUB_I64                      SUB_I8
#define SUB_IMM_I8                   SUB_I8
#define SUB_IMM_I16                  SUB_I8
#define SUB_IMM_I32                  SUB_I8
#define SUB_IMM_I64                  SUB_I8

#define SMUL_I8(a, b)                ((int8_t)(a) * (int8_t)(b))
#define SMUL_I16(a, b)               ((int16_t)(a) * (int16_t)(b))
#define SMUL_I32(a, b)               ((int32_t)(a) * (int32_t)(b))
#define SMUL_I64(a, b)               ((int64_t)(a) * (int64_t)(b))
#define SMUL_IMM_I8                  SMUL_I8
#define SMUL_IMM_I16                 SMUL_I16
#define SMUL_IMM_I32                 SMUL_I32
#define SMUL_IMM_I64                 SMUL_I64

#define UMUL_I8(a, b)                ((uint8_t)(a) * (uint8_t)(b))
#define UMUL_I16(a, b)               ((uint16_t)(a) * (uint16_t)(b))
#define UMUL_I32(a, b)               ((uint32_t)(a) * (uint32_t)(b))
#define UMUL_I64(a, b)               ((uint64_t)(a) * (uint64_t)(b))
#define UMUL_IMM_I8                  UMUL_I8
#define UMUL_IMM_I16                 UMUL_I16
#define UMUL_IMM_I32                 UMUL_I32
#define UMUL_IMM_I64                 UMUL_I64

#define NEG_I8(a)                    (-(a))
#define NEG_I16                      NEG_I8
#define NEG_I32                      NEG_I8
#define NEG_I64                      NEG_I8

#define FADD_F32(a, b)               ((a) + (b))
#define FADD_F64                     FADD_F32

#define FSUB_F32(a, b)               ((a) - (b))
#define FSUB_F64                     FSUB_F32

#define FMUL_F32(a, b)               ((a) * (b))
#define FMUL_F64                     FMUL_F32

#define FDIV_F32(a, b)               ((a) / (b))
#define FDIV_F64                     FDIV_F32

#define FNEG_F32(a)                  (-(a))
#define FNEG_F64                     FNEG_F32

#define FABS_F32(a)                  fabsf(a)
#define FABS_F64(a)                  fabs(a)

#define FSQRT_F32(a)                 sqrtf(a)
#define FSQRT_F64(a)                 sqrt(a)
#define FRSQRT_F32(a)                (1.0f / sqrtf(a))

#define VBROADCAST_F32(a)            {*(int32_t *)&(a), *(int32_t *)&(a), *(int32_t *)&(a), *(int32_t *)&(a)}
#define VADD_F32(a, b)               {vadd_f32_el((a)[0], (b)[0]), \
                                      vadd_f32_el((a)[1], (b)[1]), \
                                      vadd_f32_el((a)[2], (b)[2]), \
                                      vadd_f32_el((a)[3], (b)[3])}
#define VMUL_F32(a, b)               {vmul_f32_el((a)[0], (b)[0]), \
                                      vmul_f32_el((a)[1], (b)[1]), \
                                      vmul_f32_el((a)[2], (b)[2]), \
                                      vmul_f32_el((a)[3], (b)[3])}
#define VDOT_F32(a, b)               vdot_f32(a, b)

#define AND_I8(a, b)                 ((a) & (b))
#define AND_I16                      AND_I8
#define AND_I32                      AND_I8
#define AND_I64                      AND_I8
#define AND_IMM_I8                   AND_I8
#define AND_IMM_I16                  AND_I8
#define AND_IMM_I32                  AND_I8
#define AND_IMM_I64                  AND_I8

#define OR_I8(a, b)                  ((a) | (b))
#define OR_I16                       OR_I8
#define OR_I32                       OR_I8
#define OR_I64                       OR_I8
#define OR_IMM_I8                    OR_I8
#define OR_IMM_I16                   OR_I8
#define OR_IMM_I32                   OR_I8
#define OR_IMM_I64                   OR_I8

#define XOR_I8(a, b)                 ((a) ^ (b))
#define XOR_I16                      XOR_I8
#define XOR_I32                      XOR_I8
#define XOR_I64                      XOR_I8
#define XOR_IMM_I8                   XOR_I8
#define XOR_IMM_I16                  XOR_I8
#define XOR_IMM_I32                  XOR_I8
#define XOR_IMM_I64                  XOR_I8

#define NOT_I8(a)                    (~(a))
#define NOT_I16                      NOT_I8
#define NOT_I32                      NOT_I8
#define NOT_I64                      NOT_I8

#define SHL_I8(v, n)                 (v << n)
#define SHL_I16(v, n)                (v << n)
#define SHL_I32(v, n)                (v << n)
#define SHL_I64(v, n)                (v << n)
#define SHL_IMM_I8                   SHL_I8
#define SHL_IMM_I16                  SHL_I16
#define SHL_IMM_I32                  SHL_I32
#define SHL_IMM_I64                  SHL_I64

#define ASHR_I8(v, n)                ((int8_t)v >> n)
#define ASHR_I16(v, n)               ((int16_t)v >> n)
#define ASHR_I32(v, n)               ((int32_t)v >> n)
#define ASHR_I64(v, n)               ((int64_t)v >> n)
#define ASHR_IMM_I8                  ASHR_I8
#define ASHR_IMM_I16                 ASHR_I16
#define ASHR_IMM_I32                 ASHR_I32
#define ASHR_IMM_I64                 ASHR_I64

#define LSHR_I8(v, n)                ((uint8_t)v >> n)
#define LSHR_I16(v, n)               ((uint16_t)v >> n)
#define LSHR_I32(v, n)               ((uint32_t)v >> n)
#define LSHR_I64(v, n)               ((uint64_t)v >> n)
#define LSHR_IMM_I8                  LSHR_I8
#define LSHR_IMM_I16                 LSHR_I16
#define LSHR_IMM_I32                 LSHR_I32
#define LSHR_IMM_I64                 LSHR_I64

#define ASHD_I32(v, n)               (((n) & 0x80000000) ? (((n) & 0x1f) ? ((v) >> -((n) & 0x1f)) : ((v) >> 31)) : ((v) << ((n) & 0x1f)))
#define LSHD_I32(v, n)               (((n) & 0x80000000) ? (((n) & 0x1f) ? ((uint32_t)(v) >> -((n) & 0x1f)) : 0) : ((uint32_t)(v) << ((n) & 0x1f)))

#define BRANCH_I32(d)                (CTX->pc = d)
#define BRANCH_IMM_I32               BRANCH_I32
#define BRANCH_COND_IMM_I32(c, t, f) { CTX->pc = c ? t : f; return; }

#define INVALID_INSTR()              guest->invalid_instr(guest->data)

#define LDTLB()                      guest->ltlb(guest->data)

#define PREF_COND(c, addr)           if (c) { guest->pref(guest->data, addr); }

#define SLEEP()                      guest->sleep(guest->data)

#define DEBUG_LOG(a, b, c)           LOG_INFO("DEBUG_LOG a=0x%" PRIx64 " b=0x%" PRIx64 " c=0x%" PRIx64, a, b, c)

/* clang-format on */

#define INSTR(name)                                                \
  void sh4_fallback_##name(struct sh4_guest *guest, uint32_t addr, \
                           union sh4_instr i)
#include "jit/frontend/sh4/sh4_instr.h"
#undef INSTR
