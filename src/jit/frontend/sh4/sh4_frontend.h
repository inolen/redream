#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

struct jit_frontend_s;

struct jit_frontend_s *sh4_frontend_create();
void sh4_frontend_destroy(struct jit_frontend_s *frontend);

#endif
