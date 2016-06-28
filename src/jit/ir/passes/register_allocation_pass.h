#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

struct ir;
struct jit_register;

void ra_run(struct ir *ir, const struct jit_register *registers,
            int num_registers);

#endif
