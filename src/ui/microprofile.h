#ifndef MICROPROFILE_IMPL_H
#define MICROPROFILE_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

struct microprofile_s;
struct window_s;

struct microprofile_s *mp_create(struct window_s *window);
void mp_destroy(struct microprofile_s *mp);

#ifdef __cplusplus
}
#endif

#endif
