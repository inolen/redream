#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

struct ir;
struct jit_register;
struct ra;

struct ra *ra_create(const struct jit_register *registers, int num_registers);
void ra_destroy(struct ra *ra);
void ra_run(struct ra *ra, struct ir *ir);

#endif
