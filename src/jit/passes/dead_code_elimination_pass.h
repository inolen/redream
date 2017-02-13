#ifndef DEAD_CODE_ELIMINATION_PASS_H
#define DEAD_CODE_ELIMINATION_PASS_H

struct ir;
struct dce;

struct dce *dce_create();
void dce_destroy(struct dce *dce);
void dce_run(struct dce *dce, struct ir *ir);

#endif
