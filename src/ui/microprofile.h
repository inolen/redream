#ifndef MICROPROFILE_IMPL_H
#define MICROPROFILE_IMPL_H

struct microprofile_s;
struct window_s;

struct microprofile_s *mp_create(struct window_s *window);
void mp_destroy(struct microprofile_s *mp);

#endif
