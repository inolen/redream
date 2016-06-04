#ifndef DEAD_CODE_ELIMINATION_PASS_H
#define DEAD_CODE_ELIMINATION_PASS_H

#ifdef __cplusplus
extern "C" {
#endif

struct ir_s;

extern const char *dce_name;

void dce_run(struct ir_s *ir);

#ifdef __cplusplus
}
#endif

#endif
