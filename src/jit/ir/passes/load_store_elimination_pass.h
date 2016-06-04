#ifndef LOAD_STORE_ELIMINATION_PASS_H
#define LOAD_STORE_ELIMINATION_PASS_H

#ifdef __cplusplus
extern "C" {
#endif

struct ir_s;

extern const char *lse_name;

void lse_run(struct ir_s *ir);

#ifdef __cplusplus
}
#endif

#endif
