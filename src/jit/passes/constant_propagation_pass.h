#ifndef CONSTANT_PROPAGATION_PASS_H
#define CONSTANT_PROPAGATION_PASS_H

struct cprop;
struct ir;

struct cprop *cprop_create();
void cprop_destroy(struct cprop *cprop);
void cprop_run(struct cprop *cprop, struct ir *ir);

#endif
