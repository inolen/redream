#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

#ifdef __cplusplus
extern "C" {
#endif

struct jit_frontend_s;

struct jit_frontend_s *sh4_frontend_create();
void sh4_frontend_destroy(struct jit_frontend_s *frontend);

#ifdef __cplusplus
}
#endif

#endif
