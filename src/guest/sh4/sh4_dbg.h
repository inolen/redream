#ifndef SH4_DBG_H
#define SH4_DBG_H

int sh4_dbg_num_registers(struct device *dev);
void sh4_dbg_step(struct device *dev);
void sh4_dbg_add_breakpoint(struct device *dev, int type, uint32_t addr);
void sh4_dbg_remove_breakpoint(struct device *dev, int type, uint32_t addr);
void sh4_dbg_read_memory(struct device *dev, uint32_t addr, uint8_t *buffer,
                         int size);
void sh4_dbg_read_register(struct device *dev, int n, uint64_t *value,
                           int *size);
int sh4_dbg_invalid_instr(struct sh4 *sh4);

#endif
