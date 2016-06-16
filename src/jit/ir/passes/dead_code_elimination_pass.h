#ifndef DEAD_CODE_ELIMINATION_PASS_H
#define DEAD_CODE_ELIMINATION_PASS_H

struct ir_s;

extern const char *dce_name;

void dce_run(struct ir_s *ir);

#endif
