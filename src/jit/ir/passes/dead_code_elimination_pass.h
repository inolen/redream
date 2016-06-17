#ifndef DEAD_CODE_ELIMINATION_PASS_H
#define DEAD_CODE_ELIMINATION_PASS_H

struct ir;

extern const char *dce_name;

void dce_run(struct ir *ir);

#endif
