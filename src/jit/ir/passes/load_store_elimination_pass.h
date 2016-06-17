#ifndef LOAD_STORE_ELIMINATION_PASS_H
#define LOAD_STORE_ELIMINATION_PASS_H

struct ir;

extern const char *lse_name;

void lse_run(struct ir *ir);

#endif
