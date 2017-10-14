#ifndef SH4_INTC_H
#define SH4_INTC_H

void sh4_intc_update_pending(struct sh4 *sh4);
void sh4_intc_reprioritize(struct sh4 *sh4);

#endif
