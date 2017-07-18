#include <jit/ir/ir.h>

#if ARCH_X64
#include "jit/backend/x64/x64_backend.h"
#else
#include "jit/backend/interp/interp_backend.h"
#endif

static uint8_t dsp_code[64 * 1024] ALIGNED(4096);

struct _INST
{
	unsigned int TRA;
	unsigned int TWT;
	unsigned int TWA;
	
	unsigned int XSEL;
	unsigned int YSEL;
	unsigned int IRA;
	unsigned int IWT;
	unsigned int IWA;

	unsigned int EWT;
	unsigned int EWA;
	unsigned int ADRL;
	unsigned int FRCL;
	unsigned int SHIFT;
	unsigned int YRL;
	unsigned int NEGB;
	unsigned int ZERO;
	unsigned int BSEL;

	unsigned int NOFL;  //MRQ set
	unsigned int TABLE; //MRQ set
	unsigned int MWT;   //MRQ set
	unsigned int MRD;   //MRQ set
	unsigned int MASA;  //MRQ set
	unsigned int ADREB; //MRQ set
	unsigned int NXADR; //MRQ set
};

void DecodeInst(uint32_t *IPtr, struct _INST *i) {
	i->TRA=(IPtr[0]>>9)&0x7F;
	i->TWT=(IPtr[0]>>8)&0x01;
	i->TWA=(IPtr[0]>>1)&0x7F;
	
	i->XSEL=(IPtr[1]>>15)&0x01;
	i->YSEL=(IPtr[1]>>13)&0x03;
	i->IRA=(IPtr[1]>>7)&0x3F;
	i->IWT=(IPtr[1]>>6)&0x01;
	i->IWA=(IPtr[1]>>1)&0x1F;

	i->TABLE=(IPtr[2]>>15)&0x01;
	i->MWT=(IPtr[2]>>14)&0x01;
	i->MRD=(IPtr[2]>>13)&0x01;
	i->EWT=(IPtr[2]>>12)&0x01;
	i->EWA=(IPtr[2]>>8)&0x0F;
	i->ADRL=(IPtr[2]>>7)&0x01;
	i->FRCL=(IPtr[2]>>6)&0x01;
	i->SHIFT=(IPtr[2]>>4)&0x03;
	i->YRL=(IPtr[2]>>3)&0x01;
	i->NEGB=(IPtr[2]>>2)&0x01;
	i->ZERO=(IPtr[2]>>1)&0x01;
	i->BSEL=(IPtr[2]>>0)&0x01;

	i->NOFL=(IPtr[3]>>15)&1;
	
	i->MASA=(IPtr[3]>>9)&0x3f;
	i->ADREB=(IPtr[3]>>8)&0x1;
	i->NXADR=(IPtr[3]>>7)&0x1;
}

static uint16_t PACK(int32_t val) {
	uint32_t temp;
	int sign,exponent,k;

	sign = (val >> 23) & 0x1;
	temp = (val ^ (val << 1)) & 0xFFFFFF;
	exponent = 0;
	for (k=0; k<12; k++)
	{
		if (temp & 0x800000)
			break;
		temp <<= 1;
		exponent += 1;
	}
	if (exponent < 12)
		val = (val << exponent) & 0x3FFFFF;
	else
		val <<= 11;
	val >>= 11;
	val |= sign << 15;
	val |= exponent << 11;

	return (uint16_t)val;
}

static int32_t UNPACK(uint16_t val) {
	int sign,exponent,mantissa;
	int32_t uval;

	sign = (val >> 15) & 0x1;
	exponent = (val >> 11) & 0xF;
	mantissa = val & 0x7FF;
	uval = mantissa << 11;
	if (exponent > 11)
		exponent = 11;
	else
		uval |= (sign ^ 1) << 22;
	uval |= sign << 23;
	uval <<= 8;
	uval >>= 8;
	uval >>= exponent;

	return uval;
}

static void dsp_run(struct aica *aica) {
	struct dsp *dsp = &aica->dsp;

	memset(aica->dsp_data->EFREG, 0, sizeof(aica->dsp_data->EFREG));

	dsp->regs.MDEC_CT &= dsp->buffered.RBL;

	dsp->dsp_program();

	dsp->regs.MDEC_CT--;
}

static void dsp_compile(struct aica *aica) {
  struct dsp *dsp = &aica->dsp;

	struct jit_block dsp_block = {0};
  struct ir ir = {0};
  static uint8_t ir_buffer[1024 * 32];

  ir.buffer = ir_buffer;
  ir.capacity = sizeof(ir_buffer);

	dsp->backend->reset(dsp->backend);

  for(int step=0;step<128;++step) {

		uint32_t* mpro=aica->dsp_data->MPRO+step*4;

		struct _INST op;
		DecodeInst(mpro,&op);

		printf("[%d] "
			"TRA %d,TWT %d,TWA %d,XSEL %d,YSEL %d,IRA %d,IWT %d,IWA %d,TABLE %d,MWT %d,MRD %d,EWT %d,EWA %d,ADRL %d,FRCL %d,SHIFT %d,YRL %d,NEGB %d,ZERO %d,BSEL %d,NOFL %d,MASA %d,ADREB %d,NXADR %d\n"
			,step
			,op.TRA,op.TWT,op.TWA,op.XSEL,op.YSEL,op.IRA,op.IWT,op.IWA,op.TABLE,op.MWT,op.MRD,op.EWT,op.EWA,op.ADRL,op.FRCL,op.SHIFT,op.YRL,op.NEGB,op.ZERO,op.BSEL,op.NOFL,op.MASA,op.ADREB,op.NXADR);


#if 0
		//Dynarec !
		_dsp_debug_step_start();
		//DSP regs are on memory
		//Wires stay on x86 regs, written to memory as fast as possible
		
		//EDI=MEM_RD_DATA_NV
		MEM_RD_DATA_NV = dsp_rec_DRAM_CI(x86e,prev_op,step);
		
		//;)
		//Address Generation Unit ! nothing spectacular really ...
		dsp_rec_MEM_AGU(x86e,op,step);
		
		//Calculate INPUTS wire
		//ESI : INPUTS
		INPUTS = dsp_rec_INPUTS(x86e,op);
		
		//:o ?
		//Write the MEMS register
		dsp_rec_MEMS_WRITE(x86e,op,step,INPUTS);
		
		//Write the MEM_RD_DATA regiter
		dsp_rec_MEM_RD_DATA_WRITE(x86e,op,step,MEM_RD_DATA_NV);
		
		//EDI is used for MAD_OUT_NV
		//Mul-add
		MAD_OUT_NV = dsp_rec_MAD(x86e,op,step, INPUTS);
		
		//Effect output/ Feedback
		dsp_rec_EFO_FB(x86e,op,step, INPUTS);

		//Write MAD_OUT_NV
		{
			x86e.Emit(op_mov32,&dsp.regs.MAD_OUT, MAD_OUT_NV);
			wtn(MAD_OUT);
		}
		//These are implemented here :p

		//Inputs -> Y reg
		//Last use of inputs (ESI) and its destructive at that ;p
		{
			if (op.YRL)
			{
				x86e.Emit(op_sar32,INPUTS,4);//[23:4]
				x86e.Emit(op_mov32,&dsp.regs.Y_REG,INPUTS);

			}
			wtn(Y_REG);
		}

		//NOFL delay propagation :)
		{
			//NOFL_2=NOFL_1
			x86e.Emit(op_mov32,EAX,&dsp.regs.NOFL_1);
			x86e.Emit(op_mov32,&dsp.regs.NOFL_2,EAX);
			//NOFL_1 = NOFL
			x86e.Emit(op_mov32,&dsp.regs.NOFL_1,op.NOFL);

			wtn(NOFL_2);
			wtn(NOFL_1);
		}

		//MWT_1/MRD_1 propagation
		{
			//MWT_1=MWT
			x86e.Emit(op_mov32,&dsp.regs.MWT_1,op.MWT);
			//MRD_1=MRD
			x86e.Emit(op_mov32,&dsp.regs.MRD_1,op.MRD);

			wtn(MWT_1);
			wtn(MRD_1);
		}

		_dsp_debug_step_end();
#endif
	}

	int res = dsp->backend->assemble_code(dsp->backend, &dsp_block, &ir, JIT_ABI_CDECL);

	if (!res) {
		LOG_FATAL("dsp compile failed");
	}

	dsp->dsp_program = dsp_block.host_addr;
  dsp->step = &dsp_run;

  printf("Compiled DSP microcode-> %p\n", dsp->dsp_program);

  aica->dsp.step(aica);
}

uint32_t aica_dsp_reg_read(struct aica *aica, uint32_t addr,
                                  uint32_t data_mask) {
  return READ_DATA(&aica->reg[addr]);
}

void aica_dsp_reg_write(struct aica *aica, uint32_t addr, uint32_t data,
                               uint32_t data_mask) {
  
  WRITE_DATA(&aica->reg[addr]);

  //COEF : native
  //MEMS : native
  //MPRO : native
  if (addr >= 0x3400 && addr < 0x3C00) {
    aica_dsp_invalidate(aica);
  }
}

void aica_dsp_init(struct aica *aica) {
	struct dsp *dsp = &aica->dsp;

	aica_dsp_invalidate(aica);

	dsp->regs.MDEC_CT = 1;

#if ARCH_X64
  dsp->backend = x64_backend_create(dsp_code, sizeof(dsp_code));
#else
  dsp->backend = interp_backend_create();
#endif
}

void aica_dsp_invalidate(struct aica *aica) {
	struct dsp *dsp = &aica->dsp;

	dsp->step = &dsp_compile;

	dsp->buffered.RBL = (8192 << aica->common_data->RBL) - 1;

	dsp->buffered.RBP = aica->common_data->RBP * 2048;
}