#ifndef EXPRESSION_SIMPLIFICATION_PASS_H
#define EXPRESSION_SIMPLIFICATION_PASS_H

struct esimp;
struct ir;

struct esimp *esimp_create();
void esimp_destroy(struct esimp *esimp);
void esimp_run(struct esimp *esimp, struct ir *ir);

#endif
