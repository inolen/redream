#ifndef CONTROL_FLOW_ANALYSIS_PASS_H
#define CONTROL_FLOW_ANALYSIS_PASS_H

struct cfa;
struct ir;

struct cfa *cfa_create();
void cfa_destroy(struct cfa *cfa);
void cfa_run(struct cfa *cfa, struct ir *ir);

#endif
