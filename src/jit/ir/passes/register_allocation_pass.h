#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

#ifdef __cplusplus
extern "C" {
#endif

struct ir_s;
struct register_def_s;

extern const char *ra_name;

void ra_run(struct ir_s *ir, const struct register_def_s *registers,
            int num_registers);

#ifdef __cplusplus
}
#endif

#endif
