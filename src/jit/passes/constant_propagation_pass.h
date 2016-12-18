#ifndef CONSTANT_PROPAGATION_PASS_H
#define CONSTANT_PROPAGATION_PASS_H

class ConstantPropagationPass : public Pass {
 public:
  static const char *NAME = "constprop";

  const char *name() {
    return NAME;
  }

  void Run(struct ir *ir);
};

#endif
