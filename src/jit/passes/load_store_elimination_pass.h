#ifndef LOAD_STORE_ELIMINATION_PASS_H
#define LOAD_STORE_ELIMINATION_PASS_H

struct ir;
struct lse;

struct lse *lse_create();
void lse_destroy(struct lse *lse);
void lse_run(struct lse *lse, struct ir *ir);

#endif
