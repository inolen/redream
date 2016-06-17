#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

struct ir;
struct register_def;

extern const char *ra_name;

void ra_run(struct ir *ir, const struct register_def *registers,
            int num_registers);

#endif
