#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

struct jit_frontend;

struct jit_frontend *sh4_frontend_create();
void sh4_frontend_destroy(struct jit_frontend *frontend);

#endif
