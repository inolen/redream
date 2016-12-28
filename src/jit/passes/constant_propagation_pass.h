#ifndef CONSTANT_PROPAGATION_PASS_H
#define CONSTANT_PROPAGATION_PASS_H

struct ir;

void cprop_run(struct ir *ir);

#endif
