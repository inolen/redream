struct dsp {
  struct jit_backend *backend;
  void (*step)(struct aica *);
  void (*dsp_program)();

  struct {
    bool MAD_OUT;
    bool MEM_ADDR;
    bool MEM_RD_DATA;
    bool MEM_WT_DATA;
    bool FRC_REG;
    bool ADRS_REG;
    bool Y_REG;

    bool MDEC_CT;
    bool MWT_1;
    bool MRD_1;
    // bool MADRS;
    bool MEMS;
    bool NOFL_1;
    bool NOFL_2;

    bool TEMPS;
    bool EFREG;
  } regs_init;

  // int32_t -> stored as signed extended to 32 bits

  // pipeline state
  struct {
    int32_t MAD_OUT;
    int32_t MEM_RD_DATA;
    int32_t MEM_WT_DATA;

    uint32_t MWT_1;
    uint32_t MRD_1;
    uint32_t MADRS;
    uint32_t NOFL_1;
    uint32_t NOFL_2;
  } pipeline;

  // dsp state
  struct {
    int32_t MEM_ADDR;
    int32_t FRC_REG;
    int32_t ADRS_REG;
    int32_t Y_REG;

    uint32_t DEC;
    uint32_t MDEC_CT;
  } regs;

  // buffered configuration registers
  struct {
    // 24 bit wide
    uint32_t TEMP[128];
    // 24 bit wide
    uint32_t MEMS[32];
    // 20 bit wide
    int32_t MIXS[16];

    // RBL/RBP (decoded)
    uint32_t RBP;
    uint32_t RBL;
  } buffered;
};

void aica_dsp_init(struct aica *);
void aica_dsp_invalidate(struct aica *);

uint32_t aica_dsp_reg_read(struct aica *aica, uint32_t addr,
                           uint32_t data_mask);

void aica_dsp_reg_write(struct aica *aica, uint32_t addr, uint32_t data,
                        uint32_t data_mask);