#ifndef LOAD_STORE_ELIMINATION_PASS_H
#define LOAD_STORE_ELIMINATION_PASS_H

struct ir_s;

extern const char *lse_name;

void lse_run(struct ir_s *ir);

#endif
